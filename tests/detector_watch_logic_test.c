#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* MODEL */
/* LOGIC */

_Static_assert(sizeof(detector_watch_session_t) < 512,
               "detector watch session must not reserve the network list in internal BSS");

static void network(detector_watch_session_t *s, int index, const char *ssid,
                    const char *bssid, int channel)
{
    assert(detector_watch_add_network(s, index, ssid, bssid, channel));
}

static void test_details_ack_fallback_timeout_and_two_sessions(void)
{
    detector_watch_session_t grove = {0}, mbus = {0};
    detector_watch_network_t grove_networks[DETECTOR_WATCH_MAX] = {0};
    detector_watch_network_t mbus_networks[DETECTOR_WATCH_MAX] = {0};
    grove.networks = grove_networks;
    mbus.networks = mbus_networks;
    detector_watch_reset(&grove);
    detector_watch_reset(&mbus);
    char command[320];

    assert(detector_watch_build_v2_command(&grove, false, 0, false,
                                                command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector_v2"));
    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove, "[DETECTOR_INFO] CSA/ECSA announcements enabled.");
    assert(grove.phase == DETECTOR_WATCH_STARTING && !grove.details_confirmed);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    assert(grove.phase == DETECTOR_WATCH_STARTING && grove.start_ack_seen &&
           !grove.details_confirmed);
    detector_watch_status_line(&grove, "[DETECTOR_INFO] format=deauth_v2");
    assert(grove.phase == DETECTOR_WATCH_RUNNING && grove.details_confirmed &&
           grove.details_requested && !grove.legacy_fallback);

    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove, "[DETECTOR_INFO] format=deauth_v2");
    assert(grove.phase == DETECTOR_WATCH_STARTING && !grove.start_ack_seen);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    assert(grove.phase == DETECTOR_WATCH_RUNNING && grove.details_confirmed);

    detector_watch_begin_scan(&mbus, 4);
    assert(detector_watch_ack_scan(&mbus,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&mbus, 1, "Office", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&mbus, "Scan results printed."));
    assert(detector_watch_select(&mbus, 1, true));
    assert(detector_watch_build_v2_command(&mbus, true, 4, false,
                                                command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector_v2 --only 1"));
    detector_watch_begin_start_details(&mbus, true, true);
    detector_watch_status_line(&mbus, "Unrecognized command");
    assert(mbus.legacy_retry_pending && mbus.phase == DETECTOR_WATCH_STARTING &&
           !mbus.remote_state_unknown && !mbus.running);
    assert(detector_watch_build_command(&mbus, true, 4, command,
                                        sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 1"));
    assert(grove.phase == DETECTOR_WATCH_RUNNING && grove.details_confirmed &&
           !grove.legacy_retry_pending);

    detector_watch_begin_start_details(&mbus, true, false);
    assert(mbus.legacy_fallback && !mbus.legacy_retry_pending);
    detector_watch_status_line(&mbus,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    assert(mbus.phase == DETECTOR_WATCH_STARTING && !mbus.details_requested &&
           !mbus.details_confirmed);
    detector_watch_status_line(&mbus,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Office");
    assert(mbus.phase == DETECTOR_WATCH_RUNNING && mbus.watch_verified &&
           detector_watch_accept_event(&mbus,
                                                                "00:11:22:33:44:55"));

    detector_watch_begin_start_details(&mbus, true, true);
    detector_watch_timeout(&mbus);
    assert(mbus.phase == DETECTOR_WATCH_ERROR && mbus.remote_state_unknown &&
           !mbus.legacy_retry_pending);

    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    detector_watch_timeout(&grove);
    assert(grove.phase == DETECTOR_WATCH_ERROR && grove.remote_state_unknown &&
           !grove.legacy_retry_pending && !grove.details_confirmed);
    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    detector_watch_status_line(&grove, "[DETECTOR_INFO] format=deauth_v3");
    assert(grove.phase == DETECTOR_WATCH_ERROR && grove.remote_state_unknown &&
           !grove.legacy_retry_pending && !grove.details_confirmed);
    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove, "[DETECTOR_INFO] format=deauth_v2extra");
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.details_confirmed);

    detector_watch_begin_start_details(&grove, false, true);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    detector_watch_status_line(&grove, "Unrecognized command");
    assert(grove.phase == DETECTOR_WATCH_ERROR && grove.remote_state_unknown &&
           !grove.legacy_retry_pending);

    detector_watch_begin_start_details(&mbus, true, true);
    detector_watch_status_line(&mbus,
        "Invalid detector option; use --only before scan indices.");
    assert(mbus.phase == DETECTOR_WATCH_ERROR && !mbus.legacy_retry_pending);

    detector_watch_begin_start_details(&mbus, true, false);
    detector_watch_status_line(&mbus, "Unrecognized command");
    assert(mbus.phase == DETECTOR_WATCH_ERROR && !mbus.legacy_retry_pending);
}

static void test_v2_command_requires_fresh_completed_scan_and_preserves_all_indices(void)
{
    detector_watch_session_t session = {0};
    detector_watch_network_t networks[DETECTOR_WATCH_MAX] = {0};
    session.networks = networks;
    detector_watch_reset(&session);
    char command[320];

    assert(!detector_watch_build_v2_command(&session, true, 1, false,
                                                 command, sizeof(command)));
    detector_watch_begin_scan(&session, 1);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Office", "00:11:22:33:44:55", 6);
    network(&session, 2, "Office", "00:11:22:33:44:66", 11);
    assert(!detector_watch_build_v2_command(&session, true, 1, false,
                                                 command, sizeof(command)));
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));
    assert(detector_watch_select(&session, 2, true));
    assert(detector_watch_build_v2_command(&session, true, 1, false,
                                                command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector_v2 --only 1 2"));
    assert(!detector_watch_build_v2_command(&session, true, 2, false,
                                                 command, sizeof(command)));
    detector_watch_begin_scan(&session, 2);
    assert(!detector_watch_build_v2_command(&session, true, 2, false,
                                                 command, sizeof(command)));
}

static void test_selected_v2_watch_lines_before_format_confirmation(void)
{
    detector_watch_session_t session = {0};
    detector_watch_network_t networks[DETECTOR_WATCH_MAX] = {0};
    session.networks = networks;
    detector_watch_reset(&session);
    detector_watch_begin_scan(&session, 9);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Lab (east)", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));
    detector_watch_begin_start_details(&session, true, true);
    detector_watch_status_line(&session,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    assert(session.phase == DETECTOR_WATCH_STARTING && session.start_ack_seen);
    detector_watch_status_line(&session,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Lab (east)");
    assert(session.phase == DETECTOR_WATCH_STARTING && session.watched_count == 1);
    detector_watch_status_line(&session, "[DETECTOR_INFO] format=deauth_v2");
    assert(session.phase == DETECTOR_WATCH_RUNNING && session.details_confirmed &&
           session.watch_verified && session.watched_count == 1);
    assert(detector_watch_accept_event(&session, "00:11:22:33:44:55"));
    assert(!detector_watch_accept_event(&session, "AA:BB:CC:DD:EE:FF"));

    detector_watch_begin_start_details(&session, true, true);
    detector_watch_status_line(&session,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&session, "[DETECTOR_INFO] format=deauth_v2");
    assert(session.phase == DETECTOR_WATCH_STARTING &&
           session.start_ack_seen && session.details_confirmed &&
           !session.watch_verified && !session.running);
    int64_t watch_deadline_started_us = 0;
    assert(detector_watch_start_watch_deadline(
        &session, 4900000, &watch_deadline_started_us));
    assert(watch_deadline_started_us == 4900000);
    assert(!detector_watch_start_watch_deadline(
        &session, 9900000, &watch_deadline_started_us));
    assert(watch_deadline_started_us == 4900000);
    detector_watch_status_line(&session,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Lab (east)");
    assert(session.phase == DETECTOR_WATCH_RUNNING && session.running &&
           session.watch_verified);
}

static void test_watch_proof_requires_exact_bssid_and_ssid_field(void)
{
    detector_watch_session_t session = {0};
    detector_watch_network_t networks[DETECTOR_WATCH_MAX] = {0};
    session.networks = networks;
    detector_watch_reset(&session);
    detector_watch_begin_scan(&session, 14);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Office", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));

    detector_watch_begin_start(&session, true);
    detector_watch_status_line(&session,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&session,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55X SSID=Office");
    assert(session.phase == DETECTOR_WATCH_ERROR && !session.watch_verified);

    detector_watch_begin_start(&session, true);
    detector_watch_status_line(&session,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&session,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55");
    assert(session.phase == DETECTOR_WATCH_ERROR && !session.watch_verified);
}

static void test_emergency_stop_and_unconfirmed_stop_keep_remote_state_uncertain(void)
{
    detector_watch_session_t grove = {0}, mbus = {0};
    detector_watch_network_t grove_networks[DETECTOR_WATCH_MAX] = {0};
    detector_watch_network_t mbus_networks[DETECTOR_WATCH_MAX] = {0};
    grove.networks = grove_networks;
    mbus.networks = mbus_networks;
    detector_watch_reset(&grove);
    detector_watch_reset(&mbus);
    detector_watch_begin_start(&grove, false);
    detector_watch_begin_start(&mbus, false);
    detector_watch_status_line(&grove,
        "Deauth Detector started - scanning all channels for deauth frames...");
    detector_watch_status_line(&mbus,
        "Deauth Detector started - scanning all channels for deauth frames...");
    assert(grove.running && mbus.running);

    detector_watch_emergency_stop(&grove);
    assert(grove.phase == DETECTOR_WATCH_ERROR && grove.running &&
           grove.remote_state_unknown);
    assert(mbus.phase == DETECTOR_WATCH_RUNNING && mbus.running &&
           !mbus.remote_state_unknown);
    detector_watch_status_line(&grove, "Deauth Detector stopped.");
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.running &&
           !grove.remote_state_unknown);

    detector_watch_begin_stop(&mbus);
    detector_watch_stop_unconfirmed(&mbus);
    assert(mbus.phase == DETECTOR_WATCH_ERROR && mbus.running &&
           mbus.remote_state_unknown &&
           strstr(mbus.error, "No stop confirmation") != NULL);
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.remote_state_unknown);
    detector_watch_status_line(&mbus, "I JanOS: All operations stopped.");
    assert(!mbus.running && !mbus.remote_state_unknown);
}

static void test_already_active_requires_confirmed_stop_before_new_start(void)
{
    detector_watch_session_t session = {0};
    detector_watch_network_t networks[DETECTOR_WATCH_MAX] = {0};
    session.networks = networks;
    detector_watch_reset(&session);
    detector_watch_begin_start_details(&session, false, true);
    detector_watch_status_line(&session,
        "Deauth detector already active. Use 'stop' to stop it first.");
    assert(session.phase == DETECTOR_WATCH_ERROR && !session.running &&
           session.remote_state_unknown && !session.legacy_retry_pending);
    detector_watch_begin_stop(&session);
    detector_watch_status_line(&session, "Deauth Detector stopped.");
    assert(session.phase == DETECTOR_WATCH_STOPPED && !session.running &&
           !session.remote_state_unknown);
}

static void test_trace_command_status_unsupported_fallback_and_stop(void)
{
    detector_watch_session_t session = {0};
    detector_watch_network_t networks[DETECTOR_WATCH_MAX] = {0};
    session.networks = networks;
    detector_watch_reset(&session);
    char command[320];

    assert(detector_watch_build_v2_command(&session, false, 0, true,
                                           command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector_v2 trace"));
    assert(detector_watch_is_trace_status_line(
        "> [DETECTOR_TRACE] locked_channel=6"));
    assert(!detector_watch_is_trace_status_line("[DETECTOR_INFO] format=deauth_v2"));
    detector_watch_begin_start_v2(&session, false, true);
    assert(session.trace_requested &&
           session.trace_state == DETECTOR_TRACE_WAITING_ARM);
    detector_watch_status_line(&session,
        "[DEAUTH_V2] CH: 1 | BSSID: 00:11:22:33:44:55 | TX: 00:11:22:33:44:55 | DA: FF:FF:FF:FF:FF:FF | RSSI: -40 | REASON: 7 | SSID: [DETECTOR_INFO] format=deauth_v2");
    assert(session.phase == DETECTOR_WATCH_STARTING &&
           !session.details_confirmed && !session.start_ack_seen);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] locked_channel=6");
    assert(session.trace_state == DETECTOR_TRACE_WAITING_ARM);
    /* A marker cannot confirm an unacknowledged/unformatted session. */
    detector_watch_status_line(&session,
        "Deauth Detector started - scanning all channels for deauth frames...");
    assert(session.phase == DETECTOR_WATCH_STARTING && session.start_ack_seen);
    detector_watch_status_line(&session, "[DETECTOR_INFO] format=deauth_v2");
    assert(session.phase == DETECTOR_WATCH_RUNNING && session.trace_requested &&
           session.trace_state == DETECTOR_TRACE_WAITING_ARM);
    detector_watch_status_line(&session, "> [DETECTOR_TRACE] armed");
    assert(session.trace_state == DETECTOR_TRACE_ARMED);

    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] confirming_bssid=00:11:22:33:44:55 first_channel=6 window_ms=999");
    assert(session.trace_state == DETECTOR_TRACE_ARMED);
    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] confirming_bssid=00:11:22:33:44:ZZ first_channel=6 window_ms=1000");
    assert(session.trace_state == DETECTOR_TRACE_ARMED);
    detector_watch_status_line(&session,
        "I JanOS: [DETECTOR_TRACE] confirming_bssid=00:11:22:33:44:55 first_channel=6 window_ms=1000");
    assert(session.trace_state == DETECTOR_TRACE_CONFIRMING &&
           !strcmp(session.trace_confirming_bssid, "00:11:22:33:44:55") &&
           session.trace_first_channel == 6 && session.trace_window_ms == 1000);
    detector_watch_status_line(&session,
        "> [DETECTOR_TRACE] lock_failed_channel=6 error=ESP_ERR_WIFI_NOT_STARTED");
    assert(session.phase == DETECTOR_WATCH_RUNNING &&
           session.trace_state == DETECTOR_TRACE_RETRY_READY &&
           session.trace_failed_channel == 6 &&
           !strcmp(session.trace_error, "ESP_ERR_WIFI_NOT_STARTED"));
    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] confirming_bssid=00:11:22:33:44:55 first_channel=11 window_ms=1000");
    assert(session.trace_state == DETECTOR_TRACE_CONFIRMING &&
           session.trace_first_channel == 11);
    detector_watch_status_line(&session,
        "I JanOS: [DETECTOR_TRACE] locked_channel=11");
    assert(session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 11);
    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] released_channel=11 reason=idle idle_ms=19999");
    assert(session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 11);
    detector_watch_status_line(&session,
        "I JanOS: > [DETECTOR_TRACE] released_channel=11 reason=idle idle_ms=20000");
    assert(session.trace_state == DETECTOR_TRACE_RELEASED &&
           session.trace_channel == 0 &&
           session.trace_released_channel == 11 &&
           session.trace_release_idle_ms == 20000);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] armed");
    assert(session.trace_state == DETECTOR_TRACE_ARMED &&
           session.trace_released_channel == 11 &&
           session.trace_release_idle_ms == 20000);
    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] confirming_bssid=00:11:22:33:44:55 first_channel=1 window_ms=1000");
    assert(session.trace_state == DETECTOR_TRACE_CONFIRMING &&
           session.trace_released_channel == 0 &&
           session.trace_release_idle_ms == 0);
    detector_watch_status_line(&session,
        "[DETECTOR_TRACE] locked_channel=1");
    assert(session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 1);
    detector_watch_begin_stop(&session);
    detector_watch_status_line(&session,
        "[DEAUTH_V2] CH: 1 | BSSID: 00:11:22:33:44:55 | TX: 00:11:22:33:44:55 | DA: FF:FF:FF:FF:FF:FF | RSSI: -40 | REASON: 7 | SSID: Deauth Detector stopped.");
    assert(session.phase == DETECTOR_WATCH_STOPPING &&
           session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 1);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] armed");
    detector_watch_stop_unconfirmed(&session);
    assert(session.remote_state_unknown &&
           session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 1);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] locked_channel=3");
    assert(session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 1);
    detector_watch_status_line(&session, "Deauth Detector stopped.");
    assert(session.trace_state == DETECTOR_TRACE_OFF &&
           session.trace_channel == 0 &&
           session.trace_released_channel == 0 &&
           session.trace_release_idle_ms == 0 &&
           !session.trace_requested &&
           !session.remote_state_unknown);

    /* Older V2 firmware can skip armed/confirming without blocking Start. */
    detector_watch_begin_scan(&session, 11);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Office", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));
    detector_watch_begin_start_v2(&session, true, true);
    detector_watch_status_line(&session,
        "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&session, "[DETECTOR_INFO] format=deauth_v2");
    detector_watch_status_line(&session,
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Office");
    assert(session.phase == DETECTOR_WATCH_RUNNING && session.count == 1 &&
           session.scan_complete && session.selected_count == 1 &&
           session.trace_state == DETECTOR_TRACE_WAITING_ARM);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] locked_channel=6");
    assert(session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 6);
    detector_watch_connection_lost(&session);
    assert(session.phase == DETECTOR_WATCH_ERROR &&
           session.remote_state_unknown && !session.running &&
           session.trace_requested && session.details_confirmed &&
           session.trace_state == DETECTOR_TRACE_LOCKED &&
           session.trace_channel == 6 && session.count == 0 &&
           !session.scan_complete && session.selected_count == 0);
    detector_watch_status_line(&session, "[DETECTOR_TRACE] locked_channel=3");
    assert(session.trace_channel == 6);
    detector_watch_begin_stop(&session);
    detector_watch_status_line(&session, "Deauth Detector stopped.");

    detector_watch_begin_start_v2(&session, false, true);
    detector_watch_status_line(&session,
        "Invalid detector index 'trace' (valid: 1-48). Detector not started.");
    assert(session.phase == DETECTOR_WATCH_ERROR &&
           session.trace_state == DETECTOR_TRACE_UNSUPPORTED &&
           !session.remote_state_unknown && !session.legacy_retry_pending);

    detector_watch_reset(&session);
    detector_watch_begin_scan(&session, 12);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Office", "00:11:22:33:44:55", 6);
    network(&session, 2, "Lab", "00:11:22:33:44:66", 11);
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));
    assert(detector_watch_select(&session, 2, true));
    assert(detector_watch_build_v2_command(&session, true, 12, true,
                                           command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector_v2 --only 1 2 trace"));
    detector_watch_begin_start_v2(&session, true, true);
    detector_watch_status_line(&session, "Unrecognized command");
    assert(session.legacy_retry_pending && session.trace_requested);
    assert(detector_watch_build_command(&session, true, 12,
                                        command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 1 2"));
    assert(!strstr(command, "trace"));
    detector_watch_begin_start(&session, true);
    assert(session.legacy_fallback &&
           session.trace_state == DETECTOR_TRACE_LEGACY_DROPPED &&
           !session.trace_requested);

    detector_watch_reset(&session);
    detector_watch_begin_scan(&session, 13);
    assert(detector_watch_ack_scan(&session,
        "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&session, 1, "Office", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&session, "Scan results printed."));
    assert(detector_watch_select(&session, 1, true));
    detector_watch_begin_start_v2(&session, true, true);
    detector_watch_status_line(&session,
        "Invalid watch index 'trace' (valid: 1-1). Detector not started.");
    assert(session.trace_state == DETECTOR_TRACE_UNSUPPORTED &&
           !session.legacy_retry_pending);
}

int main(void)
{
    test_details_ack_fallback_timeout_and_two_sessions();
    test_v2_command_requires_fresh_completed_scan_and_preserves_all_indices();
    test_selected_v2_watch_lines_before_format_confirmation();
    test_watch_proof_requires_exact_bssid_and_ssid_field();
    test_emergency_stop_and_unconfirmed_stop_keep_remote_state_uncertain();
    test_already_active_requires_confirmed_stop_before_new_start();
    test_trace_command_status_unsupported_fallback_and_stop();
    detector_watch_session_t grove = {0}, mbus = {0};
    detector_watch_network_t grove_storage[DETECTOR_WATCH_MAX] = {0};
    detector_watch_network_t mbus_storage[DETECTOR_WATCH_MAX] = {0};
    grove.networks = grove_storage;
    mbus.networks = mbus_storage;
    char command[320];
    detector_watch_session_t missing_storage = {0};
    detector_watch_reset(&missing_storage);
    assert(missing_storage.phase == DETECTOR_WATCH_ERROR);
    assert(!detector_watch_build_command(&missing_storage, false, 0,
                                         command, sizeof(command)));
    detector_watch_begin_scan(&missing_storage, 1);
    assert(missing_storage.phase == DETECTOR_WATCH_ERROR);

    detector_watch_reset(&grove);
    assert(grove.networks == grove_storage);
    assert(detector_watch_build_command(&grove, false, 0, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector"));
    assert(!detector_watch_build_command(&grove, true, 0, command, sizeof(command)));

    detector_watch_begin_scan(&grove, 4);
    assert(!detector_watch_add_network(&grove, 1, "stale", "00:11:22:33:44:55", 6));
    assert(!detector_watch_finish_scan(&grove, "Scan results printed."));
    assert(grove.count == 0 && !grove.scan_complete);
    assert(!detector_watch_ack_scan(&grove, "Showing results from last scan (3 networks found):"));
    assert(detector_watch_ack_scan(&grove, "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&grove, 1, "Office", "00:11:22:33:44:55", 6);
    network(&grove, 2, "Office", "00:11:22:33:44:66", 11);
    network(&grove, 3, "Lab (east)", "AA:BB:CC:DD:EE:FF", 36);
    assert(!detector_watch_build_command(&grove, true, 4, command, sizeof(command)));
    assert(!detector_watch_select(&grove, 1, true));
    assert(!detector_watch_finish_scan(&grove, "Scan results printed"));
    assert(!detector_watch_finish_scan(&grove, "Scan results printed. trailing"));
    assert(detector_watch_finish_scan(&grove, "I (1234) JanOS: Scan results printed."));
    assert(!detector_watch_select(&grove, 0, true));
    assert(!detector_watch_select(&grove, 4, true));
    assert(detector_watch_select(&grove, 1, true));
    assert(detector_watch_select(&grove, 2, true));
    assert(detector_watch_build_command(&grove, true, 4, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 1 2"));
    assert(!detector_watch_build_command(&grove, true, 5, command, sizeof(command)));
    assert(detector_watch_build_command(&grove, false, 5, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector"));

    detector_watch_begin_start(&grove, true);
    detector_watch_status_line(&grove, "Deauth Detector started - scanning all channels for deauth frames...");
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.running);
    detector_watch_begin_start(&grove, true);
    assert(grove.phase == DETECTOR_WATCH_STARTING);
    detector_watch_status_line(&grove, "I JanOS: [DETECTOR_INFO] CSA/ECSA announcements enabled.");
    assert(grove.phase == DETECTOR_WATCH_STARTING);
    detector_watch_status_line(&grove, "I JanOS: Deauth Detector started - monitoring selected channels for deauth frames...");
    assert(grove.phase == DETECTOR_WATCH_STARTING && !grove.watch_verified);
    detector_watch_status_line(&grove, "[DETECTOR_WATCH] count=2 ap=1/2 CH=6 BSSID=00:11:22:33:44:55 SSID=Office");
    assert(!grove.watch_verified);
    assert(!detector_watch_accept_event(&grove, "00:11:22:33:44:55"));
    detector_watch_status_line(&grove, "[DETECTOR_WATCH] count=2 ap=1/2 CH=11 BSSID=00:11:22:33:44:66 SSID=Office");
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.watch_verified);
    detector_watch_begin_start(&grove, true);
    detector_watch_status_line(&grove, "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&grove, "[DETECTOR_WATCH] count=2 ap=1/2 CH=6 BSSID=00:11:22:33:44:55 SSID=Office");
    detector_watch_status_line(&grove, "[DETECTOR_WATCH] count=2 ap=2/2 CH=11 BSSID=00:11:22:33:44:66 SSID=Office");
    assert(grove.watch_verified && grove.watched_count == 2);
    assert(detector_watch_accept_event(&grove, "00:11:22:33:44:55"));
    assert(detector_watch_accept_event(&grove, "00:11:22:33:44:66"));
    assert(!detector_watch_accept_event(&grove, "AA:BB:CC:DD:EE:FF"));
    detector_watch_begin_stop(&grove);
    assert(grove.phase == DETECTOR_WATCH_STOPPING);
    detector_watch_status_line(&grove, "I JanOS: Deauth Detector stopped.");
    assert(grove.phase == DETECTOR_WATCH_STOPPED && !grove.watch_verified);

    detector_watch_begin_scan(&grove, 6);
    assert(grove.networks == grove_storage && !grove.networks[0].selected);
    assert(grove.count == 0 && !grove.scan_complete);
    assert(detector_watch_ack_scan(&grove, "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&grove, 1, "Office", "00:11:22:33:44:55", 6);
    assert(!detector_watch_add_network(&grove, 1, "Bad", "00:11:22:33:44:66", 11));
    assert(!detector_watch_add_network(&grove, 3, "Gap", "00:11:22:33:44:77", 11));
    assert(!detector_watch_finish_scan(&grove, "Scan results printed."));
    detector_watch_begin_scan(&grove, 7);
    assert(detector_watch_ack_scan(&grove, "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&grove, 1, "Office", "00:11:22:33:44:55", 6);
    assert(detector_watch_finish_scan(&grove, "Scan results printed."));
    assert(detector_watch_select(&grove, 1, true));
    assert(detector_watch_build_command(&grove, true, 7, command, sizeof(command)));
    detector_watch_begin_start(&grove, true);
    detector_watch_status_line(&grove, "Invalid watch index '7' (valid: 1-1). Detector not started.");
    assert(grove.phase == DETECTOR_WATCH_ERROR);
    assert(!grove.running);

    detector_watch_begin_start(&grove, true);
    detector_watch_timeout(&grove);
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.running &&
           grove.remote_state_unknown);
    detector_watch_begin_stop(&grove);
    detector_watch_status_line(&grove, "Deauth Detector stopped.");
    assert(grove.phase == DETECTOR_WATCH_STOPPED && !grove.remote_state_unknown);
    detector_watch_begin_start(&grove, true);
    detector_watch_status_line(&grove, "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_watch_timeout(&grove);
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.running &&
           grove.remote_state_unknown && !grove.watch_verified);
    detector_watch_begin_start(&grove, true);
    detector_watch_status_line(&grove, "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&grove, "[DETECTOR_WATCH] count=1 ap=1/1 CH=11 BSSID=00:11:22:33:44:66 SSID=Other");
    assert(grove.phase == DETECTOR_WATCH_ERROR && !grove.watch_verified);

    detector_watch_begin_start(&grove, false);
    detector_watch_status_line(&grove, "Deauth Detector started - scanning all channels for deauth frames...");
    detector_watch_begin_stop(&grove);
    detector_watch_timeout(&grove);
    assert(grove.phase == DETECTOR_WATCH_ERROR && grove.running &&
           grove.remote_state_unknown);
    detector_watch_begin_stop(&grove);
    detector_watch_status_line(&grove, "Deauth Detector stopped.");
    assert(grove.phase == DETECTOR_WATCH_STOPPED && !grove.running &&
           !grove.remote_state_unknown);

    detector_watch_reset(&mbus);
    assert(mbus.networks == mbus_storage && mbus.networks != grove.networks);
    detector_watch_begin_scan(&mbus, 2);
    assert(detector_watch_ack_scan(&mbus, "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&mbus, 1, "Office", "AA:BB:CC:DD:EE:FF", 1);
    assert(detector_watch_finish_scan(&mbus, "Scan results printed."));
    assert(detector_watch_select(&mbus, 1, true));
    assert(detector_watch_build_command(&mbus, true, 2, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 1"));
    assert(grove.phase == DETECTOR_WATCH_STOPPED && mbus.phase == DETECTOR_WATCH_READY);
    detector_watch_begin_scan(&mbus, 3);
    assert(!detector_watch_build_command(&mbus, true, 3, command, sizeof(command)));

    detector_watch_begin_scan(&mbus, 4);
    assert(detector_watch_ack_scan(&mbus, "Background scan started (min: 20 ms, max: 40 ms per channel)"));
    network(&mbus, 1, "Office", "00:11:22:33:44:55", 6);
    network(&mbus, 2, "Office", "00:11:22:33:44:55", 11);
    assert(detector_watch_finish_scan(&mbus, "Scan results printed."));
    assert(detector_watch_select(&mbus, 2, true));
    assert(mbus.selected_count == 1);
    assert(detector_watch_build_command(&mbus, true, 4, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 2"));
    assert(detector_watch_select(&mbus, 1, true));
    assert(mbus.selected_count == 1);
    assert(detector_watch_build_command(&mbus, true, 4, command, sizeof(command)));
    assert(!strcmp(command, "deauth_detector --only 1 2"));
    detector_watch_begin_start(&mbus, true);
    detector_watch_status_line(&mbus, "Deauth Detector started - monitoring selected channels for deauth frames...");
    detector_watch_status_line(&mbus, "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Office");
    assert(mbus.phase == DETECTOR_WATCH_RUNNING && mbus.watch_verified);
    puts("detector watch state tests passed");
    return 0;
}
