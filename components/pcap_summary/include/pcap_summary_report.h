#pragma once

/* Deterministic English summary of a capture, in the medium-detail shape from
 * docs/PCAP_Analysis_and_Implementation_Plan.md section 22.7: Traffic,
 * Protocols, Anomalies and IOC.
 *
 * The renderer is a pure function of the three input structures. It reads no
 * clock, no file and no global state, so the same capture always produces the
 * same bytes - which is what makes the host baseline test in
 * tests/pcap_summary_report_test.c meaningful.
 *
 * It reports only what the decoders actually observed. An indicator is labelled
 * "observed", never "malicious": nothing here consults an external threat feed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_reader.h"
#include "pcap_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_SUMMARY_REPORT_VERSION 1U

/* Comfortable buffer for a full report; the renderer never writes past
 * out_size regardless. */
#define PCAP_SUMMARY_REPORT_SUGGESTED_SIZE 6144U

/* Writes the report into `out` and returns the number of characters written,
 * excluding the terminator. The buffer is always NUL-terminated. When the text
 * does not fit, the output ends with a "[report truncated]" line so a clipped
 * report can never be mistaken for a complete one. */
size_t pcap_summary_render_report(const pcap_capture_info_t *capture_info,
                                  const pcap_scan_summary_t *scan_summary,
                                  const pcap_summary_t *summary,
                                  char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
