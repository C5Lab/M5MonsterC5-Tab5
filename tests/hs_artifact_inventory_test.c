#include "hs_artifact_inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void parser_cases(void)
{
    hs_artifact_message_t message;
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] CAPABILITIES req=0 snapshot=0 artifact_inventory=1 "
        "scopes=handshakes,pcaps page_max=32 entries_max=256 name_max=255 "
        "line_max=1024 inspect_max=16777216 validator=hccapx_v1", &message) ==
           HS_ARTIFACT_PARSE_OK);
    assert(message.type == HS_ARTIFACT_CAPABILITIES && message.page_max == 32 &&
           message.entries_max == 256 && message.inspect_max == 16777216);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=0 snapshot=0 status=ok reason=ok next=0 more=0 count=0",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=Rbusy snapshot=0 status=error reason=busy next=0 more=0 count=0",
        &message) == HS_ARTIFACT_PARSE_OK);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] BEGIN req=R_1 snapshot=17 scope=handshakes cursor=0 limit=32",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(message.type == HS_ARTIFACT_BEGIN && !strcmp(message.request_id, "R_1") &&
           message.snapshot == 17 && message.cursor == 0 && message.limit == 32 &&
           message.scope == HS_ARTIFACT_SCOPE_HANDSHAKES);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R_1 snapshot=17 entry=4 name_hex=4C616220312E70636170 "
        "size=11202 mtime=123 format=pcap validation=unknown reason=cache_stale",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(message.type == HS_ARTIFACT_ITEM && message.entry == 4 && message.size == 11202 &&
           message.name_length == 10 && !memcmp(message.name, "Lab 1.pcap", 10) &&
           message.format == HS_ARTIFACT_FORMAT_PCAP &&
           message.validation == HS_ARTIFACT_VALIDATION_UNKNOWN);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] RESULT req=R_1 snapshot=17 entry=4 validation=valid reason=ok "
        "crc32=57BF22AF bytes=11202", &message) == HS_ARTIFACT_PARSE_OK);
    assert(message.type == HS_ARTIFACT_RESULT && message.crc32_known &&
           message.crc32 == 0x57bf22af && message.inspected_bytes == 11202);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=R_1 snapshot=17 status=ok reason=ok next=32 more=1 count=32",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(message.type == HS_ARTIFACT_END && message.end_status == HS_ARTIFACT_END_OK &&
           message.more && message.next_cursor == 32 && message.count == 32);

    assert(hs_artifact_parse_line("ordinary boot log", &message) == HS_ARTIFACT_PARSE_IGNORED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R snapshot=1 entry=1 name_hex=0 size=1 mtime=1 "
        "format=pcap validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_MALFORMED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=R snapshot=1 status=ok reason=ok next=0 more=0 count=0 "
        "count=0", &message) == HS_ARTIFACT_PARSE_MALFORMED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] BEGIN req=bad! snapshot=1 scope=handshakes cursor=0 limit=1",
        &message) == HS_ARTIFACT_PARSE_MALFORMED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] RESULT req=R snapshot=1 entry=1 validation=valid reason=ok "
        "crc32=123 bytes=1", &message) == HS_ARTIFACT_PARSE_MALFORMED);
    message.type = HS_ARTIFACT_END;
    assert(hs_artifact_parse_line("[ARTIFACT/1] broken", &message) ==
           HS_ARTIFACT_PARSE_MALFORMED);
    assert(message.type == HS_ARTIFACT_MESSAGE_NONE);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R snapshot=1 entry=0 name_hex=61 size=1 mtime=1 "
        "format=pcap validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_MALFORMED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R snapshot=1 entry=1 name_hex=610062 size=1 mtime=1 "
        "format=pcap validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_MALFORMED);
}

typedef struct {
    unsigned messages;
    unsigned malformed;
    hs_artifact_message_type_t last;
} stream_seen_t;

static void stream_cb(hs_artifact_parse_result_t result,
                      const hs_artifact_message_t *message, void *context)
{
    stream_seen_t *seen = context;
    if (result == HS_ARTIFACT_PARSE_OK) {
        ++seen->messages;
        seen->last = message->type;
    } else if (result == HS_ARTIFACT_PARSE_MALFORMED) {
        ++seen->malformed;
    }
}

static void fragmented_stream(void)
{
    const char *stream =
        "noise\r\n[ARTIFACT/1] BEGIN req=A snapshot=7 scope=pcaps cursor=0 limit=2\r\n"
        "[ARTIFACT/1] END req=A snapshot=7 status=ok reason=ok next=0 more=0 count=0\n";
    hs_artifact_stream_t parser;
    hs_artifact_stream_init(&parser);
    stream_seen_t seen = {0};
    for (size_t i = 0; stream[i]; ++i)
        hs_artifact_stream_feed(&parser, (const uint8_t *)&stream[i], 1, stream_cb, &seen);
    assert(seen.messages == 2 && seen.malformed == 0 && seen.last == HS_ARTIFACT_END);

    uint8_t long_line[HS_ARTIFACT_LINE_MAX + 20];
    memset(long_line, 'X', sizeof(long_line));
    long_line[sizeof(long_line) - 1] = '\n';
    hs_artifact_stream_feed(&parser, long_line, sizeof(long_line), stream_cb, &seen);
    assert(seen.malformed == 1);
}

