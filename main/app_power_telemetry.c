#include "app_power_telemetry.h"

#include <string.h>

// The existing INA226 timer runs every two seconds: 30 samples are one minute.
#define APP_POWER_TELEMETRY_WINDOW_SAMPLES 30

static float s_current_window[APP_POWER_TELEMETRY_WINDOW_SAMPLES];
static float s_power_window[APP_POWER_TELEMETRY_WINDOW_SAMPLES];
static uint32_t s_window_count;
static uint32_t s_window_index;
static float s_current_sum;
static float s_power_sum;
static int64_t s_last_sample_us;
static float s_last_power_w;
static double s_energy_mwh;
static app_power_telemetry_snapshot_t s_snapshot;

static void clear_rolling_window(void)
{
    memset(s_current_window, 0, sizeof(s_current_window));
    memset(s_power_window, 0, sizeof(s_power_window));
    s_window_count = 0;
    s_window_index = 0;
    s_current_sum = 0.0f;
    s_power_sum = 0.0f;
    s_last_sample_us = 0;
    s_last_power_w = 0.0f;
}

void app_power_telemetry_reset(void)
{
    clear_rolling_window();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_energy_mwh = 0.0;
}

bool app_power_telemetry_record(float voltage_v, float current_a, bool current_valid,
                                int64_t now_us,
                                app_power_telemetry_snapshot_t *snapshot_out)
{
    if (!current_valid || voltage_v <= 0.1f || now_us <= 0) {
        clear_rolling_window();
        s_snapshot.valid = false;
        s_snapshot.average_60s_current_a = 0.0f;
        s_snapshot.average_60s_power_w = 0.0f;
        if (snapshot_out) {
            *snapshot_out = s_snapshot;
        }
        return false;
    }

    // On Tab5 the INA226 reports discharge as positive and charging as negative.
    // We measure energy taken from the battery, so charging must not subtract it.
    float draw_current_a = current_a > 0.0f ? current_a : 0.0f;
    float power_w = voltage_v * draw_current_a;

    if (s_window_count == APP_POWER_TELEMETRY_WINDOW_SAMPLES) {
        s_current_sum -= s_current_window[s_window_index];
        s_power_sum -= s_power_window[s_window_index];
    } else {
        s_window_count++;
    }
    s_current_window[s_window_index] = draw_current_a;
    s_power_window[s_window_index] = power_w;
    s_current_sum += draw_current_a;
    s_power_sum += power_w;
    s_window_index = (s_window_index + 1) % APP_POWER_TELEMETRY_WINDOW_SAMPLES;

    if (s_last_sample_us > 0 && now_us > s_last_sample_us) {
        double elapsed_s = (double)(now_us - s_last_sample_us) / 1000000.0;
        double average_interval_power_w = ((double)s_last_power_w + power_w) * 0.5;
        s_energy_mwh += average_interval_power_w * elapsed_s / 3.6;
    }
    s_last_sample_us = now_us;
    s_last_power_w = power_w;

    s_snapshot.valid = true;
    s_snapshot.sample_count++;
    s_snapshot.voltage_v = voltage_v;
    s_snapshot.current_a = current_a;
    s_snapshot.battery_draw_current_a = draw_current_a;
    s_snapshot.battery_power_w = power_w;
    s_snapshot.average_60s_current_a = s_current_sum / (float)s_window_count;
    s_snapshot.average_60s_power_w = s_power_sum / (float)s_window_count;
    s_snapshot.energy_mwh = (float)s_energy_mwh;

    if (snapshot_out) {
        *snapshot_out = s_snapshot;
    }
    return true;
}
