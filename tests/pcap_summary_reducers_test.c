// Host test for the summary reducers: key normalization, bounded top-k, ratio,
// time window and the distinct-key sketch.
//
// Covers the unit-test list of docs/PCAP_Analysis_and_Implementation_Plan.md
// section 22.9: empty, single element, ties, limits, overflow and deterministic
// order.
#include <stdio.h>
#include <string.h>

#include "pcap_summary_reducers.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static void check_key(bool ok, const char *got, const char *expected, int line)
{
    if (!ok || strcmp(got, expected) != 0) {
        printf("FAIL %s:%d: got %s'%s', expected '%s'\n", __FILE__, line,
               ok ? "" : "(rejected) ", got, expected);
        failures++;
    }
}

#define CHECK_KEY(call, buffer, expected) do { \
    bool ok_ = (call); check_key(ok_, (buffer), (expected), __LINE__); \
} while (0)

#define CHECK_REJECTED(call, buffer) do { \
    bool ok_ = (call); \
    if (ok_ || (buffer)[0] != '\0') { \
        printf("FAIL %s:%d: expected rejection, got '%s'\n", __FILE__, __LINE__, (buffer)); \
        failures++; \
    } \
} while (0)

static void test_keys(void)
{
    char key[PCAP_KEY_TEXT_MAX];

    // --- domains: case, the root dot and blanks all fold into one key ---
    CHECK_KEY(pcap_key_domain("WWW.Example.COM.", key, sizeof(key)), key, "www.example.com");
    CHECK_KEY(pcap_key_domain("  api.test.local  ", key, sizeof(key)), key, "api.test.local");
    CHECK_KEY(pcap_key_domain("*.CDN.example", key, sizeof(key)), key, "*.cdn.example");
    CHECK_REJECTED(pcap_key_domain("", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_domain(".", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_domain("bad\x01name", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_domain(NULL, key, sizeof(key)), key);
    // A key that does not fit is refused rather than clipped, because a clipped
    // key would merge two different names into one row.
    char tiny[8];
    CHECK_REJECTED(pcap_key_domain("example.com", tiny, sizeof(tiny)), tiny);

    // --- SNI adds the port and bracket shapes ---
    CHECK_KEY(pcap_key_sni("Example.com:443", key, sizeof(key)), key, "example.com");
    CHECK_KEY(pcap_key_sni("[2001:DB8::1]:443", key, sizeof(key)), key, "2001:db8::1");
    CHECK_KEY(pcap_key_sni("host.local", key, sizeof(key)), key, "host.local");

    // --- hosts: ports off, MAC case folded, bare IPv6 left intact ---
    CHECK_KEY(pcap_key_host("192.168.1.10", key, sizeof(key)), key, "192.168.1.10");
    CHECK_KEY(pcap_key_host("192.168.1.10:53", key, sizeof(key)), key, "192.168.1.10");
    CHECK_KEY(pcap_key_host("AA:BB:CC:DD:EE:FF", key, sizeof(key)), key, "aa:bb:cc:dd:ee:ff");
    CHECK_KEY(pcap_key_host("aa-bb-cc-dd-ee-ff", key, sizeof(key)), key, "aa:bb:cc:dd:ee:ff");
    CHECK_KEY(pcap_key_host("FE80::1", key, sizeof(key)), key, "fe80::1");
    CHECK_KEY(pcap_key_host("[fe80::1]:8080", key, sizeof(key)), key, "fe80::1");
    CHECK_REJECTED(pcap_key_host("   ", key, sizeof(key)), key);

    // --- pairs fold both directions into one row ---
    char forward[PCAP_KEY_TEXT_MAX];
    char reverse[PCAP_KEY_TEXT_MAX];
    CHECK(pcap_key_host_pair("192.168.1.10", "10.0.0.1", forward, sizeof(forward)),
          "pair rejected");
    CHECK(pcap_key_host_pair("10.0.0.1", "192.168.1.10:443", reverse, sizeof(reverse)),
          "reverse pair rejected");
    CHECK(strcmp(forward, reverse) == 0, "'%s' != '%s'", forward, reverse);
    CHECK(strcmp(forward, "10.0.0.1 <-> 192.168.1.10") == 0, "pair='%s'", forward);
    // Two long hosts cannot be joined without clipping, so the pair is refused
    // and the caller can flag the table instead of storing a merged key.
    char long_a[70];
    char long_b[70];
    memset(long_a, 'a', sizeof(long_a) - 1U); long_a[sizeof(long_a) - 1U] = '\0';
    memset(long_b, 'b', sizeof(long_b) - 1U); long_b[sizeof(long_b) - 1U] = '\0';
    CHECK_REJECTED(pcap_key_host_pair(long_a, long_b, key, sizeof(key)), key);

    // --- services ---
    CHECK_KEY(pcap_key_service(443U, 6U, key, sizeof(key)), key, "https/tcp");
    CHECK_KEY(pcap_key_service(53U, 17U, key, sizeof(key)), key, "dns/udp");
    CHECK_KEY(pcap_key_service(8123U, 6U, key, sizeof(key)), key, "8123/tcp");
    CHECK_KEY(pcap_key_service(1234U, 99U, key, sizeof(key)), key, "1234/ip99");
    CHECK_REJECTED(pcap_key_service(0U, 6U, key, sizeof(key)), key);

    // --- file names: no directories, no query string, no traversal ---
    CHECK_KEY(pcap_key_filename("/downloads/Setup.EXE?v=2", key, sizeof(key)),
              key, "Setup.EXE");
    CHECK_KEY(pcap_key_filename("C:\\temp\\report.pdf", key, sizeof(key)), key, "report.pdf");
    CHECK_KEY(pcap_key_filename("archive.tar.gz#part", key, sizeof(key)), key, "archive.tar.gz");
    CHECK_REJECTED(pcap_key_filename("../..", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_filename("/tmp/", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_filename("bad\tname", key, sizeof(key)), key);

    // --- hashes only at digest lengths ---
    CHECK_KEY(pcap_key_hash("D41D8CD98F00B204E9800998ECF8427E", key, sizeof(key)),
              key, "d41d8cd98f00b204e9800998ecf8427e");
    CHECK_REJECTED(pcap_key_hash("d41d8cd98f00b204e9800998ecf8427", key, sizeof(key)), key);
    CHECK_REJECTED(pcap_key_hash("zzzd8cd98f00b204e9800998ecf8427e", key, sizeof(key)), key);
}

static void test_topk(void)
{
    pcap_summary_text_counter_t table[4];
    uint16_t used = 0;
    bool approximate = false;
    memset(table, 0, sizeof(table));

    // --- empty and single element ---
    CHECK(used == 0, "fresh table is not empty");
    pcap_topk_sort_text(table, used);   // must tolerate an empty table
    CHECK(pcap_topk_add_text(table, 4U, &used, "alpha", 100U, &approximate) ==
          PCAP_TOPK_INSERTED, "first insert");
    CHECK(used == 1 && table[0].count == 1 && table[0].bytes == 100U, "single element");
    CHECK(pcap_topk_add_text(table, 4U, &used, "alpha", 40U, &approximate) ==
          PCAP_TOPK_EXISTING, "repeat insert");
    CHECK(table[0].count == 2 && table[0].bytes == 140U, "repeat did not accumulate");
    CHECK(!approximate, "approximate raised too early");

    // A NULL or empty label is ignored rather than stored as a blank row.
    CHECK(pcap_topk_add_text(table, 4U, &used, "", 1U, &approximate) == PCAP_TOPK_EXISTING,
          "empty label accepted");
    CHECK(pcap_topk_add_text(table, 4U, &used, NULL, 1U, &approximate) == PCAP_TOPK_EXISTING,
          "NULL label accepted");
    CHECK(used == 1, "used=%u after ignored labels", used);

    // --- ties sort lexicographically, so the order never depends on qsort ---
    memset(table, 0, sizeof(table));
    used = 0;
    approximate = false;
    const char *tied[] = {"delta", "bravo", "charlie"};
    for (unsigned i = 0; i < 3; i++) {
        pcap_topk_add_text(table, 4U, &used, tied[i], 10U, &approximate);
    }
    pcap_topk_sort_text(table, used);
    CHECK(strcmp(table[0].label, "bravo") == 0 && strcmp(table[1].label, "charlie") == 0 &&
          strcmp(table[2].label, "delta") == 0, "tie order: %s,%s,%s",
          table[0].label, table[1].label, table[2].label);

    // --- limits: eviction keeps the dominant key and raises the flag ---
    memset(table, 0, sizeof(table));
    used = 0;
    approximate = false;
    for (unsigned i = 0; i < 20U; i++) {
        pcap_topk_add_text(table, 4U, &used, "heavy", 1U, &approximate);
    }
    CHECK(!approximate, "flag raised before the table filled");
    const char *strangers[] = {"one", "two", "three", "four", "five"};
    for (unsigned i = 0; i < 5U; i++) {
        pcap_topk_add_text(table, 4U, &used, strangers[i], 1U, &approximate);
    }
    CHECK(used == 4, "used=%u", used);
    CHECK(approximate, "eviction did not raise the approximate flag");
    pcap_topk_sort_text(table, used);
    CHECK(strcmp(table[0].label, "heavy") == 0 && table[0].count == 20U,
          "dominant key lost: %s=%llu", table[0].label,
          (unsigned long long)table[0].count);
    // Space-Saving: a newcomer inherits the evicted count, so its count is an
    // upper bound rather than its true number of observations.
    CHECK(table[1].count >= 1U, "inherited count missing");

    // --- overflow saturates instead of wrapping ---
    CHECK(pcap_sat_add_u64(UINT64_MAX - 1U, 1U) == UINT64_MAX, "saturating add off by one");
    CHECK(pcap_sat_add_u64(UINT64_MAX, 5U) == UINT64_MAX, "saturating add wrapped");
    memset(table, 0, sizeof(table));
    used = 0;
    pcap_topk_add_text(table, 4U, &used, "big", 10U, NULL);
    table[0].count = UINT64_MAX;
    table[0].bytes = UINT64_MAX;
    pcap_topk_add_text(table, 4U, &used, "big", 10U, NULL);
    CHECK(table[0].count == UINT64_MAX && table[0].bytes == UINT64_MAX,
          "counter wrapped: %llu", (unsigned long long)table[0].count);

    // --- deterministic order: same input, same table, byte for byte ---
    pcap_summary_text_counter_t first[4];
    pcap_summary_text_counter_t second[4];
    const char *stream[] = {"e", "b", "e", "a", "c", "b", "e", "d", "a", "f"};
    for (unsigned run = 0; run < 2U; run++) {
        pcap_summary_text_counter_t *target = run == 0 ? first : second;
        uint16_t count = 0;
        bool flag = false;
        memset(target, 0, sizeof(first));
        for (unsigned i = 0; i < sizeof(stream) / sizeof(stream[0]); i++) {
            pcap_topk_add_text(target, 4U, &count, stream[i], i, &flag);
        }
        pcap_topk_sort_text(target, count);
    }
    CHECK(memcmp(first, second, sizeof(first)) == 0, "table is not reproducible");

    // --- ports: same rules, tie broken by protocol then port ---
    pcap_summary_port_counter_t ports[3];
    uint16_t port_used = 0;
    bool port_flag = false;
    memset(ports, 0, sizeof(ports));
    pcap_topk_add_port(ports, 3U, &port_used, 443U, 6U, &port_flag);
    pcap_topk_add_port(ports, 3U, &port_used, 53U, 17U, &port_flag);
    pcap_topk_add_port(ports, 3U, &port_used, 80U, 6U, &port_flag);
    pcap_topk_add_port(ports, 3U, &port_used, 0U, 6U, &port_flag);       // ignored
    CHECK(port_used == 3, "port_used=%u", port_used);
    pcap_topk_sort_port(ports, port_used);
    CHECK(ports[0].port == 80U && ports[1].port == 443U && ports[2].port == 53U,
          "port tie order: %u,%u,%u", ports[0].port, ports[1].port, ports[2].port);
    pcap_topk_add_port(ports, 3U, &port_used, 8080U, 6U, &port_flag);
    CHECK(port_flag, "port eviction did not raise the flag");
}

static void test_ratio(void)
{
    char text[64];

    pcap_ratio_t empty = pcap_reduce_ratio(0U, 0U, 20U);
    CHECK(!empty.valid && empty.value == 0.0, "empty ratio is not marked invalid");
    pcap_ratio_format(&empty, text, sizeof(text));
    CHECK(strcmp(text, "n/a (no samples)") == 0, "empty text='%s'", text);

    pcap_ratio_t thin = pcap_reduce_ratio(1U, 2U, 20U);
    CHECK(thin.valid && thin.low_sample, "thin sample not flagged");
    pcap_ratio_format(&thin, text, sizeof(text));
    CHECK(strcmp(text, "50.0% (1/2, low sample)") == 0, "thin text='%s'", text);

    pcap_ratio_t solid = pcap_reduce_ratio(5U, 40U, 20U);
    CHECK(solid.valid && !solid.low_sample && solid.value == 0.125, "solid=%f", solid.value);
    pcap_ratio_format(&solid, text, sizeof(text));
    CHECK(strcmp(text, "12.5% (5/40)") == 0, "solid text='%s'", text);

    // Zero numerator over a real denominator is a fact, not missing data.
    pcap_ratio_t zero = pcap_reduce_ratio(0U, 100U, 20U);
    CHECK(zero.valid && zero.value == 0.0, "zero over 100 is not valid");
    pcap_ratio_format(&zero, text, sizeof(text));
    CHECK(strcmp(text, "0.0% (0/100)") == 0, "zero text='%s'", text);

    // A short buffer truncates the text but still terminates it.
    char small[8];
    size_t written = pcap_ratio_format(&solid, small, sizeof(small));
    CHECK(written == sizeof(small) - 1U && small[sizeof(small) - 1U] == '\0',
          "short buffer written=%zu", written);
}

static void test_window(void)
{
    pcap_window_t window;

    // --- empty window ---
    pcap_window_init(&window, 0U, 0U);
    CHECK(window.configured && window.bucket_count == 1U && window.bucket_us == 1U,
          "zero span: buckets=%u width=%llu", window.bucket_count,
          (unsigned long long)window.bucket_us);
    CHECK(pcap_window_peak(&window, NULL) == 0 && pcap_window_burst_score(&window) == 0.0,
          "empty window is not empty");

    // --- single sample ---
    pcap_window_add(&window, 0U);
    CHECK(window.total == 1U && pcap_window_peak(&window, NULL) == 1U, "single sample lost");
    CHECK(pcap_window_burst_score(&window) == 1.0, "single sample burst=%f",
          pcap_window_burst_score(&window));

    // --- an even spread scores 1.0, both edges land inside ---
    pcap_window_init(&window, 1000U, 1000U + 64U * 1000U);
    for (unsigned i = 0; i < 64U; i++) {
        pcap_window_add(&window, 1000U + (uint64_t)i * 1000U);
    }
    pcap_window_add(&window, 1000U + 64U * 1000U);            // the very last microsecond
    CHECK(window.out_of_range == 0U, "out_of_range=%llu",
          (unsigned long long)window.out_of_range);
    CHECK(window.total == 65U, "total=%llu", (unsigned long long)window.total);
    CHECK(pcap_window_burst_score(&window) < 2.5, "even spread scored %f",
          pcap_window_burst_score(&window));

    // --- everything in one bucket scores near the bucket count ---
    pcap_window_init(&window, 0U, 64000000U);
    for (unsigned i = 0; i < 100U; i++) {
        pcap_window_add(&window, 5U);
    }
    uint16_t index = 0xFFFFU;
    CHECK(pcap_window_peak(&window, &index) == 100U && index == 0U, "peak index=%u", index);
    CHECK(pcap_window_burst_score(&window) > 60.0, "burst=%f",
          pcap_window_burst_score(&window));
    CHECK(pcap_window_bucket_seconds(&window) > 0.9 &&
          pcap_window_bucket_seconds(&window) < 1.1,
          "bucket seconds=%f", pcap_window_bucket_seconds(&window));

    // --- samples outside the span are counted apart, never folded into an edge ---
    pcap_window_init(&window, 10000U, 20000U);
    pcap_window_add(&window, 9999U);
    pcap_window_add(&window, 999999U);
    CHECK(window.total == 0U && window.out_of_range == 2U, "out of range: total=%llu oor=%llu",
          (unsigned long long)window.total, (unsigned long long)window.out_of_range);

    // --- an unconfigured window ignores samples instead of writing past its array ---
    pcap_window_t blank;
    memset(&blank, 0, sizeof(blank));
    pcap_window_add(&blank, 1234U);
    CHECK(blank.total == 0U && blank.out_of_range == 0U, "unconfigured window accepted a sample");
}

static void test_unique(void)
{
    pcap_unique_t unique;
    pcap_unique_reset(&unique);

    CHECK(unique.distinct == 0U, "fresh sketch is not empty");
    CHECK(pcap_unique_add(&unique, "a.example") , "first key not fresh");
    CHECK(!pcap_unique_add(&unique, "a.example"), "repeat counted as fresh");
    CHECK(pcap_unique_add(&unique, "b.example"), "second key not fresh");
    CHECK(unique.distinct == 2U && unique.observations == 3U,
          "distinct=%llu observations=%llu", (unsigned long long)unique.distinct,
          (unsigned long long)unique.observations);
    CHECK(!unique.approximate, "small sketch already approximate");
    CHECK(!pcap_unique_add(&unique, ""), "empty key accepted");
    CHECK(!pcap_unique_add(&unique, NULL), "NULL key accepted");

    // A key that reappears long after a bounded table would have evicted it is
    // still counted once - the reason the sketch exists.
    for (unsigned i = 0; i < 500U; i++) {
        char key[32];
        snprintf(key, sizeof(key), "host-%u.example", i);
        pcap_unique_add(&unique, key);
    }
    uint64_t after_fill = unique.distinct;
    CHECK(!pcap_unique_add(&unique, "a.example"), "early key counted twice after fill");
    CHECK(unique.distinct == after_fill, "distinct moved on a repeat");
    CHECK(unique.approximate, "heavy load did not raise the approximate flag");
    // Collisions may only undercount; the sketch must never invent keys.
    CHECK(after_fill <= 502U, "distinct=%llu exceeds the number of keys fed",
          (unsigned long long)after_fill);
    CHECK(after_fill >= 480U, "distinct=%llu lost too many keys",
          (unsigned long long)after_fill);
}

int main(void)
{
    test_keys();
    test_topk();
    test_ratio();
    test_window();
    test_unique();

    if (failures == 0) {
        printf("all pcap_summary reducer tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
