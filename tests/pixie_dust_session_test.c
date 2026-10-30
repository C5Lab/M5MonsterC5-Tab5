#include <stdio.h>
#include <string.h>
#include "pixie_dust_session.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static pixie_session_t fresh(int tab, int port, bool modern)
{
    pixie_session_t s;
    CHECK(pixie_session_init(&s, tab, port, "02:11:22:33:44:55", modern));
    return s;
}

static pixie_command_t command(pixie_session_t *s, uint64_t now, const char *text,
                                int tab, int port, bool write_ok)
{
    pixie_outgoing_t out = {0};
    pixie_command_t kind = pixie_session_take_command(s, &out, now);
    CHECK(kind != PIXIE_CMD_NONE);
    CHECK(strcmp(out.text, text) == 0);
    CHECK(out.tab == tab && out.port == port);
    pixie_session_write_result(s, kind, write_ok, now);
    return kind;
}

static void line(pixie_session_t *s, const char *value, uint64_t now)
{
    pixie_session_feed(s, value, strlen(value), now);
    pixie_session_feed(s, "\r\n", 2, now);
}

static void accepted(pixie_session_t *s, uint64_t now)
{
    line(s, "{\"schema_version\":1,\"record_type\":\"accepted\",\"run_id\":7,\"operation\":\"wps_pixie\"}", now);
}

static void started(pixie_session_t *s)
{
    command(s, 10, "wps pixie 02:11:22:33:44:55", s->tab, s->port, true);
    accepted(s, 100);
    CHECK(s->accepted && s->run_id == 7 && s->phase == PIXIE_CAPTURE);
}

static void checked_stop(pixie_session_t *s, uint64_t now,
                         int tab, int port, bool stop_write_ok)
{
    command(s, now, "wps status", tab, port, true);
    line(s, "{\"record_type\":\"wps_status\",\"phase\":\"running\",\"run_id\":7}", now + 1);
    command(s, now + 2, "wps stop", tab, port, stop_write_ok);
}

static void test_exact_target_and_port_isolation(void)
{
    const int ports[] = {1, 1, 2};
    for (int tab = 0; tab < 3; tab++) {
        pixie_session_t s = fresh(tab, ports[tab], true);
        int other_visible_tab = (tab + 1) % 3;
        (void)other_visible_tab;
        command(&s, 10, "wps pixie 02:11:22:33:44:55", tab, ports[tab], true);
        accepted(&s, 100);
        pixie_session_request_exit(&s, 200);
        checked_stop(&s, 201, tab, ports[tab], true);
        CHECK(s.tab == tab && s.port == ports[tab]);
    }
}

static void test_esc_before_ack_and_late_ack(void)
{
    pixie_session_t s = fresh(0, 1, true);
    command(&s, 10, "wps pixie 02:11:22:33:44:55", 0, 1, true);
    pixie_session_request_exit(&s, 100);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.stop_queued && !s.close_ready);
    pixie_outgoing_t out;
    CHECK(pixie_session_take_command(&s, &out, 101) == PIXIE_CMD_NONE);
    accepted(&s, 200);
    CHECK(s.accepted && s.phase == PIXIE_STOPPING);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"schema_version\":1,\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 300);
    CHECK(s.stop_confirmed && s.close_ready);
}

static void test_escape_before_start_write_cancels_queued_command(void)
{
    pixie_session_t s = fresh(0, 1, true);
    pixie_session_request_exit(&s, 1);
    pixie_outgoing_t out = {0};
    CHECK(pixie_session_take_command(&s, &out, 2) == PIXIE_CMD_NONE);
    CHECK(s.close_ready);
}

static void test_missing_ack_requires_explicit_global_stop(void)
{
    pixie_session_t s = fresh(0, 1, true);
    command(&s, 0, "wps pixie 02:11:22:33:44:55", 0, 1, true);
    pixie_session_tick(&s, 4001);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.stop_queued);
    pixie_session_request_exit(&s, 4100);
    CHECK(!s.stop_queued && !s.close_ready);
    pixie_session_request_retry(&s, 4200);
    command(&s, 4201, "wps stop", 0, 1, true);
}

