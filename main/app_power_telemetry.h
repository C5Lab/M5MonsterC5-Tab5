#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    uint32_t sample_count;
    float voltage_v;
    float current_a;
    float battery_draw_current_a;
    float battery_power_w;
    float average_60s_current_a;
    float average_60s_power_w;
    float energy_mwh;
} app_power_telemetry_snapshot_t;

void app_power_telemetry_reset(void);
bool app_power_telemetry_record(float voltage_v, float current_a, bool current_valid,
                                int64_t now_us,
                                app_power_telemetry_snapshot_t *snapshot_out);