static void correlation_and_paging(void)
{
    hs_artifact_request_t request;
    hs_artifact_request_init(&request, "R1");
    hs_artifact_message_t message;
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] BEGIN req=R1 snapshot=9 scope=handshakes cursor=0 limit=2",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_ACCEPTED);
    assert(request.snapshot == 9 && request.begun && !request.terminal);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R1 snapshot=9 entry=1 name_hex=61 size=1 mtime=1 "
        "format=unknown validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_ACCEPTED);
    assert(request.items_seen == 1);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=old snapshot=9 entry=1 name_hex=61 size=1 mtime=1 "
        "format=unknown validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_STALE);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R1 snapshot=8 entry=1 name_hex=61 size=1 mtime=1 "
        "format=unknown validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_STALE);
    assert(hs_artifact_request_finish(&request) == HS_ARTIFACT_REQUEST_INCOMPLETE);

    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=R1 snapshot=9 status=ok reason=ok next=2 more=1 count=2",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_STALE);
    assert(!request.terminal);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] ITEM req=R1 snapshot=9 entry=2 name_hex=62 size=1 mtime=1 "
        "format=unknown validation=unknown reason=cache_stale", &message) ==
           HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_ACCEPTED);
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=R1 snapshot=9 status=ok reason=ok next=2 more=1 count=2",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_DONE);
    assert(request.more && request.next_cursor == 2 &&
           hs_artifact_request_finish(&request) == HS_ARTIFACT_REQUEST_DONE);

    hs_artifact_request_init(&request, "busy");
    assert(hs_artifact_parse_line(
        "[ARTIFACT/1] END req=busy snapshot=0 status=error reason=busy next=0 more=0 count=0",
        &message) == HS_ARTIFACT_PARSE_OK);
    assert(hs_artifact_request_accept(&request, &message) == HS_ARTIFACT_REQUEST_DONE);
}