static void test_foreign_records_and_line_reassembly(void)
{
    pixie_session_t s = fresh(1, 1, true);
    command(&s, 0, "wps pixie 02:11:22:33:44:55", 1, 1, true);
    line(&s, "{\"record_type\":\"accepted\",\"run_id\":77,\"operation\":\"wps_verify\"}", 1);
    CHECK(!s.accepted);
    const char *first = "{\"schema_version\":1,\"record_type\":\"accepted\",";
    const char *second = "\"run_id\":7,\"operation\":\"wps_pixie\"}\n";
    pixie_session_feed(&s, first, strlen(first), 2);
    pixie_session_feed(&s, second, strlen(second), 2);
    CHECK(s.accepted && s.run_id == 7);
    line(&s, "{\"schema\":1,\"type\":\"wps_stage\",\"operation_id\":9,\"stage\":\"pixie_done\",\"found\":true}", 3);
    CHECK(s.phase == PIXIE_CAPTURE);
    line(&s, "{\"schema\":1,\"type\":\"wps_stage\",\"operation_id\":7,\"stage\":\"pixie_done\",\"found\":true}", 4);
    CHECK(s.phase == PIXIE_SEARCH && strcmp(s.stage, "Search hit") == 0);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":8,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 5);
    CHECK(!s.terminal_seen && !s.reveal_queued);
}

static void test_stop_pending_status_idle_then_stopped(void)
{
    pixie_session_t s = fresh(2, 2, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 2, 2, true);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stop_pending\",\"phase\":\"cleanup\"}", 300);
    CHECK(s.phase == PIXIE_STOPPING && s.stop_pending && !s.close_ready);
    pixie_session_tick(&s, 1299);
    pixie_outgoing_t out;
    CHECK(pixie_session_take_command(&s, &out, 1299) == PIXIE_CMD_NONE);
    pixie_session_tick(&s, 1300);
    command(&s, 1300, "wps status", 2, 2, true);
    line(&s, "{\"record_type\":\"wps_status\",\"phase\":\"idle\",\"run_id\":7}", 1400);
    command(&s, 1401, "wps stop", 2, 2, true);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 1500);
    CHECK(s.close_ready && s.stop_confirmed);
}

static void test_terminal_is_not_stop_barrier_and_reveal_is_scoped(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 250);
    CHECK(s.terminal_seen && !s.close_ready && s.phase == PIXIE_STOPPING);
    command(&s, 260, "secret reveal", 0, 1, true);
    line(&s, "SENSITIVE operation_id=8 recovered_pin=12345670 network_key=\"wrong\"", 270);
    CHECK(!s.has_secret);
    line(&s, "SENSITIVE operation_id=7 recovered_pin=12345670 network_key=\"correct key\"", 280);
    CHECK(s.has_secret && strcmp(s.pin, "12345670") == 0 && strcmp(s.network_key, "correct key") == 0);
    CHECK(strstr(s.line, "recovered_pin") == NULL);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 290);
    CHECK(s.close_ready);
}

static void test_stop_ack_before_reveal_keeps_session_for_secret(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 250);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 251);
    CHECK(s.stop_confirmed && !s.close_ready);
    command(&s, 252, "secret reveal", 0, 1, true);
    CHECK(!s.close_ready);
    line(&s, "SENSITIVE operation_id=7 recovered_pin=12345670 network_key=\"0123456789012345678901234567890123456789012345678901234567890123\"", 253);
    CHECK(s.has_secret && strlen(s.network_key) == 64 && s.close_ready);
}

static void test_failed_cleanup_cannot_be_overridden_by_stop_reply(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"failed\",\"lease_state\":\"retained\"}", 250);
    CHECK(s.phase == PIXIE_QUARANTINED && !s.reveal_queued);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 251);
    CHECK(!s.close_ready && s.phase == PIXIE_QUARANTINED && !s.reveal_queued);
}

static void test_late_sensitive_response_survives_reveal_retry_request(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 250);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 251);
    command(&s, 252, "secret reveal", 0, 1, true);
    pixie_session_tick(&s, 7253);
    CHECK(s.phase == PIXIE_ERROR && !s.close_ready);
    pixie_session_request_retry(&s, 7300);
    line(&s, "SENSITIVE operation_id=7 recovered_pin=12345670 network_key=\"late-key\"", 7301);
    CHECK(s.has_secret && strcmp(s.network_key, "late-key") == 0 && s.close_ready);
    pixie_outgoing_t out;
    CHECK(pixie_session_take_command(&s, &out, 7302) == PIXIE_CMD_NONE);
}

