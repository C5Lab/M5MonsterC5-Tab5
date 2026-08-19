# Host tests

Pure-logic pieces that carry no LVGL or ESP-IDF dependency are tested on the
host, so they can be verified without flashing a Tab5.

Run any of them with a host compiler. On a Windows workstation without one, the
same commands work inside WSL against the `/mnt/c/...` checkout.

## `cgw_parser_test.c`

Covers `main/screens/cgw_parser.c` — the JanOS GITM (`capture_gateway`) status
parser. The fixtures are the exact line shapes emitted by
`capture_gateway_print_status()` in the JanOS firmware, including the
human-readable `MY_LOG_INFO` lines that precede the `[CGW]` block and must be
ignored.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I main/screens -o /tmp/cgw_parser_test \
    tests/cgw_parser_test.c main/screens/cgw_parser.c
/tmp/cgw_parser_test
```

What it pins down:

- a complete block is published only on the exact `[CGW] END` terminator;
- SSIDs containing spaces survive the anchored `ssid=` / ` security=` /
  ` upstream_ssid=` split required by contract section 8.5;
- `drops=` is never harvested out of `rate_queue_drops=`;
- `file_bytes` keeps 64-bit precision;
- recorder loss and shaper loss are reported independently, and adaptive
  throttling on its own is not degradation;
- `[CGW]` appearing inside a log message is not parsed as protocol;
- duplicate `[CGW_CLIENT]` rows upsert by MAC instead of appending;
- `[PCAP_FINAL]` and the legacy `PCAP saved:` marker both yield a file path.

The contract itself lives in the JanOS repository at
`projectZero/ESP32C5/docs/janos-capture-gateway.md`, section 8.

## `boot_melody_osc_test.c`

Covers the two-term recurrence oscillator in `play_startup_beep()` (main.c),
which replaced a per-sample `sinf()` call so the startup melody can stream
instead of being rendered in full before the first sample reaches the codec.

```sh
gcc -std=c11 -Wall -Wextra -O2 -o /tmp/boot_melody_osc_test \
    tests/boot_melody_osc_test.c -lm
/tmp/boot_melody_osc_test
```

For every note in all three melodies it checks that the recurrence holds pitch
within one cent, that its amplitude does not drift over the longest note, and
that the exact `int16_t` conversion the firmware performs (including the 0.85
gain) never wraps.