static void legacy_and_catalog(void)
{
    uint64_t size;
    char name[HS_ARTIFACT_NAME_MAX + 1];
    assert(hs_artifact_parse_legacy_sized_row("12 15645263 Polish list final.pcap", &size,
                                               name, sizeof(name)));
    assert(size == 15645263 && !strcmp(name, "Polish list final.pcap"));
    assert(!hs_artifact_parse_legacy_sized_row("Found 14 file(s)", &size, name, sizeof(name)));
    assert(!hs_artifact_parse_legacy_sized_row("1 nope file.pcap", &size, name, sizeof(name)));

    hs_artifact_catalog_t catalog = {0};
    size_t asset_index;
    hs_artifact_location_t first = {0};
    first.source = HS_ARTIFACT_SOURCE_LOCAL;
    first.scope = HS_ARTIFACT_SCOPE_HANDSHAKES;
    first.size = 100;
    first.crc32_known = true;
    first.crc32 = 0x12345678;
    first.format = HS_ARTIFACT_FORMAT_PCAP;
    first.validation = HS_ARTIFACT_VALIDATION_VALID;
    strcpy(first.reason, "ok");
    strcpy(first.name, "local.pcap");
    hs_artifact_location_t invalid_source = first;
    invalid_source.source = (hs_artifact_source_t)-1;
    assert(hs_artifact_catalog_add(&catalog, &invalid_source, &asset_index) ==
           HS_ARTIFACT_CATALOG_INVALID);
    assert(hs_artifact_catalog_add(&catalog, &first, &asset_index) == HS_ARTIFACT_CATALOG_ADDED);
    assert(asset_index == 0 && catalog.asset_count == 1);

    hs_artifact_location_t second_local = first;
    second_local.size = 101;
    second_local.crc32 = 0x87654321;
    strcpy(second_local.name, "another-local.pcap");
    assert(hs_artifact_catalog_add(&catalog, &second_local, &asset_index) ==
           HS_ARTIFACT_CATALOG_ADDED);
    assert(catalog.asset_count == 2);

    hs_artifact_location_t exact = first;
    exact.source = HS_ARTIFACT_SOURCE_USB;
    strcpy(exact.name, "remote-renamed.pcap");
    assert(hs_artifact_catalog_add(&catalog, &exact, &asset_index) == HS_ARTIFACT_CATALOG_MERGED);
    assert(catalog.asset_count == 2 && catalog.assets[0].location_count == 2);

    hs_artifact_location_t provisional = {0};
    provisional.source = HS_ARTIFACT_SOURCE_GROVE;
    provisional.scope = HS_ARTIFACT_SCOPE_HANDSHAKES;
    provisional.snapshot = 3;
    provisional.entry = 7;
    provisional.size = 100;
    strcpy(provisional.name, "same-name.pcap");
    strcpy(provisional.reason, "cache_stale");
    assert(hs_artifact_catalog_add(&catalog, &provisional, &asset_index) ==
           HS_ARTIFACT_CATALOG_ADDED);
    hs_artifact_location_t another = provisional;
    another.source = HS_ARTIFACT_SOURCE_MBUS;
    assert(hs_artifact_catalog_add(&catalog, &another, &asset_index) ==
           HS_ARTIFACT_CATALOG_ADDED);
    assert(catalog.asset_count == 4); /* never merge remote provisional by name/size */
    assert(hs_artifact_catalog_add(&catalog, &provisional, &asset_index) ==
           HS_ARTIFACT_CATALOG_UPDATED);
    assert(catalog.asset_count == 4);

    provisional.crc32_known = true;
    provisional.crc32 = first.crc32;
    provisional.size = first.size;
    assert(hs_artifact_catalog_add(&catalog, &provisional, &asset_index) ==
           HS_ARTIFACT_CATALOG_UPDATED);
    assert(asset_index == 0 && catalog.assets[0].location_count == 3);

    hs_artifact_location_t same_name_other_scope = provisional;
    same_name_other_scope.crc32_known = false;
    same_name_other_scope.scope = HS_ARTIFACT_SCOPE_PCAPS;
    same_name_other_scope.snapshot = 4;
    assert(hs_artifact_catalog_add(&catalog, &same_name_other_scope, &asset_index) ==
           HS_ARTIFACT_CATALOG_ADDED);
    assert(catalog.asset_count == 4);

    hs_artifact_catalog_t bounded = {0};
    hs_artifact_location_t unique = first;
    unique.crc32_known = false;
    for (size_t i = 0; i < HS_ARTIFACT_CATALOG_MAX_ASSETS; ++i) {
        snprintf(unique.name, sizeof(unique.name), "unique-%zu.pcap", i);
        assert(hs_artifact_catalog_add(&bounded, &unique, &asset_index) ==
               HS_ARTIFACT_CATALOG_ADDED);
    }
    strcpy(unique.name, "overflow.pcap");
    assert(hs_artifact_catalog_add(&bounded, &unique, &asset_index) ==
           HS_ARTIFACT_CATALOG_FULL);

    hs_artifact_catalog_t locations = {0};
    for (int source = HS_ARTIFACT_SOURCE_LOCAL; source <= HS_ARTIFACT_SOURCE_MBUS; ++source) {
        hs_artifact_location_t same = first;
        same.source = (hs_artifact_source_t)source;
        snprintf(same.name, sizeof(same.name), "copy-%d.pcap", source);
        hs_artifact_catalog_result_t result =
            hs_artifact_catalog_add(&locations, &same, &asset_index);
        assert(result == (source == HS_ARTIFACT_SOURCE_LOCAL ? HS_ARTIFACT_CATALOG_ADDED
                                                             : HS_ARTIFACT_CATALOG_MERGED));
    }
    hs_artifact_location_t fifth = first;
    strcpy(fifth.name, "fifth-copy.pcap");
    assert(hs_artifact_catalog_add(&locations, &fifth, &asset_index) ==
           HS_ARTIFACT_CATALOG_FULL);
}

static void synchronization_identity(void)
{
    hs_artifact_location_t grove = {0};
    grove.source = HS_ARTIFACT_SOURCE_GROVE;
    grove.scope = HS_ARTIFACT_SCOPE_HANDSHAKES;
    grove.size = 11202;
    grove.format = HS_ARTIFACT_FORMAT_PCAP;
    strcpy(grove.name, "Nt_Holiday.pcap");

    hs_artifact_location_t usb = grove;
    usb.source = HS_ARTIFACT_SOURCE_USB;
    assert(hs_artifact_locations_same_content(&grove, &usb));

    usb.size++;
    assert(!hs_artifact_locations_same_content(&grove, &usb));
    usb.size = grove.size;

    strcpy(usb.name, "renamed.pcap");
    assert(!hs_artifact_locations_same_content(&grove, &usb));

    grove.crc32_known = true;
    grove.crc32 = 0x57BF22AF;
    usb.crc32_known = true;
    usb.crc32 = grove.crc32;
    assert(hs_artifact_locations_same_content(&grove, &usb));

    usb.crc32 = 0xE4FBC4A9;
    strcpy(usb.name, grove.name);
    assert(!hs_artifact_locations_same_content(&grove, &usb));

    usb.crc32_known = false;
    usb.format = HS_ARTIFACT_FORMAT_HCCAPX;
    assert(!hs_artifact_locations_same_content(&grove, &usb));
}

int main(void)
{
    parser_cases();
    fragmented_stream();
    correlation_and_paging();
    legacy_and_catalog();
    synchronization_identity();
    puts("hs_artifact_inventory_test: PASS");
    return 0;
}
