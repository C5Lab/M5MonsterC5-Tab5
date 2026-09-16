from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    return path.read_text(encoding="utf-8") if path.exists() else ""


def function_body(source: str, signature: str) -> str:
    search_from = 0
    while True:
        start = source.index(signature, search_from)
        opening = source.index("{", start)
        semicolon = source.find(";", start, opening)
        if semicolon == -1:
            break
        search_from = start + len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PowerManagementContractTests(unittest.TestCase):
    def test_power_management_is_enabled_without_automatic_light_sleep(self):
        defaults = read("sdkconfig.defaults")
        generated = read("sdkconfig")

        self.assertIn("CONFIG_PM_ENABLE=y", defaults)
        self.assertIn("CONFIG_PM_ENABLE=y", generated)
        self.assertIn("CONFIG_ESP32P4_REV_MIN_1=y", defaults)
        self.assertIn("CONFIG_ESP32P4_REV_MIN_1=y", generated)
        self.assertNotIn("CONFIG_ESP32P4_SELECTS_REV_LESS_V3", defaults)
        self.assertIn("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y", defaults)
        self.assertIn("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y", generated)
        self.assertNotIn("CONFIG_FREERTOS_USE_TICKLESS_IDLE=y", defaults)
        self.assertNotIn("CONFIG_FREERTOS_USE_TICKLESS_IDLE=y", generated)

    def test_power_manager_profiles_match_tab5_policy(self):
        header = read("main/app_power_manager.h")
        source = read("main/app_power_manager.c")

        self.assertIn("APP_POWER_PROFILE_INTERACTIVE", header)
        self.assertIn("APP_POWER_PROFILE_SCREEN_OFF", header)
        self.assertIn("APP_POWER_PROFILE_PERFORMANCE", header)
        self.assertIn("APP_POWER_CPU_MAX_MHZ 360", source)
        self.assertIn("APP_POWER_CPU_SCREEN_OFF_MHZ 360", source)
        self.assertIn("APP_POWER_CPU_MIN_MHZ 40", source)
        self.assertIn(".light_sleep_enable = false", source)
        self.assertIn("atomic_bool s_screen_dimmed", source)
        self.assertNotIn("s_screen_dimmed = previous", source)

        selector = function_body(source, "static app_power_profile_t select_profile_locked")
        self.assertLess(selector.index("s_performance_claims"), selector.index("s_screen_dimmed"))

        apply_profile = function_body(source, "static esp_err_t apply_profile_locked")
        same_frequency = (
            "profile_max_frequency(profile) == profile_max_frequency(s_profile)"
        )
        self.assertIn(same_frequency, apply_profile)
        self.assertLess(apply_profile.index(same_frequency),
                        apply_profile.index("esp_pm_configure"))

    def test_touch_wake_refreshes_deferred_ui_before_backlight(self):
        source = read("main/main.c")
        wake = function_body(source, "static void wake_screen")
        sleep = function_body(source, "static void screen_timeout_timer_cb")

        self.assertIn("app_power_manager_set_screen_dimmed(false)", wake)
        self.assertIn("refresh_deferred_wardrive_ui()", wake)
        self.assertIn("set_brightness_gamma(screen_brightness_setting)", wake)
        self.assertIn("app_power_manager_set_screen_dimmed(true)", sleep)
        interactive = wake.index("app_power_manager_set_screen_dimmed(false)")
        refresh = wake.index("refresh_deferred_wardrive_ui()")
        backlight = wake.index("set_brightness_gamma(screen_brightness_setting)")
        self.assertLess(interactive, refresh)
        self.assertLess(refresh, backlight)

        dark = sleep.index("bsp_display_brightness_set(0)")
        limit = sleep.index("app_power_manager_set_screen_dimmed(true)")
        self.assertLess(dark, limit)
        self.assertIn('wake_screen("touch")', source)

    def test_sleep_overlay_wakes_on_press_and_cannot_scroll(self):
        source = read("main/main.c")
        sleep = function_body(source, "static void screen_timeout_timer_cb")

        self.assertIn(
            "lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_SCROLLABLE)", sleep
        )
        self.assertIn(
            "sleep_overlay_press_cb, LV_EVENT_PRESSED", sleep
        )
        self.assertNotIn(
            "sleep_overlay_click_cb, LV_EVENT_CLICKED", sleep
        )

    def test_screen_off_profile_keeps_the_dsi_framebuffer_active(self):
        app = read("main/main.c")
        manager_header = read("main/app_power_manager.h")
        manager_source = read("main/app_power_manager.c")
        bsp_header = read("components/m5stack_tab5/include/bsp/m5stack_tab5.h")
        bsp_source = read("components/m5stack_tab5/m5stack_tab5.c")

        self.assertNotIn("app_power_manager_set_screen_off_reduction_allowed", manager_header)
        selector = function_body(manager_source, "static app_power_profile_t select_profile_locked")
        self.assertNotIn("s_screen_off_reduction_allowed", selector)

        self.assertNotIn("bsp_display_set_framebuffer_active", bsp_header)
        self.assertNotIn("bsp_display_set_framebuffer_active", bsp_source)
        self.assertNotIn("esp_lcd_dpi_panel_set_pattern", bsp_source)

        sleep = function_body(app, "static void screen_timeout_timer_cb")
        dark = sleep.index("bsp_display_brightness_set(0)")
        dimmed = sleep.index("app_power_manager_set_screen_dimmed(true)")
        self.assertLess(dark, dimmed)
        self.assertNotIn("bsp_display_set_framebuffer_active", sleep)

        wake = function_body(app, "static void wake_screen")
        interactive = wake.index("app_power_manager_set_screen_dimmed(false)")
        refresh = wake.index("refresh_deferred_wardrive_ui()")
        backlight = wake.index("set_brightness_gamma(screen_brightness_setting)")
        self.assertLess(interactive, refresh)
        self.assertLess(refresh, backlight)
        self.assertNotIn("bsp_display_set_framebuffer_active", wake)

    def test_wardrive_defers_heavy_ui_while_screen_is_off(self):
        source = read("main/main.c")
        wardrive = function_body(source, "static void wardrive_monitor_task")
        reopen = function_body(source, "static void show_wardrive_page")

        self.assertIn("app_power_manager_is_screen_dimmed()", wardrive)
        self.assertIn("wardrive_ui_dirty", wardrive)
        self.assertIn("atomic_bool wardrive_ui_dirty", source)
        deferred = wardrive[wardrive.index("// Network parsing continues") :]
        self.assertLess(deferred.index("bsp_display_lock(0)"),
                        deferred.index("bool page_visible"))
        self.assertIn("wardrive_ui_dirty", reopen)
        self.assertIn("refresh_deferred_wardrive_ui", source)

    def test_wpa_crack_holds_the_performance_profile_until_cleanup(self):
        source = read("main/main.c")
        crack = function_body(source, "static void hs_crack_task")

        self.assertIn('app_power_manager_acquire_performance("wpa-crack")', crack)
        self.assertIn('app_power_manager_release_performance("wpa-crack")', crack)
        acquire = crack.index('app_power_manager_acquire_performance("wpa-crack")')
        finish = crack.index("finish:")
        workers_stopped = crack.index("hs_crack_workers_stop()")
        release = crack.index('app_power_manager_release_performance("wpa-crack")')
        self.assertLess(acquire, finish)
        self.assertLess(finish, workers_stopped)
        self.assertLess(workers_stopped, release)

    def test_ina226_telemetry_is_emitted_for_the_analyzer(self):
        cmake = read("main/CMakeLists.txt")
        source = read("main/main.c")
        telemetry = read("main/app_power_telemetry.c")

        self.assertIn('"app_power_manager.c"', cmake)
        self.assertIn('"app_power_telemetry.c"', cmake)
        self.assertIn("esp_pm", cmake)
        self.assertIn("POWER_CSV,", source)
        self.assertIn("APP_POWER_TELEMETRY_WINDOW_SAMPLES 30", telemetry)
        self.assertIn("current_a > 0.0f ? current_a : 0.0f", telemetry)
        self.assertIn("clear_rolling_window();", telemetry)


if __name__ == "__main__":
    unittest.main()
