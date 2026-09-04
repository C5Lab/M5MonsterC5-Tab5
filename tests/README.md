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

Covers the shared tone renderer `audio_play_notes()` (main.c), which the startup
melody and the UI alert chime both go through:

- the two-term recurrence oscillator, which replaced a per-sample `sinf()` call
  so a tune can stream instead of being rendered in full before the first sample
  reaches the codec;
- the per-note envelope clamp, which lets an alert tone ask for a 140 ms decay
  on a 70 ms note without the attack and release ramps ever overlapping.

```sh
gcc -std=c11 -Wall -Wextra -O2 -o /tmp/boot_melody_osc_test \
    tests/boot_melody_osc_test.c -lm
/tmp/boot_melody_osc_test
```

For every note in all three melodies it checks that the recurrence holds pitch
within one cent, that its amplitude does not drift over the longest note, and
that the exact `int16_t` conversion the firmware performs (including the 0.85
gain) never wraps.

For the envelope it checks, on every note length the melodies use and on every
note of the four alert tones (join, win, warn, alarm), that the ramps never
overlap, that the release hands over at exactly 1.0, that each note starts and
ends in silence, and that no boot melody note is short enough to be clamped -
i.e. sharing the renderer with the alerts left the startup melody sounding
exactly as it did.

## `pcap_summary_reducers_test.c`

Covers `components/pcap_summary/pcap_summary_reducers.c` - the key normalizers
and the aggregation primitives the PCAP summary is built from. This is the unit
test list of `docs/PCAP_Analysis_and_Implementation_Plan.md` section 22.9:
empty, single element, ties, limits, overflow and deterministic order.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Icomponents/pcap_summary/include -o /tmp/reducers_test \
    tests/pcap_summary_reducers_test.c \
    components/pcap_summary/pcap_summary_reducers.c
/tmp/reducers_test
```

What it pins down:

- a key that does not fit its buffer is refused, never clipped, because a
  clipped key merges two different observations into one row;
- `A -> B` and `B -> A` produce the same host-pair key, with the port stripped
  and MAC case folded;
- a domain loses its root dot and its case, so `WWW.Example.COM.` and
  `www.example.com` are one key;
- ties sort lexicographically, so the table never depends on insertion order;
- an evicting table raises `approximate`, and counters saturate instead of
  wrapping;
- a ratio carries its denominator: zero samples render as `n/a`, a thin sample
  is labelled `low sample`, and `0/100` stays a real zero;
- window buckets hold both edges of the capture span, out-of-span samples are
  counted apart, and the burst score is 1.0 for a flat capture;
- the distinct-key sketch counts a returning key once and admits when its load
  makes the count approximate.

## `pcap_summary_report_test.c`

End-to-end over the analysis stack: it synthesizes PCAP files on disk, runs
`pcap_reader` -> `pcap_summary` -> `pcap_summary_render_report()` and compares
the result with a baseline embedded in the test.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Icomponents/pcap_summary/include -Icomponents/pcap_reader/include \
    -o /tmp/report_test tests/pcap_summary_report_test.c \
    components/pcap_summary/pcap_summary.c \
    components/pcap_summary/pcap_summary_reducers.c \
    components/pcap_summary/pcap_summary_report.c \
    components/pcap_reader/pcap_reader.c
/tmp/report_test            # fixtures go to /tmp, or to the directory in argv[1]
```

The fixtures are the acceptance list of section 22.9: a reference capture whose
whole report is pinned byte for byte, an empty capture, a 10-byte file that must
be refused, a capture whose last record is cut, records with broken protocol
data and a lying `caplen`, an NXDOMAIN burst, one-pair dominance and a
high-cardinality capture. It also renders the same file twice and requires
identical bytes, and renders into a 400-byte buffer to check that a clipped
report says `[report truncated]`.

When a deliberate wording change breaks the baseline, the test prints the actual
report between `----8<----` markers so the new text can be reviewed and pasted
into `reference_report[]`.
