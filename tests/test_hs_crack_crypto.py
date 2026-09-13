"""Reference tests. Default execution NEVER invokes a compiler.

To run C vectors later, explicitly set HS_CRACK_CRYPTO_BUILD=1 and MBEDTLS_ROOT
to the ESP-IDF components/mbedtls/mbedtls directory on a host with gcc, or set
HS_CRACK_CRYPTO_LIBRARY to a previously built shared library.
"""
import ctypes
from concurrent.futures import ThreadPoolExecutor
import hashlib
import hmac
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENABLED = bool(os.environ.get("HS_CRACK_CRYPTO_LIBRARY")) or os.environ.get("HS_CRACK_CRYPTO_BUILD") == "1"
CHECKPOINT = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p)


class SourceContract(unittest.TestCase):
    def test_private_software_backend(self):
        source = (ROOT / "main/hs_crack_crypto.c").read_text()
        header = (ROOT / "main/hs_crack_crypto.h").read_text()
        self.assertIn('#include "sha1.c"', source)
        self.assertIn('hs_crack_sha1_config.h', source)
        self.assertNotIn('mbedtls_sha1_context', header)
        self.assertNotIn('malloc(', source)
        self.assertIn('mbedtls_platform_zeroize', source)


@unittest.skipUnless(ENABLED, "C reference tests require explicit opt-in; no compilation by default")
class CryptoVectors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        supplied = os.environ.get("HS_CRACK_CRYPTO_LIBRARY")
        if supplied:
            library = Path(supplied)
        else:
            upstream = Path(os.environ["MBEDTLS_ROOT"])
            cls.temp = tempfile.TemporaryDirectory()
            cls.addClassCleanup(cls.temp.cleanup)
            library = Path(cls.temp.name) / "hs_crypto.so"
            subprocess.run([
                os.environ.get("CC", "gcc"), "-std=c99", "-O2", "-Wall", "-Wextra",
                "-Werror", "-shared", "-fPIC", '-DMBEDTLS_CONFIG_FILE="hs_crack_sha1_config.h"',
                "-I" + str(ROOT / "main"), "-I" + str(upstream / "include"),
                "-I" + str(upstream / "library"), str(ROOT / "main/hs_crack_crypto.c"),
                str(upstream / "library/platform_util.c"), "-o", str(library),
            ], check=True)
        cls.lib = ctypes.CDLL(str(library.resolve()))
        cls.lib.hs_crack_derive_pmk.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
            ctypes.c_size_t, ctypes.c_void_p, CHECKPOINT, ctypes.c_void_p]
        cls.lib.hs_crack_derive_pmk.restype = ctypes.c_int
        cls.lib.hs_crack_hmac_sha1.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
            ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        cls.lib.hs_crack_hmac_sha1.restype = ctypes.c_int

    def derive(self, password, ssid, checkpoint=None):
        output = (ctypes.c_ubyte * 32)(*([0xA5] * 32))
        callback = CHECKPOINT(checkpoint) if checkpoint else CHECKPOINT()
        result = self.lib.hs_crack_derive_pmk(password, ssid, len(ssid), output, callback, None)
        return result, bytes(output)

    def test_pmk_vectors_and_boundaries(self):
        rng = random.Random(701)
        vectors = [(b"password", b"IEEE"), (b"ThisIsAPassword", b"ThisIsASSID"),
                   (b"a" * 8, b""), (b"z" * 63, bytes(range(32))),
                   (b"12345678", b"zero\0inside")]
        vectors += [(bytes(rng.randrange(33, 127) for _ in range(rng.randrange(8, 64))),
                     bytes(rng.randrange(256) for _ in range(rng.randrange(33)))) for _ in range(12)]
        for password, ssid in vectors:
            with self.subTest(password=password, ssid=ssid):
                result, actual = self.derive(password, ssid)
                self.assertEqual(result, 0)
                self.assertEqual(actual, hashlib.pbkdf2_hmac("sha1", password, ssid, 4096, 32))

    def test_hmac_padding_and_key_boundaries(self):
        rng = random.Random(702)
        for key_len in [0, 1, 20, 63, 64, 65, 131]:
            key = bytes(rng.randrange(256) for _ in range(key_len))
            for size in [0, 1, 20, 55, 56, 63, 64, 65, 119, 120, 255, 1024]:
                data = bytes(rng.randrange(256) for _ in range(size))
                output = (ctypes.c_ubyte * 20)()
                with self.subTest(key_len=key_len, size=size):
                    self.assertEqual(self.lib.hs_crack_hmac_sha1(key, key_len, data, size, output), 0)
                    self.assertEqual(bytes(output), hmac.digest(key, data, "sha1"))

    def test_independent_concurrent_callers(self):
        vectors = [(b"parallel-password-%d" % i, b"synthetic-ssid-%d" % i) for i in range(8)]
        expected = [hashlib.pbkdf2_hmac("sha1", pw, ssid, 4096, 32) for pw, ssid in vectors]
        with ThreadPoolExecutor(max_workers=2) as executor:
            results = list(executor.map(lambda pair: self.derive(*pair), vectors))
        self.assertEqual(results, [(0, value) for value in expected])

    def test_cancellation_at_every_checkpoint(self):
        # Initial checkpoint + 128 batches for each of the two 4096-U blocks.
        for stop_at in range(1, 258):
            calls = [0]
            def check(_):
                calls[0] += 1
                return calls[0] != stop_at
            result, output = self.derive(b"password", b"IEEE", check)
            self.assertEqual((result, calls[0], output), (-1, stop_at, bytes(32)))
        calls = [0]
        def accept(_):
            calls[0] += 1
            return True
        result, output = self.derive(b"password", b"IEEE", accept)
        self.assertEqual((result, calls[0]), (0, 257))
        self.assertEqual(output, hashlib.pbkdf2_hmac("sha1", b"password", b"IEEE", 4096, 32))

    def test_invalid_inputs_clear_output(self):
        for password, ssid in [(b"short", b"ssid"), (b"x" * 64, b"ssid"),
                               (b"password", b"x" * 33), (None, b"ssid")]:
            self.assertEqual(self.derive(password, ssid), (-2, bytes(32)))
        output = (ctypes.c_ubyte * 32)(*([0xA5] * 32))
        self.assertEqual(self.lib.hs_crack_derive_pmk(b"password", None, 1, output, CHECKPOINT(), None), -2)
        self.assertEqual(bytes(output), bytes(32))
        self.assertEqual(self.lib.hs_crack_derive_pmk(b"password", b"ssid", 4, None, CHECKPOINT(), None), -2)
        for key, key_len, data, data_len in [(None, 1, b"", 0), (b"", 0, None, 1)]:
            output = (ctypes.c_ubyte * 20)(*([0xA5] * 20))
            self.assertEqual(self.lib.hs_crack_hmac_sha1(key, key_len, data, data_len, output), -2)
            self.assertEqual(bytes(output), bytes(20))


if __name__ == "__main__":
    unittest.main()
