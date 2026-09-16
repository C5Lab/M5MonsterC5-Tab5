#include "app_power_manager.h"

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_pm.h"

#define APP_POWER_CPU_MAX_MHZ 360
// Keep the display-off profile at the same clock while the MIPI-DPI panel is
// initialized. Hardware testing showed that changing the clock or switching
// the DPI source can leave the panel blank after an underrun.
#define APP_POWER_CPU_SCREEN_OFF_MHZ 360
#define APP_POWER_CPU_MIN_MHZ 40

static const char *TAG = "app_power";

static SemaphoreHandle_t s_mutex;
static esp_pm_lock_handle_t s_performance_lock;
static bool s_initialized;
static atomic_bool s_screen_dimmed;
static bool s_performance_lock_held;
static uint32_t s_performance_claims;
static app_power_profile_t s_profile = APP_POWER_PROFILE_INTERACTIVE;

static app_power_profile_t select_profile_locked(void)
{
    if (s_performance_claims > 0) {
        return APP_POWER_PROFILE_PERFORMANCE;
    }
    if (atomic_load_explicit(&s_screen_dimmed, memory_order_acquire)) {
        return APP_POWER_PROFILE_SCREEN_OFF;
    }
    return APP_POWER_PROFILE_INTERACTIVE;
}

static uint32_t profile_max_frequency(app_power_profile_t profile)
{
    return profile == APP_POWER_PROFILE_SCREEN_OFF
               ? APP_POWER_CPU_SCREEN_OFF_MHZ
               : APP_POWER_CPU_MAX_MHZ;
}

const char *app_power_manager_profile_name(app_power_profile_t profile)
{
    switch (profile) {
        case APP_POWER_PROFILE_SCREEN_OFF:
            return "screen_off";
        case APP_POWER_PROFILE_PERFORMANCE:
            return "performance";
        case APP_POWER_PROFILE_INTERACTIVE:
        default:
            return "interactive";
    }
}

static esp_err_t apply_profile_locked(app_power_profile_t profile)
{
    if (s_initialized &&
        profile_max_frequency(profile) == profile_max_frequency(s_profile)) {
        if (profile != s_profile) {
            ESP_LOGI(TAG, "Profile %s: CPU config %u-%u MHz, automatic light sleep off",
                     app_power_manager_profile_name(profile),
                     (unsigned)APP_POWER_CPU_MIN_MHZ,
                     (unsigned)profile_max_frequency(profile));
        }
        s_profile = profile;
        return ESP_OK;
    }

    esp_pm_config_t config = {
        .max_freq_mhz = (int)profile_max_frequency(profile),
        .min_freq_mhz = APP_POWER_CPU_MIN_MHZ,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not apply %s profile: %s",
                 app_power_manager_profile_name(profile), esp_err_to_name(err));
        return err;
    }

    if (!s_initialized || profile != s_profile) {
        ESP_LOGI(TAG, "Profile %s: CPU config %u-%u MHz, automatic light sleep off",
                 app_power_manager_profile_name(profile),
                 (unsigned)APP_POWER_CPU_MIN_MHZ,
                 (unsigned)profile_max_frequency(profile));
    }
    s_profile = profile;
    return ESP_OK;
}

esp_err_t app_power_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0,
                                       "app-performance", &s_performance_lock);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }

    atomic_store_explicit(&s_screen_dimmed, false, memory_order_release);
    s_performance_claims = 0;
    s_performance_lock_held = false;
    err = apply_profile_locked(APP_POWER_PROFILE_INTERACTIVE);
    if (err != ESP_OK) {
        esp_pm_lock_delete(s_performance_lock);
        s_performance_lock = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t app_power_manager_set_screen_dimmed(bool dimmed)
{
    // This is the requested/physical screen state, not confirmation that the
    // frequency transition succeeded. Keep it truthful even in fallback mode.
    atomic_store_explicit(&s_screen_dimmed, dimmed, memory_order_release);
    if (!s_initialized || !s_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = apply_profile_locked(select_profile_locked());
    xSemaphoreGive(s_mutex);
    return err;
}

bool app_power_manager_is_screen_dimmed(void)
{
    return atomic_load_explicit(&s_screen_dimmed, memory_order_acquire);
}

esp_err_t app_power_manager_acquire_performance(const char *reason)
{
    if (!s_initialized || !s_mutex || !s_performance_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t previous_claims = s_performance_claims;
    s_performance_claims++;

    esp_err_t err = apply_profile_locked(select_profile_locked());
    if (err == ESP_OK && previous_claims == 0) {
        err = esp_pm_lock_acquire(s_performance_lock);
        if (err == ESP_OK) {
            s_performance_lock_held = true;
        }
    }

    if (err != ESP_OK) {
        s_performance_claims--;
        (void)apply_profile_locked(select_profile_locked());
    } else {
        ESP_LOGI(TAG, "Performance claim +1 (%s), total=%u",
                 reason ? reason : "unspecified", (unsigned)s_performance_claims);
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t app_power_manager_release_performance(const char *reason)
{
    if (!s_initialized || !s_mutex || !s_performance_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_performance_claims == 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (s_performance_claims == 1 && s_performance_lock_held) {
        err = esp_pm_lock_release(s_performance_lock);
        if (err == ESP_OK) {
            s_performance_lock_held = false;
        }
    }

    if (err == ESP_OK) {
        s_performance_claims--;
        err = apply_profile_locked(select_profile_locked());
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Performance claim -1 (%s), total=%u",
                 reason ? reason : "unspecified", (unsigned)s_performance_claims);
    }
    xSemaphoreGive(s_mutex);
    return err;
}

app_power_profile_t app_power_manager_get_profile(void)
{
    if (!s_initialized || !s_mutex) {
        return APP_POWER_PROFILE_INTERACTIVE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    app_power_profile_t profile = s_profile;
    xSemaphoreGive(s_mutex);
    return profile;
}

uint32_t app_power_manager_get_max_frequency_mhz(void)
{
    return profile_max_frequency(app_power_manager_get_profile());
}
