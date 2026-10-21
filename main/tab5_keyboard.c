#include "tab5_keyboard.h"
#include "tab5_keyboard_protocol.h"
#include "app_keyboard.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>

static const char *TAG = "tab5_keyboard";
static i2c_master_bus_handle_t keyboard_bus;
static i2c_master_dev_handle_t keyboard_device;
static QueueHandle_t key_queue;
static atomic_bool connected;
static atomic_uint discard_requested;
static atomic_uint discard_completed;
static bool (*on_activity)(void);
static bool (*on_state)(bool);
static bool started;

static bool read_register(void *ctx, uint8_t reg, uint8_t *data, size_t size)
{
    (void)ctx;
    return i2c_master_transmit_receive(keyboard_device, &reg, 1, data, size, 20) == ESP_OK;
}

static bool write_register(void *ctx, uint8_t reg, uint8_t value)
{
    (void)ctx;
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(keyboard_device, data, sizeof(data), 20) == ESP_OK;
}

static void keyboard_task(void *arg)
{
    (void)arg;
    tab5_keyboard_protocol_t kb = {0};
    const tab5_keyboard_io_t io = {read_register, write_register, NULL};
    unsigned heartbeat = 0;
    for (;;) {
        if (!kb.connected) {
            /* A missing optional accessory must not log an I2C error every tick. */
            if (i2c_master_probe(keyboard_bus, 0x6d, 20) == ESP_OK &&
                tab5_keyboard_connect(&kb, &io)) {
                xQueueReset(key_queue);
                atomic_store(&connected, true);
                ESP_LOGI(TAG, "Tab5 Keyboard connected (I2C 0x6D, SDA 0, SCL 1)");
                heartbeat = 0;
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        /* Only the producer touches I2C. Flush its FIFO and any in-flight
         * event before acknowledging a newly revealed confirmation dialog. */
        unsigned discard_generation = atomic_load(&discard_requested);
        if (discard_generation != atomic_load(&discard_completed)) {
            if (write_register(NULL, 0x02, 0)) {
                xQueueReset(key_queue);
                atomic_store(&discard_completed, discard_generation);
            } else {
                kb.connected = false;
            }
        }
        if (++heartbeat >= 25) {
            uint8_t mode;
            /* A keyboard MCU reset may still ACK while reverting to Normal mode. */
            if (read_register(NULL, 0x10, &mode, 1) && mode != 2) kb.connected = false;
            heartbeat = 0;
        }
        for (unsigned i = 0; i < 8 && kb.connected; ++i) {
            if (uxQueueSpacesAvailable(key_queue) == 0) break;
            tab5_key_t key;
            if (!tab5_keyboard_poll(&kb, &io, &key)) break;
            xQueueSend(key_queue, &key, 0);
        }
        if (!kb.connected) {
            atomic_store(&connected, false);
            xQueueReset(key_queue);
            ESP_LOGI(TAG, "Tab5 Keyboard disconnected; using touch keyboard");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void keyboard_timer(lv_timer_t *timer)
{
    (void)timer;
    static bool ui_connected;
    bool current = atomic_load(&connected);
    if (ui_connected != current) {
        app_keyboard_set_connected(current);
        ui_connected = current;
    }
    if (on_state && on_state(current)) {
        atomic_fetch_add(&discard_requested, 1);
        xQueueReset(key_queue);
        return;
    }
    if (atomic_load(&discard_requested) != atomic_load(&discard_completed)) return;
    tab5_key_t key;
    for (unsigned i = 0; i < 8 && xQueueReceive(key_queue, &key, 0) == pdTRUE; ++i) {
        if (!current) continue;
        if (on_activity && !on_activity()) {
            xQueueReset(key_queue);
            break;
        }
        /* Do not let buffered input spill into a newly opened form. */
        if (app_keyboard_input(&key)) {
            xQueueReset(key_queue);
            break;
        }
    }
}

esp_err_t tab5_keyboard_init(bool (*activity_cb)(void), bool (*state_cb)(bool))
{
    if (started) return ESP_OK;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = GPIO_NUM_0,
        .scl_io_num = GPIO_NUM_1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    /* SYS I2C stays on controller 0 (GPIO31/32). Do not reuse the BSP's Grove
     * helper, whose controller 1 pin mapping is GPIO53/54, not Ext.Port1. */
    esp_err_t err = i2c_new_master_bus(&bus_config, &keyboard_bus);
    if (err != ESP_OK) return err;
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x6d,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(keyboard_bus, &device_config, &keyboard_device);
    if (err != ESP_OK) goto fail_bus;
    key_queue = xQueueCreate(32, sizeof(tab5_key_t));
    if (!key_queue) { err = ESP_ERR_NO_MEM; goto fail_device; }
    lv_timer_t *timer = lv_timer_create(keyboard_timer, 20, NULL);
    if (!timer) { err = ESP_ERR_NO_MEM; goto fail_queue; }
    on_activity = activity_cb;
    on_state = state_cb;
    if (xTaskCreate(keyboard_task, "tab5_keyboard", 4096, NULL, 3, NULL) != pdPASS) {
        lv_timer_delete(timer);
        err = ESP_ERR_NO_MEM;
        goto fail_queue;
    }
    started = true;
    return ESP_OK;

fail_queue:
    vQueueDelete(key_queue);
    key_queue = NULL;
fail_device:
    i2c_master_bus_rm_device(keyboard_device);
    keyboard_device = NULL;
fail_bus:
    i2c_del_master_bus(keyboard_bus);
    keyboard_bus = NULL;
    return err;
}
