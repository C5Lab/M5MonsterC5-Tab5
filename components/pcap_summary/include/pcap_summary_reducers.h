#pragma once

/* Portable aggregation primitives for the PCAP summary layer.
 *
 * This file is the reducer/normalization contract described in the Zeek-derived
 * summary plan (docs/PCAP_Analysis_and_Implementation_Plan.md section 22.6). It
 * carries no LVGL, ESP-IDF or file dependency so the same code runs on the host
 * under the tests in tests/pcap_summary_reducers_test.c.
 *
 * Two rules hold for everything below:
 *
 *   - determinism: identical input in identical order produces identical
 *     output, including tie ordering, so a text baseline can be compared byte
 *     for byte;
 *   - no hidden loss: a table that had to evict, or a sketch that grew past its
 *     reliable load, raises an explicit `approximate` flag instead of quietly
 *     returning a smaller number.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_KEY_TEXT_MAX        96U
#define PCAP_WINDOW_BUCKETS      64U
#define PCAP_UNIQUE_SKETCH_WORDS 128U   /* 4096 bits */

/* ---------------------------------------------------------------- keys ---
 *
 * Every normalizer writes a canonical key into `out` and returns true, or
 * leaves `out` as an empty string and returns false when the input carries no
 * usable key. "No usable key" also covers a key that does not fit `out_size`:
 * truncating would merge two distinct observations into one, so the caller is
 * told instead and can keep the raw value.
 */

/* Lowercases, trims blanks and control characters, drops the trailing root dot.
 * A leading "*." is kept because a wildcard name is a different observation
 * than the name it covers. */
bool pcap_key_domain(const char *input, char *out, size_t out_size);

/* Domain rules plus the shapes an SNI or HTTP Host header adds: a trailing
 * ":port", and an IPv6 literal wrapped in brackets. */
bool pcap_key_sni(const char *input, char *out, size_t out_size);

/* Canonical host key for one endpoint. Accepts "10.0.0.1", "10.0.0.1:53",
 * "[fe80::1]:443", a bare IPv6 literal and a MAC address; hex is lowercased and
 * a port suffix is removed. */
bool pcap_key_host(const char *endpoint, char *out, size_t out_size);

/* Unordered pair key, so A->B and B->A fold into one row. Both sides are
 * normalized with pcap_key_host() and emitted as "<lower> <-> <higher>". */
bool pcap_key_host_pair(const char *first, const char *second,
                        char *out, size_t out_size);

/* Service key from a port and IP protocol number: "https/tcp", "dns/udp", or
 * "8123/tcp" when the port has no well-known name. */
bool pcap_key_service(uint16_t port, uint8_t ip_protocol, char *out, size_t out_size);

/* Basename of a file reference: directories, query string and fragment are
 * removed, control characters reject the key, and "." / ".." are refused so a
 * traversal artefact never becomes a counter label. */
bool pcap_key_filename(const char *input, char *out, size_t out_size);

/* Lowercased hex digest, accepted only at MD5, SHA-1 or SHA-256 length. */
bool pcap_key_hash(const char *input, char *out, size_t out_size);

/* --------------------------------------------------------------- top-k ---
 *
 * Bounded counters with Space-Saving style eviction: once the table is full the
 * weakest row is replaced and inherits its predecessor's count, which keeps a
 * genuinely dominant key from being pushed out by a stream of one-off keys. The
 * inherited count is an upper bound, hence the `approximate` flag.
 */

typedef struct {
    char label[PCAP_KEY_TEXT_MAX];
    uint64_t count;
    uint64_t bytes;
} pcap_summary_text_counter_t;

typedef struct {
    uint16_t port;
    uint8_t ip_protocol;
    uint64_t count;
} pcap_summary_port_counter_t;

typedef enum {
    PCAP_TOPK_EXISTING = 0,   /* label was already tracked */
    PCAP_TOPK_INSERTED,       /* label took a free slot */
    PCAP_TOPK_REPLACED,       /* label evicted the weakest row */
} pcap_topk_result_t;

