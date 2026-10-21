// Included by main.c after Observer data/context declarations.
static uint32_t observer_clock_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t observer_age_clock(const observer_network_t *net)
{
    return net->snapshot_live ? observer_clock_ms() : net->snapshot_clock_ms;
}

static void observer_plain_text(char *text)
{
    // User supplied names must never become LVGL recolor directives.
    for (char *p = text; *p; ++p) if (*p == '#') *p = '_';
}

static void observer_field_text(const observer_network_t *net, int field,
                                char *destination, size_t destination_cap)
{
    const observer_extended_t *x = &net->extended;
    const bool ext = x->present;
    char out[180];
    const size_t cap = sizeof(out);
    const char *color = NULL;
    out[0] = 0;
    switch (field) {
    case OBS_FIELD_SECURITY: snprintf(out, cap, "%s", ext ? x->security : net->security); break;
    case OBS_FIELD_PMF: snprintf(out, cap, "PMF %s", ext ? x->pmf :
        (net->inspected ? (net->mfp_capable ? "capable" : "disabled") : "unknown")); break;
    case OBS_FIELD_WPS: snprintf(out, cap, "%s", ext ? x->wps : "WPS unknown"); break;
    case OBS_FIELD_CHANNEL: {
        char band[32];
        snprintf(band, sizeof(band), "%s", net->band);
        observer_plain_text(band);
        snprintf(destination, destination_cap, "#5599FF CH%d# / #%s %s#", net->channel,
                 strstr(band, "5") ? "CC66FF" : "FFAA33", band);
        return;
    }
    case OBS_FIELD_RSSI:
        if (!ext || x->rssi_known) snprintf(out, cap, "%d dBm", net->rssi);
        else snprintf(out, cap, "RSSI unknown");
        break;
    case OBS_FIELD_AGE:
        if (ext && x->age_known) snprintf(out, cap, "Seen %lus ago", (unsigned long)
            ((x->age_ms + (uint32_t)(observer_age_clock(net) - net->extended_received_ms)) / 1000));
        else snprintf(out, cap, "Seen unknown");
        break;
    case OBS_FIELD_BSSID: snprintf(out, cap, "%s", net->bssid); break;
    case OBS_FIELD_UPTIME: snprintf(out, cap, "Uptime %s", net->uptime[0] ? net->uptime : "unknown"); break;
    case OBS_FIELD_VENDOR: snprintf(out, cap, "Vendor %s", !observer_resolve_vendors ? "disabled" : (net->vendor[0] ? net->vendor : "unknown")); break;
    case OBS_FIELD_PAIRWISE: snprintf(out, cap, "Pairwise %s", ext ? x->pairwise : "unknown"); break;
    case OBS_FIELD_GROUP: snprintf(out, cap, "Group %s", ext ? x->group : "unknown"); break;
    case OBS_FIELD_AKM: snprintf(out, cap, "AKM %s", ext ? x->akm : "unknown"); break;
    case OBS_FIELD_SOURCE: snprintf(out, cap, "SSID source: %s", ext ? x->ssid_source : "unknown"); break;
    }
    // Sanitize received values before adding our own trusted recolor directives.
    observer_plain_text(out);
    switch (field) {
    case OBS_FIELD_SECURITY:
        if (!out[0] || !strcmp(out, "Unknown") || !strcmp(out, "unknown")) {
            snprintf(out, cap, "Unknown"); color = "888888";
        } else color = strstr(out, "WPA3") ? "55DD55" : strstr(out, "WPA2") ? "00CCCC" : "FF6666";
        break;
    case OBS_FIELD_PMF:
        if (ext) color = x->pmf_required == 1 || (x->pmf_capable == 1 && x->pmf_required == 0)
            ? "FF5555" : x->pmf_capable == 0 && x->pmf_required == 0 ? "55DD55" : "888888";
        else color = !net->inspected ? "888888" : net->mfp_capable ? "FF5555" : "55DD55";
        break;
    case OBS_FIELD_WPS: color = !ext || x->wps_present < 0 ? "888888" : x->wps_present == 1 ? "FFAA00" : "CCCCCC"; break;
    case OBS_FIELD_RSSI: color = ext && !x->rssi_known ? "888888" : net->rssi > -50 ? "55DD55" : net->rssi > -70 ? "FFAA00" : "FF5555"; break;
    case OBS_FIELD_BSSID: color = "5599FF"; break;
    default: break;
    }
    if (color) snprintf(destination, destination_cap, "#%s %s#", color, out);
    else snprintf(destination, destination_cap, "%s", out);
}

static void observer_format_fields(const observer_network_t *net, char *out, size_t cap)
{
    out[0] = 0;
    for (int i = 0; i < OBS_FIELD_COUNT; ++i) {
        int field = observer_view_prefs.ap_order[i];
        if (!observer_view_prefs.ap_visible[field]) continue;
        char value[256]; observer_field_text(net, field, value, sizeof(value));
        if (!value[0]) continue;
        size_t n = strlen(out);
        // Never leave a truncated recolor directive active across later text.
        if (n + (n ? 5 : 0) + strlen(value) >= cap) break;
        snprintf(out + n, cap > n ? cap - n : 0, "%s%s", n ? "  |  " : "", value);
    }
}

static void observer_format_client(const observer_network_t *net, int idx, char *out, size_t cap)
{
    snprintf(out, cap, "%s", net->clients[idx]);
    for (int i = 0; i < OBS_CLIENT_FIELD_COUNT; ++i) {
        int field = observer_view_prefs.client_order[i];
        if (!observer_view_prefs.client_visible[field]) continue;
        char value[100] = "";
        if (field == OBS_CLIENT_VENDOR) snprintf(value, sizeof(value), "%s", !observer_resolve_vendors ? "Vendor disabled" : (net->client_vendors[idx][0] ? net->client_vendors[idx] : "Vendor unknown"));
        if (field == OBS_CLIENT_RSSI) {
            if (net->client_rssi_known[idx]) snprintf(value, sizeof(value), "%d dBm", net->client_rssi[idx]);
            else snprintf(value, sizeof(value), "RSSI unknown");
        }
        if (field == OBS_CLIENT_AGE) {
            if (net->client_age_known[idx]) snprintf(value, sizeof(value), "Seen %lus ago", (unsigned long)
                ((net->client_age_ms[idx] + (uint32_t)(observer_age_clock(net) - net->client_received_ms[idx])) / 1000));
            else snprintf(value, sizeof(value), "Seen unknown");
        }
        size_t n = strlen(out);
        snprintf(out + n, cap > n ? cap - n : 0, "  |  %s", value);
    }
}
