/* Real LVGL editor with an in-memory NVS fault-injection backend. Static editor
 * internals are visible here to assert order, layout and lifecycle precisely. */
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/screens/tool_order_screen.c"

static uint8_t committed[2][TILE_ORDER_BLOB_SIZE], pending[2][TILE_ORDER_BLOB_SIZE];
static size_t committed_size[2], pending_size[2];
static bool fail_open, fail_commit;
static unsigned commits, saved_calls;
static uint8_t buffer[720 * 1280 * 4];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area; (void)pixels;
    lv_display_flush_ready(display);
}
static void screenshot(lv_display_t *display, unsigned rotation)
{
    char path[80]; snprintf(path, sizeof(path), ".codex-tmp/tool-order-%u.ppm", rotation);
    FILE *file = fopen(path, "wb"); assert(file);
    unsigned width = lv_display_get_horizontal_resolution(display);
    unsigned height = lv_display_get_vertical_resolution(display);
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (unsigned i = 0; i < width * height; ++i) {
        uint8_t rgb[] = {buffer[i*4+2], buffer[i*4+1], buffer[i*4]};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}
static unsigned key_index(const char *key) { return !strcmp(key, "monster") ? 0 : 1; }
esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(space, "tool_order"));
    if (fail_open) return ESP_FAIL;
    *handle = mode;
    memcpy(pending, committed, sizeof(pending));
    memcpy(pending_size, committed_size, sizeof(pending_size));
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h;
    unsigned i = key_index(key);
    if (!committed_size[i]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < committed_size[i]) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = committed_size[i];
    memcpy(out, committed[i], *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    (void)h;
    unsigned i = key_index(key);
    assert(size <= TILE_ORDER_BLOB_SIZE);
    memcpy(pending[i], data, size); pending_size[i] = size;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    unsigned i = key_index(key);
    if (!pending_size[i]) return ESP_ERR_NVS_NOT_FOUND;
    pending_size[i] = 0; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fail_commit) return ESP_FAIL;
    memcpy(committed, pending, sizeof(committed));
    memcpy(committed_size, pending_size, sizeof(committed_size));
    ++commits; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
static void saved(void) { ++saved_calls; }