/* Saturating, so a pathological capture cannot wrap a counter back to zero. */
uint64_t pcap_sat_add_u64(uint64_t value, uint64_t increment);

pcap_topk_result_t pcap_topk_add_text(pcap_summary_text_counter_t *entries,
                                      size_t capacity, uint16_t *used,
                                      const char *label, uint64_t bytes,
                                      bool *approximate);

void pcap_topk_add_port(pcap_summary_port_counter_t *entries, size_t capacity,
                        uint16_t *used, uint16_t port, uint8_t ip_protocol,
                        bool *approximate);

/* Descending by count. Ties break on the label (text) or on protocol then port
 * (ports), so the order never depends on insertion order or on qsort internals. */
void pcap_topk_sort_text(pcap_summary_text_counter_t *entries, size_t count);
void pcap_topk_sort_port(pcap_summary_port_counter_t *entries, size_t count);

/* --------------------------------------------------------------- ratio ---
 *
 * A ratio without its denominator is not evidence, so the denominator travels
 * with the value and an empty or thin sample is labelled rather than rendered
 * as a confident 0%.
 */

typedef struct {
    uint64_t numerator;
    uint64_t denominator;
    double value;        /* 0.0 when !valid */
    bool valid;          /* denominator > 0 */
    bool low_sample;     /* valid, but denominator < min_samples */
} pcap_ratio_t;

pcap_ratio_t pcap_reduce_ratio(uint64_t numerator, uint64_t denominator,
                               uint64_t min_samples);

/* "12.5% (5/40)", "12.5% (5/40, low sample)" or "n/a (no samples)".
 * Returns the number of characters written, excluding the terminator. */
size_t pcap_ratio_format(const pcap_ratio_t *ratio, char *out, size_t out_size);

/* --------------------------------------------------------- time window ---
 *
 * Fixed bucket count over the capture span, so memory does not depend on the
 * capture length and a burst is measured against the same yardstick in every
 * report.
 */

typedef struct {
    uint64_t start_us;
    uint64_t bucket_us;                     /* never 0 */
    uint64_t total;
    uint64_t out_of_range;                  /* samples outside the configured span */
    uint32_t counts[PCAP_WINDOW_BUCKETS];
    uint16_t bucket_count;                  /* 1..PCAP_WINDOW_BUCKETS */
    bool configured;
} pcap_window_t;

/* end_us <= start_us collapses to a single bucket, which is what a capture with
 * one packet or with a frozen clock deserves. */
void pcap_window_init(pcap_window_t *window, uint64_t start_us, uint64_t end_us);
void pcap_window_add(pcap_window_t *window, uint64_t timestamp_us);

/* Highest bucket count; *index_out receives its bucket when not NULL. */
uint32_t pcap_window_peak(const pcap_window_t *window, uint16_t *index_out);

/* Peak divided by the mean over the configured span: 1.0 is a flat capture,
 * bucket_count is everything inside one bucket. 0.0 when nothing was added. */
double pcap_window_burst_score(const pcap_window_t *window);

/* Bucket width in seconds, for reports that name the window they measured. */
double pcap_window_bucket_seconds(const pcap_window_t *window);

/* -------------------------------------------------------------- unique ---
 *
 * A fixed-capacity table cannot hold an exact distinct set, and re-counting a
 * key that was evicted earlier inflates the number without bound. A bitmap
 * sketch keeps the count monotonic and bounded in memory; collisions can only
 * make it undercount, and the flag says when that becomes likely.
 */

typedef struct {
    uint32_t bits[PCAP_UNIQUE_SKETCH_WORDS];
    uint64_t distinct;
    uint64_t observations;
    bool approximate;
} pcap_unique_t;

void pcap_unique_reset(pcap_unique_t *unique);

/* Returns true when the key had not been seen before. */
bool pcap_unique_add(pcap_unique_t *unique, const char *key);

#ifdef __cplusplus
}
#endif
