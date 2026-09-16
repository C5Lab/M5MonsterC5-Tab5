"""Execute the firmware's C verifier with real mbedTLS and synthetic captures.

Linux/WSL: python3 tests/test_handshake_cracker.py --mbedtls /path/to/mbedtls
No radio, real captures or network access are used.
"""
import argparse
import ctypes
import hashlib
import hmac
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def capture(password=b"lab-passphrase", ssid=b"Lab fixture", keyver=2):
    ap, sta = bytes.fromhex("020000000001"), bytes.fromhex("020000000002")
    anonce, snonce = bytes(range(32)), bytes(range(32, 64))
    eapol = bytearray(99)
    eapol[:7] = bytes([2, 3, 0, 95, 2, 1, 8 | keyver])
    eapol[17:49] = snonce
    pmk = hashlib.pbkdf2_hmac("sha1", password, ssid, 4096, 32)
    kck = hmac.digest(pmk, b"Pairwise key expansion\0" + ap + sta + anonce + snonce + b"\0", "sha1")[:16]
    mic = hmac.digest(kck, eapol, "md5" if keyver == 1 else "sha1")[:16]
    return struct.pack("<IIBB32sB16s6s32s6s32sH256s", 0x58504348, 4, 0,
                       len(ssid), ssid, keyver, mic, ap, anonce, sta, snonce, len(eapol), eapol)


class VerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="tab5-cracker-")
        cls.folder = Path(cls.tmp.name)
        source = (Path(__file__).resolve().parents[1] / "main/main.c").read_text()
        types = source[source.index("#define HCCAPX_SIGNATURE"):source.index("typedef struct {\n    volatile bool active;", source.index("#define HCCAPX_SIGNATURE"))]
        core = source[source.index("static void hs_crack_compute_kck") if "static void hs_crack_compute_kck" in source else source.index("static bool hs_crack_compute_kck"):source.index("static void hs_crack_set_stage")]
        # Compile production functions directly, exposing static functions to ctypes.
        code = '#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n#include <errno.h>\n#include "mbedtls/md.h"\n#include "mbedtls/pkcs5.h"\n'
        code += types + core.replace("static ", "")
        code = '#include "hs_crack_crypto.h"\n#include "mbedtls/platform_util.h"\n' + code
        wordlist = source[source.index('    FILE *wl = fopen(HS_CRACK_WORDLIST'):source.index('\ndone:', source.index('    FILE *wl = fopen(HS_CRACK_WORDLIST'))]
        code += r'''
#undef HS_CRACK_WORDLIST
#define HS_CRACK_WORDLIST path
static FILE *observed;
#define vTaskDelay(ticks) ((void)(ticks))
static struct { bool cancel_requested; } hs_crack_ui;
static bool hs_crack_step(const char *pw, const hccapx_record_t *recs, int nrecs,
                         uint32_t *tried, int64_t t0, int *found_idx, char *found_pw, size_t size) {
    (void)recs; (void)nrecs; (void)tried; (void)t0; (void)found_idx; (void)found_pw; (void)size;
    size_t len = strlen(pw);
    if (len >= 8 && len <= 63) fprintf(observed, "%s\n", pw);
    return false;
}
void read_words(const char *path, const char *out) {
    hccapx_record_t *recs = NULL;
    int nrecs = 0, found_idx = -1;
    uint32_t tried = 0;
    int64_t t0 = 0;
    char found_pw[64] = {0}, final_detail[256] = {0};
    bool sd_wordlist_used = false;
    (void)final_detail;
    (void)hs_crack_ui;
    observed = fopen(out, "w");
''' + wordlist + r'''
    (void)sd_wordlist_used;
    fclose(observed);
}
'''
        (cls.folder / "core.c").write_text(code)
        (cls.folder / "config.h").write_text("\n".join("#define " + name for name in
            ["MBEDTLS_MD_C", "MBEDTLS_MD5_C", "MBEDTLS_SHA1_C", "MBEDTLS_PKCS5_C"]))
        sources = [MBEDTLS / "library" / (name + ".c") for name in
                   ["md", "md5", "sha1", "pkcs5", "platform_util", "constant_time"]]
        subprocess.run(["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        '-DMBEDTLS_CONFIG_FILE="config.h"', "-I" + str(cls.folder),
                        "-I" + str(MBEDTLS / "include"), "-I" + str(MBEDTLS / "library"),
                        "-I" + str(Path(__file__).resolve().parents[1] / "main"),
                        str(Path(__file__).resolve().parents[1] / "main/hs_crack_crypto.c"),
                        str(cls.folder / "core.c"),
                        *map(str, sources), "-o", str(cls.folder / "core.so")], check=True)
        cls.lib = ctypes.CDLL(str(cls.folder / "core.so"))
        cls.lib.hs_crack_test_password.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
        cls.lib.hs_crack_test_password.restype = ctypes.c_bool
        cls.lib.hs_crack_load_records.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
        cls.lib.hs_crack_try_candidate.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
        cls.lib.read_words.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        cls.lib.hs_crack_eta_seconds.argtypes = [ctypes.c_uint64, ctypes.c_uint32, ctypes.c_int64]
        cls.lib.hs_crack_eta_seconds.restype = ctypes.c_uint64
        cls.checkpoint_type = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p)
        cls.lib.hs_crack_verify_candidate.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
            ctypes.c_int, cls.checkpoint_type, ctypes.c_void_p]
        cls.lib.hs_crack_verify_candidate.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_correct_and_wrong_password_wpa_and_wpa2(self):
        for version in (1, 2):
            record = capture(keyver=version)
            self.assertTrue(self.lib.hs_crack_test_password(b"lab-passphrase", record))
            self.assertFalse(self.lib.hs_crack_test_password(b"wrong-password", record))

    def test_binary_ssid_and_password_boundaries(self):
        for password in (b"12345678", b"x" * 63, b" spaces "):
            record = capture(password, b"Lab\0SSID")
            self.assertTrue(self.lib.hs_crack_test_password(password, record))
        for password in (b"1234567", b"x" * 64):
            self.assertFalse(self.lib.hs_crack_test_password(password, capture(password)))

    def test_synthetic_password_with_both_ssid_cases(self):
        for ssid in (b"orbitlab", b"OrbitLab"):
            for keyver in (1, 2):
                with self.subTest(ssid=ssid, keyver=keyver):
                    record = capture(b"OrbitLab7392@", ssid, keyver)
                    self.assertTrue(self.lib.hs_crack_test_password(b"OrbitLab7392@", record))
                    self.assertFalse(self.lib.hs_crack_test_password(b"orbitlab7392@", record))
                    self.assertFalse(self.lib.hs_crack_test_password(b"OrbitLab7392!", record))

    def test_valid_password_rejected_for_corrupted_capture(self):
        # Structurally valid but cryptographically inconsistent MIC, SSID or nonce.
        original = capture(b"OrbitLab7392@", b"orbitlab")
        for offset in (43, 10, 65):
            record = bytearray(original)
            record[offset] ^= 1
            with self.subTest(offset=offset):
                self.assertFalse(self.lib.hs_crack_test_password(b"OrbitLab7392@", bytes(record)))

    def test_mic_field_is_zeroed_before_verification(self):
        record = bytearray(capture())
        record[137 + 81:137 + 97] = record[43:59]
        self.assertTrue(self.lib.hs_crack_test_password(b"lab-passphrase", bytes(record)))

    def test_returns_matching_record_for_mixed_networks(self):
        records = capture(b"another-password", b"Other Lab") + capture()
        self.assertEqual(self.lib.hs_crack_try_candidate(b"lab-passphrase", records, 2), 1)
        self.assertEqual(self.lib.hs_crack_try_candidate(b"wrong-password", records, 2), -1)

    def test_same_ssid_reuses_pmk_without_skipping_second_mic(self):
        for first_ssid, expected_calls in [(b"Lab fixture", 259), (b"Other Lab", 516)]:
            calls = [0]
            def checkpoint(_):
                calls[0] += 1
                return True
            callback = self.checkpoint_type(checkpoint)
            records = capture(b"another-password", first_ssid) + capture()
            self.assertEqual(self.lib.hs_crack_verify_candidate(
                b"lab-passphrase", records, 2, callback, None), 1)
            self.assertEqual(calls[0], expected_calls)

    def load(self, data):
        path = self.folder / "fixture.hccapx"
        path.write_bytes(data)
        return self.lib.hs_crack_load_records(str(path).encode(), ctypes.create_string_buffer(393 * 16), 16)

    def test_rejects_malformed_and_unsupported_records(self):
        for offset, value in [(0, 0), (4, 99), (9, 33), (42, 3), (135, 1)]:
            with self.subTest(offset=offset):
                record = bytearray(capture())
                record[offset] = value
                self.assertLess(self.load(record), 0)
        self.assertLess(self.load(capture() + b"truncated"), 0)
        self.assertLess(self.load(capture() * 17), 0)
        self.assertEqual(self.load(capture() * 2), 2)

    def test_wordlist_preserves_spaces_and_skips_entire_long_lines(self):
        path, output = self.folder / 'words.txt', self.folder / 'observed.txt'
        path.write_bytes(b' spaces \r\n' + b'x' * 79 + b'not-a-new-password\n'
                         + b'a\0invalid-password\n' + b'valid-last-password')
        self.lib.read_words(str(path).encode(), str(output).encode())
        self.assertEqual(output.read_bytes(), b' spaces \nvalid-last-password\n')

    def test_wordlist_length_boundaries_and_crlf(self):
        path, output = self.folder / 'bounds.txt', self.folder / 'bounds-out.txt'
        path.write_bytes(b'x' * 63 + b'\r\n' + b'x' * 64 + b'\r\n' + b'x' * 7
                         + b'\n' + b'x' * 8 + b'\n' + b'x' * 10000 + b'\n')
        self.lib.read_words(str(path).encode(), str(output).encode())
        self.assertEqual(output.read_bytes(), b'x' * 63 + b'\n' + b'x' * 8 + b'\n')

    def test_eta_uses_remaining_candidates_and_rounds_up(self):
        # Six checks in 25 seconds; 64 left require 266.66... seconds.
        self.assertEqual(self.lib.hs_crack_eta_seconds(70, 6, 25000000), 267)
        self.assertEqual(self.lib.hs_crack_eta_seconds(1000, 10, 40000000), 3960)
        self.assertEqual(self.lib.hs_crack_eta_seconds(70, 70, 25000000), 0)
        self.assertEqual(self.lib.hs_crack_eta_seconds(70, 71, 25000000), 0)
        self.assertEqual(self.lib.hs_crack_eta_seconds(70, 0, 0), 2**64 - 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mbedtls", required=True, type=Path)
    args, rest = parser.parse_known_args()
    MBEDTLS = args.mbedtls
    unittest.main(argv=[__file__, *rest])
