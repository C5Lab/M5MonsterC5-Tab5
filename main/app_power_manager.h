#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_POWER_PROFILE_INTERACTIVE = 0,
    APP_POWER_PROFILE_SCREEN_OFF,
    APP_POWER_PROFILE_PERFORMANCE,
} app_power_profile_t;

esp_err_t app_power_manager_init(void);
esp_err_t app_power_manager_set_screen_dimmed(bool dimmed);
bool app_power_manager_is_screen_dimmed(void);

esp_err_t app_power_manager_acquire_performance(const char *reason);
esp_err_t app_power_manager_release_performance(const char *reason);

app_power_profile_t app_power_manager_get_profile(void);
const char *app_power_manager_profile_name(app_power_profile_t profile);
uint32_t app_power_manager_get_max_frequency_mhz(void);

#ifdef __cplusplus
}
#endif