static void test_completed_run_checks_id_before_global_stop(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"no_match\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 200);
    pixie_session_request_exit(&s, 300);
    command(&s, 301, "wps status", 0, 1, true);
    line(&s, "{\"record_type\":\"wps_status\",\"phase\":\"running\",\"run_id\":8}", 302);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.stop_queued && !s.close_ready);
    pixie_outgoing_t out;
    CHECK(pixie_session_take_command(&s, &out, 303) == PIXIE_CMD_NONE);
}

static void test_partial_reveal_write_can_still_return_one_shot_secret(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 200);
    command(&s, 201, "secret reveal", 0, 1, false);
    CHECK(s.phase == PIXIE_ERROR);
    line(&s, "SENSITIVE operation_id=7 recovered_pin=12345670 network_key=\"partial-write-key\"", 202);
    CHECK(s.has_secret && strcmp(s.network_key, "partial-write-key") == 0);
}

static void test_explicit_empty_reveal_is_not_a_reply_timeout(void)
{
    pixie_session_t s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"terminal\",\"run_id\":7,\"operation_result\":\"pixie_pin_and_m7_verified\",\"cleanup_result\":\"restored\",\"lease_state\":\"released\"}", 250);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 251);
    command(&s, 252, "secret reveal", 0, 1, true);
    line(&s, "{\"schema_version\":1,\"record_type\":\"secret_reveal\",\"result\":\"empty\"}", 253);
    CHECK(!s.reveal_waiting && s.close_ready &&
          strcmp(s.result, "Secret unavailable (empty)") == 0);
}

static void test_timeouts_write_error_busy_quarantine_and_old_version(void)
{
    pixie_session_t s = fresh(0, 1, true);
    command(&s, 0, "wps pixie 02:11:22:33:44:55", 0, 1, false);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.start_refused);
    pixie_session_request_exit(&s, 1);
    CHECK(!s.stop_queued);

    s = fresh(0, 1, true);
    command(&s, 0, "wps pixie 02:11:22:33:44:55", 0, 1, true);
    line(&s, "[WFACTL1] {\"v\":1,\"type\":\"error\",\"command\":\"console\",\"code\":\"busy\",\"reason\":\"radio_lease\"}", 5);
    CHECK(s.start_refused && s.phase == PIXIE_ERROR);

    s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    pixie_session_tick(&s, 7204);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.close_ready &&
          strcmp(s.stage, "No stop reply") == 0);

    s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, false);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.close_ready);
    pixie_session_request_retry(&s, 300);
    command(&s, 301, "wps status", 0, 1, true);
    pixie_session_tick(&s, 7302);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.close_ready);
    pixie_session_request_retry(&s, 7400);
    command(&s, 7401, "wps status", 0, 1, true);
    line(&s, "{\"record_type\":\"wps_status\",\"phase\":\"quarantined\",\"run_id\":7}", 7500);
    CHECK(s.phase == PIXIE_QUARANTINED && !s.close_ready);

    s = fresh(0, 1, true);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 0, 1, true);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stop_pending\",\"phase\":\"cleanup\"}", 250);
    pixie_session_tick(&s, 18202);
    CHECK(s.phase == PIXIE_UNKNOWN && !s.close_ready);

    s = fresh(1, 1, false);
    started(&s);
    pixie_session_request_exit(&s, 200);
    checked_stop(&s, 201, 1, 1, true);
    line(&s, "{\"record_type\":\"wps_stop\",\"result\":\"stopped\",\"phase\":\"idle\"}", 300);
    CHECK(!s.stop_confirmed && !s.close_ready && s.phase == PIXIE_UNKNOWN);
}

int main(void)
{
    test_exact_target_and_port_isolation();
    test_esc_before_ack_and_late_ack();
    test_escape_before_start_write_cancels_queued_command();
    test_missing_ack_requires_explicit_global_stop();
    test_foreign_records_and_line_reassembly();
    test_stop_pending_status_idle_then_stopped();
    test_terminal_is_not_stop_barrier_and_reveal_is_scoped();
    test_stop_ack_before_reveal_keeps_session_for_secret();
    test_failed_cleanup_cannot_be_overridden_by_stop_reply();
    test_late_sensitive_response_survives_reveal_retry_request();
    test_completed_run_checks_id_before_global_stop();
    test_partial_reveal_write_can_still_return_one_shot_secret();
    test_explicit_empty_reveal_is_not_a_reply_timeout();
    test_timeouts_write_error_busy_quarantine_and_old_version();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
