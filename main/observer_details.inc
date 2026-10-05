static void observer_technical_toggle(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_user_data(e);
    if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void observer_update_details(tab_context_t *ctx)
{
    if (!ctx->network_popup || ctx->popup_network_idx < 0 ||
        ctx->popup_network_idx >= ctx->observer_network_count) return;
    observer_network_t *net = &ctx->observer_networks[ctx->popup_network_idx];
    const size_t text_cap = 4096;
    char *text = heap_caps_malloc(text_cap, MALLOC_CAP_SPIRAM);
    if (!text) return;
    for (int tab = 0; tab < 3; ++tab) {
        lv_obj_t *label = ctx->observer_details_labels[tab];
        if (!label || !lv_obj_is_valid(label)) continue;
        if (tab == 0) {
            char rssi[24];
            if (net->extended.present && !net->extended.rssi_known) snprintf(rssi, sizeof(rssi), "Unknown");
            else snprintf(rssi, sizeof(rssi), "%d dBm", net->rssi);
            snprintf(text, text_cap, "SSID: %s%s\nBSSID: %s\nChannel: %d / %s\nRSSI: %s\nUptime: %s\nVendor: %s\n\n",
                net->extended.hidden == 1 ? "(Hidden) - " : "",
                net->extended.present && net->extended.resolved_ssid[0] ? net->extended.resolved_ssid : (net->ssid[0] ? net->ssid : "Unknown"),
                net->bssid, net->channel, net->band, rssi,
                net->uptime[0] ? net->uptime : "Unknown", net->vendor[0] ? net->vendor : "Unknown");
            size_t n = strlen(text);
            if (net->extended.present) observer_ext_details(&net->extended, tab, text+n, text_cap-n);
            else snprintf(text+n, text_cap-n, "Extended data not available yet.");
        } else if (net->extended.present) observer_ext_details(&net->extended, tab, text, text_cap);
        else snprintf(text, text_cap, "Extended data not available yet.");
        if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
    }
    if (ctx->observer_technical_label && lv_obj_is_valid(ctx->observer_technical_label)) {
        if (net->extended.present) observer_ext_details(&net->extended, 4, text, text_cap);
        else snprintf(text, text_cap, "Extended unavailable");
        if (strcmp(lv_label_get_text(ctx->observer_technical_label), text))
            lv_label_set_text(ctx->observer_technical_label, text);
    }
    free(text);
}
