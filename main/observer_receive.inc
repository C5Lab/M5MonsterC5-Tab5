static const char *observer_query_command(tab_context_t *ctx, bool vendor)
{
    if (ctx->observer_extended_support < 0)
        return vendor ? "show_sniffer_results_vendor\r\n" : "show_sniffer_results\r\n";
    return vendor ? "show_sniffer_results_vendor extended\r\n" : "show_sniffer_results extended\r\n";
}

static bool observer_is_prompt_line(const char *line)
{
    while (*line == ' ' || *line == '\t') ++line;
    if (*line++ != '>') return false;
    while (*line == ' ' || *line == '\t') ++line;
    return *line == 0;
}

static bool observer_is_noise_line(const char *line)
{
    return strstr(line, "Sniffer packet count:") || strstr(line, "show_sniffer_results") ||
           strncmp(line, "I (", 3) == 0 || strncmp(line, "W (", 3) == 0 ||
           strncmp(line, "E (", 3) == 0 || strncmp(line, "[DEBUG]", 7) == 0 ||
           strncmp(line, "[HS-SNIFF]", 10) == 0;
}

static bool observer_extended_rejected(const char *line)
{
    return strstr(line, "Usage:") || strstr(line, "usage:") ||
           strstr(line, "Invalid argument") || strstr(line, "Too many arguments") ||
           strstr(line, "Unknown argument");
}

static bool observer_legacy_prefix(const char *line, char *out, size_t cap)
{
    const char *suffix = observer_ext_suffix_start(line);
    size_t length = suffix ? (size_t)(suffix - line) : strlen(line);
    while (length && (line[length - 1] == ' ' || line[length - 1] == '\t')) --length;
    if (length >= cap) return false;
    memcpy(out, line, length); out[length] = 0;
    return true;
}

static int observer_receive_ap(tab_context_t *ctx, const char *line, bool vendor, int focus)
{
    observer_network_t *parsed = heap_caps_calloc(1, sizeof(*parsed), MALLOC_CAP_SPIRAM);
    if (!parsed) return -1;
    char prefix[256];
    if (!observer_legacy_prefix(line, prefix, sizeof(prefix)) ||
        !parse_sniffer_network_line(prefix, parsed, vendor)) { free(parsed); return -1; }
    bool has_suffix = observer_ext_suffix_start(line) != NULL;
    if (has_suffix && !observer_ext_parse(line, &parsed->extended)) { free(parsed); return -1; }
    if (parsed->extended.present) {
        if (!observer_ext_value(&parsed->extended, "bssid", parsed->bssid, sizeof(parsed->bssid)) ||
            !observer_scan_field_is_mac(parsed->bssid)) { free(parsed); return -1; }
        ctx->observer_extended_support = 1;
    }
    int found = -1, matches = 0;
    for (int n = 0; n < ctx->observer_network_count; ++n) {
        observer_network_t *net = &ctx->observer_networks[n];
        bool match = parsed->extended.present ? strcasecmp(net->bssid, parsed->bssid) == 0 :
            (strcmp(net->ssid, parsed->ssid) == 0 && net->channel == parsed->channel);
        if (match) { found = n; ++matches; }
    }
    // Legacy output cannot safely distinguish duplicate SSID/channel pairs.
    if (matches > 1 || (focus >= 0 && found != focus)) { free(parsed); return -1; }
    if (found < 0 && focus < 0 && parsed->extended.present && ctx->observer_network_count < MAX_OBSERVER_NETWORKS) {
        found = ctx->observer_network_count++;
        observer_network_t *net = &ctx->observer_networks[found];
        memset(net, 0, sizeof(*net));
        snprintf(net->ssid, sizeof(net->ssid), "%s", parsed->ssid);
        snprintf(net->bssid, sizeof(net->bssid), "%s", parsed->bssid);
        ctx->observer_has_new_networks = true;
    }
    if (found >= 0) {
        observer_network_t *net = &ctx->observer_networks[found];
        if (vendor) snprintf(net->vendor, sizeof(net->vendor), "%s", parsed->vendor);
        if (parsed->extended.present) {
            net->extended = parsed->extended;
            net->extended_received_ms = observer_clock_ms();
            net->snapshot_live = true;
            net->channel = parsed->channel;
            snprintf(net->band, sizeof(net->band), "%s", net->channel > 14 ? "5GHz" : "2.4GHz");
            if (net->extended.rssi_known) net->rssi = net->extended.rssi;
        }
    }
    free(parsed);
    return found;
}

static bool observer_receive_client(observer_network_t *net, const char *line, bool vendor)
{
    char mac[18], maker[48], prefix[128];
    if (!observer_legacy_prefix(line, prefix, sizeof(prefix)) ||
        !parse_sniffer_client_line(prefix, mac, sizeof(mac), maker, sizeof(maker), vendor) ||
        !observer_scan_field_is_mac(mac)) return false;
    observer_extended_t *x = NULL;
    if (observer_ext_suffix_start(line)) {
        x = heap_caps_calloc(1, sizeof(*x), MALLOC_CAP_SPIRAM);
        if (!x || !observer_ext_parse(line, x) || strlen(x->raw) >= sizeof(net->client_extended[0])) { free(x); return false; }
    }
    add_client_mac(net, mac, vendor ? maker : NULL);
    for (int i = 0; i < net->client_count && i < MAX_CLIENTS_PER_NETWORK; ++i) {
        if (strcmp(net->clients[i], mac) != 0) continue;
        if (x) {
            memcpy(net->client_extended[i], x->raw, strlen(x->raw) + 1);
            net->client_rssi_known[i] = x->rssi_known;
            net->client_rssi[i] = x->rssi;
            net->client_age_known[i] = x->age_known;
            net->client_age_ms[i] = x->age_ms;
            net->client_received_ms[i] = observer_clock_ms();
        }
        break;
    }
    free(x);
    return true;
}