static lv_indev_state_t pointer_state;
static lv_point_t pointer_point;
static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = pointer_state; data->point = pointer_point;
}
static void advance(unsigned ms)
{
    for (unsigned i = 0; i < ms; i += 10) { lv_tick_inc(10); lv_timer_handler(); }
}
static lv_point_t center(lv_obj_t *obj)
{
    lv_area_t area; lv_obj_get_coords(obj, &area);
    return (lv_point_t){(area.x1 + area.x2)/2, (area.y1 + area.y2)/2};
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(720, 1280);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
    tool_order_item_t items[16];
    uint8_t ids[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,32,33,34};
    const char *labels[] = {"WiFi Scan & Attack", "Global WiFi Attacks", "Compromised Data",
        "Deauth Detector", "Bluetooth", "Network Observer", "Karma", "Wardrive", "Anti-Surv",
        "Mesh Recon", "Sub-GHz", "NFC", "WiFi Analyzer", "WPA PSK Auditor", "Settings", "Ad Hoc Portal & Karma"};
    for (unsigned i = 0; i < 16; ++i)
        items[i] = (tool_order_item_t){ids[i], LV_SYMBOL_WIFI, labels[i], lv_color_hex(0x55bbff)};
    tool_order_screen_config_t config = {.background = lv_color_hex(0x111520),
        .card = lv_color_hex(0x242b39), .text = lv_color_white(), .accent = lv_color_hex(0xff55aa),
        .items = items, .item_count = 16, .saved = saved};
    tool_order_screen_show(&config);
    advance(30);
    assert(lv_obj_get_child_count(editor.grid) == 13);
    /* Actual pointer long-press then drag to a different tile. */
    pointer_point = center(editor.tiles[0]); pointer_state = LV_INDEV_STATE_PRESSED;
    advance(700);
    assert(editor.dragging == TO_WIFI_SCAN);
    pointer_point = center(editor.tiles[2]); advance(60);
    assert(editor.draft[0].ids[2] == TO_WIFI_SCAN);
    pointer_state = LV_INDEV_STATE_RELEASED; advance(60);
    assert(!editor.dragging && !editor.ghost && commits == 0);
    /* Rotation cancels only the active drag and keeps the draft. */
    unsigned heights[] = {720,1280,720,1280};
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        lv_display_set_rotation(display, (lv_display_rotation_t)((rotation + 1) % 4));
        advance(40); lv_obj_update_layout(editor.page);
        lv_obj_invalidate(editor.page); advance(40);
        screenshot(display, rotation);
        assert(editor.draft[0].ids[2] == TO_WIFI_SCAN);
        lv_area_t bounds; lv_obj_get_coords(editor.grid, &bounds);
        assert(bounds.y2 < (int)heights[rotation]);
        for (unsigned i = 0; i < 13; ++i) {
            lv_area_t tile; lv_obj_get_coords(editor.tiles[i], &tile);
            assert(tile.x1 >= bounds.x1 && tile.x2 <= bounds.x2);
        }
    }
    /* Edge scroll is clamped at both ends. No flash writes during dragging. */
    lv_obj_set_flex_grow(editor.grid, 0);
    lv_obj_set_height(editor.grid, 180);
    lv_obj_update_layout(editor.page);
    pointer_point = center(editor.tiles[0]); pointer_state = LV_INDEV_STATE_PRESSED;
    advance(700); assert(editor.dragging);
    lv_area_t grid_bounds; lv_obj_get_coords(editor.grid, &grid_bounds);
    pointer_point.y = grid_bounds.y1 + 2; advance(500);
    assert(lv_obj_get_scroll_y(editor.grid) == 0);
    pointer_point.y = grid_bounds.y2 - 2; advance(2000);
    assert(lv_obj_get_scroll_y(editor.grid) > 0 && lv_obj_get_scroll_bottom(editor.grid) >= 0);
    pointer_state = LV_INDEV_STATE_RELEASED; advance(60);
    lv_obj_scroll_to_y(editor.grid, 0, LV_ANIM_OFF); advance(30);
    /* 0 -> 180 keeps width/height; still cancel the active pointer gesture. */
    pointer_point = center(editor.tiles[0]); pointer_state = LV_INDEV_STATE_PRESSED;
    advance(700); assert(editor.dragging);
    tile_order_t before_rotation = editor.draft[0];
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180); advance(60);
    assert(!editor.dragging && !editor.ghost);
    assert(!memcmp(&before_rotation, &editor.draft[0], sizeof(before_rotation)));
    pointer_state = LV_INDEV_STATE_RELEASED; advance(60);
    lv_obj_set_flex_grow(editor.grid, 1);
    lv_obj_update_layout(editor.page);
    /* Cancel does not persist preview. */
    cancel(NULL); assert(commits == 0 && !editor.page);
    tool_order_screen_show(&config); assert(editor.draft[0].ids[0] == TO_WIFI_SCAN);
    /* Keyboard activation uses the same reachable controls as touch. */
    tab5_key_t key = {.kind = TAB5_KEY_TEXT, .character = '2'};
    app_keyboard_navigation_input(&key);
    assert(editor.group == TO_INTERNAL);
    key.character = '3'; app_keyboard_navigation_input(&key);
    assert(editor.selected == TO_AUDITOR);
    key.character = '1'; app_keyboard_navigation_input(&key);
    assert(editor.group == TO_MONSTER);
    editor.selected = TO_WARDRIVE; move_to(0);
    fail_commit = true; save(NULL);
    assert(editor.page && editor.dirty[0] && saved_calls == 0);
    fail_commit = false; save(NULL);
    assert(!editor.page && commits == 1 && saved_calls == 1);
    tool_order_screen_show(&config); assert(editor.draft[0].ids[0] == TO_WARDRIVE);
    reset_button(NULL); save(NULL);
    assert(!committed_size[0]);
    tool_order_screen_show(&config); assert(editor.draft[0].ids[0] == TO_WIFI_SCAN);
    cancel(NULL);
    tile_order_t fallback;
    fail_open = true;
    assert(tile_order_load(TO_MONSTER, &fallback) == ESP_FAIL && fallback.count == 13);
    fail_open = false;
    committed[0][0] = 255; committed_size[0] = 2;
    assert(tile_order_load(TO_MONSTER, &fallback) == ESP_ERR_INVALID_SIZE && fallback.count == 13);
    tool_order_screen_show(&config);
    assert(editor.dirty[0]);
    save(NULL);
    assert(!editor.page && tile_order_load(TO_MONSTER, &fallback) == ESP_OK);
    lv_indev_delete(pointer);
    lv_display_delete(display);
    puts("tool_order UI/NVS: all tests passed (4 orientations, pointer drag, cancel, save, reset, faults)");
    return 0;
}
