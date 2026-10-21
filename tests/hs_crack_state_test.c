#define _POSIX_C_SOURCE 200809L
#include "hs_crack_state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void join_path(char *out, size_t capacity, const char *dir,
                      const char *name)
{
    int written = snprintf(out, capacity, "%s/%s", dir, name);
    assert(written > 0 && (size_t)written < capacity);
}

static char *read_all(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t)length + 1U, 1U);
    assert(text);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    return text;
}

static void quoted_round_trip_and_partial_merge(const char *csv,
                                                 const char *tmp)
{
    const char *filename = "cap, \"one\".pcap";
    const char *ssid = "SSID, \"lab\"\nfloor 2";
    const char *password = "pa,ss\"word\r\nline";
    assert(hs_crack_state_upsert_file(csv, tmp, filename, ssid, 1234,
                                      "found", "none", password));
    assert(hs_crack_state_upsert_file(csv, tmp, filename, NULL, 0,
                                      NULL, "notfound", NULL));

    char *text = read_all(csv);
    assert(strcmp(text,
                  "\"cap, \"\"one\"\".pcap\",\"SSID, \"\"lab\"\"\nfloor 2\",1234,"
                  "\"found\",\"notfound\",\"pa,ss\"\"word\r\nline\"\n") == 0);
    free(text);
    assert(access(tmp, F_OK) != 0);
}

static void legacy_rows_are_accepted_and_normalized(const char *csv,
                                                     const char *tmp)
{
    FILE *file = fopen(csv, "wb");
    assert(file);
    assert(fputs("legacy.pcap,Old SSID,42,notfound,none,\n"
                 "other.pcap,Other,7,none,found,secret\n", file) >= 0);
    assert(fclose(file) == 0);

    assert(hs_crack_state_upsert_file(csv, tmp, "legacy.pcap", "New, SSID",
                                      0, NULL, "found", NULL));
    char *text = read_all(csv);
    assert(strcmp(text,
                  "\"other.pcap\",\"Other\",7,\"none\",\"found\",\"secret\"\n"
                  "\"legacy.pcap\",\"New, SSID\",42,\"notfound\",\"found\",\"\"\n") == 0);
    free(text);
}

static void malformed_rows_are_not_destroyed(const char *csv,
                                              const char *tmp)
{
    FILE *file = fopen(csv, "wb");
    assert(file);
    assert(fputs("broken,row\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(hs_crack_state_upsert_file(csv, tmp, "fresh.pcap", "Fresh", 9,
                                      "none", "none", ""));
    char *text = read_all(csv);
    assert(strstr(text, "broken,row\n") == text);
    assert(strstr(text, "\"fresh.pcap\",\"Fresh\",9") != NULL);
    free(text);
}

static void interrupted_publish_recovers_the_backup(const char *csv,
                                                     const char *tmp)
{
    assert(unlink(csv) == 0);
    char backup[300];
    int written = snprintf(backup, sizeof(backup), "%s.bak", csv);
    assert(written > 0 && (size_t)written < sizeof(backup));
    FILE *file = fopen(backup, "wb");
    assert(file);
    assert(fputs("saved.pcap,Saved,11,none,notfound,\n", file) >= 0);
    assert(fclose(file) == 0);

    assert(hs_crack_state_upsert_file(csv, tmp, "fresh.pcap", "Fresh", 12,
                                      "none", "none", ""));
    char *text = read_all(csv);
    assert(strstr(text, "\"saved.pcap\",\"Saved\",11") != NULL);
    assert(strstr(text, "\"fresh.pcap\",\"Fresh\",12") != NULL);
    free(text);
    assert(access(backup, F_OK) != 0);
}

int main(void)
{
    char dir[] = "/tmp/hs-crack-state-XXXXXX";
    assert(mkdtemp(dir));
    char csv[256], tmp[256];
    join_path(csv, sizeof(csv), dir, "crack_state.csv");
    join_path(tmp, sizeof(tmp), dir, "crack_state.csv.tmp");

    quoted_round_trip_and_partial_merge(csv, tmp);
    legacy_rows_are_accepted_and_normalized(csv, tmp);
    malformed_rows_are_not_destroyed(csv, tmp);
    interrupted_publish_recovers_the_backup(csv, tmp);

    assert(unlink(csv) == 0);
    assert(rmdir(dir) == 0);
    puts("hs_crack_state_test: PASS");
    return 0;
}
