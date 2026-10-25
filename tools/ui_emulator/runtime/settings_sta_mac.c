/* The production Settings screen and wire parser run against a virtual JanOS
 * command endpoint. No host UART, USB, NVS, or physical radio is accessed. */
EM_JS(int, app_sta_mac_connected, (int tab), {
 try {
  return emulatorDevice.device.systemStatus(emulatorDevice.module(tab)).connected ? 1 : 0;
 } catch(e) { return 0; }
});

/* Return 1 for a complete JSON line, 0 for a model error, -1 for overflow. */
EM_JS(int, app_sta_mac_command, (int tab, const char *command, char *out, int size), {
 try {
  const line=emulatorDevice.device.staMacCommand(emulatorDevice.module(tab), UTF8ToString(command));
  if(typeof line!=='string' || lengthBytesUTF8(line)+1>size) return -1;
  stringToUTF8(line,out,size);
  return 1;
 } catch(e) { return 0; }
});

static bool sta_mac_ui_port_connected(tab_id_t tab)
{
    return (tab == TAB_GROVE || tab == TAB_USB || tab == TAB_MBUS) &&
           app_sta_mac_connected((int)tab) == 1;
}

static void sta_mac_ui_start_request(bool set_policy)
{
    tab_id_t tab = sta_mac_ui.target;
    if (tab == TAB_INTERNAL) {
        sta_mac_ui_notice("Select Grove, USB or MBus first.", true);
        return;
    }
    if (!sta_mac_ui_port_connected(tab)) {
        sta_mac_ui_clear_status();
        sta_mac_ui_notice("No virtual JanOS device is connected on this port.", true);
        return;
    }

    char command[STA_MAC_SETUP_SET_COMMAND_BUFFER_SIZE];
    if (set_policy) {
        uint16_t selected = lv_dropdown_get_selected(sta_mac_ui.mode_dropdown);
        if (selected == 0 || selected > 3) {
            sta_mac_ui_notice("Choose Factory, Random or Fixed before saving.", true);
            return;
        }
        if (!sta_mac_setup_build_set_command((sta_mac_setup_mode_t)(selected - 1),
                                             lv_textarea_get_text(sta_mac_ui.fixed_input),
                                             command, sizeof(command))) {
            sta_mac_ui_notice("Use exactly XX:XX:XX:XX:XX:XX; first octet must be local and unicast (for example 02).", true);
            return;
        }
    } else {
        snprintf(command, sizeof(command), "%s", sta_mac_setup_status_command);
    }

    char line[1024];
    if (app_sta_mac_command((int)tab, command, line, sizeof(line)) != 1) {
        sta_mac_ui_clear_status();
        sta_mac_ui_notice("Virtual JanOS command failed. Tap Refresh.", true);
        return;
    }

    if (set_policy) {
        bool saved = false;
        char error[128] = {0};
        if (!sta_mac_setup_parse_set_line(line, command, &saved, error, sizeof(error))) {
            sta_mac_ui_clear_status();
            sta_mac_ui_notice("Virtual JanOS returned an invalid save reply.", true);
            return;
        }
        if (!saved) {
            sta_mac_ui_notice(error[0] ? error : "Virtual JanOS rejected the policy.", true);
            return;
        }
    }

    if (set_policy && app_sta_mac_command((int)tab, sta_mac_setup_status_command,
                                          line, sizeof(line)) != 1) {
        sta_mac_ui_clear_status();
        sta_mac_ui_notice("Virtual policy saved, but status read failed. Tap Refresh.", true);
        return;
    }
    sta_mac_setup_status_t status = {0};
    if (!sta_mac_setup_parse_status_line(line, &status)) {
        sta_mac_ui_clear_status();
        sta_mac_ui_notice("Virtual JanOS returned an invalid status reply.", true);
        return;
    }
    sta_mac_ui_display_status(&status);
    if (set_policy) {
        static const char prefix[] = "sta_mac set ";
        if (strcmp(status.saved_policy, command + sizeof(prefix) - 1) != 0) {
            sta_mac_ui_notice("Virtual status differs from the requested policy.", true);
            return;
        }
        sta_mac_ui_notice(status.restart_required
            ? "Virtual policy saved. Simulate a JanOS reboot to apply it."
            : "Virtual policy saved. No reboot is needed for this policy.", false);
    } else {
        sta_mac_ui_notice("Status read from the selected virtual JanOS device.", false);
    }
}
