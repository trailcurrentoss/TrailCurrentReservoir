#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "can_common.h"
#include "wifi_config.h"
#include "ota.h"
#include "discovery.h"

static const char *TAG = "reservoir";

// Waveshare ESP32-S3-RS485-CAN pin assignments
#define CAN_RX_PIN           16
#define CAN_TX_PIN           15

// CAN protocol IDs
#define CAN_ID_OTA               0x00
#define CAN_ID_WIFI_CONFIG       0x01
#define CAN_ID_DISCOVERY_TRIGGER 0x02
#define CAN_ID_WATER_TANK_LEVELS 0x3E

// CAN transmit period in milliseconds
#define CAN_STATUS_PERIOD_MS 1000
#define TX_PROBE_INTERVAL_MS 2000  // slow probe when no peers detected

// Sensor poll interval
#define SENSOR_POLL_MS       500

// ---------------------------------------------------------------------------
// Water level sensor GPIO assignments
// Each tank has 4 contactless sensors at 25%, 50%, 75%, and 100% levels.
// Sensors output HIGH when water is detected (active high, pull-down).
// ---------------------------------------------------------------------------

#define NUM_TANKS   3
#define NUM_SENSORS 4

typedef struct {
    const char *name;
    gpio_num_t pins[NUM_SENSORS];  // index 0=25%, 1=50%, 2=75%, 3=100%
} tank_config_t;

static const tank_config_t tanks[NUM_TANKS] = {
    { "fresh", { GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7  } },
    { "grey",  { GPIO_NUM_8,  GPIO_NUM_9,  GPIO_NUM_10, GPIO_NUM_11 } },
    { "black", { GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_43 } },
};

// Current tank levels as percentages (0, 25, 50, 75, 100)
static volatile uint8_t tank_levels[NUM_TANKS];

// ---------------------------------------------------------------------------
// GPIO setup — configure all sensor pins as inputs with pull-down
// ---------------------------------------------------------------------------

static void sensors_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 0,
    };

    for (int t = 0; t < NUM_TANKS; t++) {
        for (int s = 0; s < NUM_SENSORS; s++) {
            io_conf.pin_bit_mask |= (1ULL << tanks[t].pins[s]);
        }
    }

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    for (int t = 0; t < NUM_TANKS; t++) {
        ESP_LOGI(TAG, "Tank '%s' sensors: GPIO %d/%d/%d/%d (25%%/50%%/75%%/100%%)",
                 tanks[t].name,
                 tanks[t].pins[0], tanks[t].pins[1],
                 tanks[t].pins[2], tanks[t].pins[3]);
    }
}

// ---------------------------------------------------------------------------
// Read sensors and convert to percentage
// Water rises from bottom up, so the level equals the highest triggered
// sensor. e.g., if 25% and 50% are high but 75% is low, level = 50%.
// ---------------------------------------------------------------------------

static uint8_t read_tank_level(const tank_config_t *tank)
{
    uint8_t level = 0;

    for (int s = 0; s < NUM_SENSORS; s++) {
        if (gpio_get_level(tank->pins[s])) {
            level = (s + 1) * 25;  // 25, 50, 75, or 100
        }
    }

    return level;
}

static void poll_sensors(void)
{
    for (int t = 0; t < NUM_TANKS; t++) {
        tank_levels[t] = read_tank_level(&tanks[t]);
    }
}

// ---------------------------------------------------------------------------
// TWAI (CAN) task -- runs independently on core 1
// ---------------------------------------------------------------------------


static void twai_task(void *arg)
{
    // Configure alerts BEFORE any bus activity so no error transitions are missed.
    twai_reconfigure_alerts(CAN_COMMON_ALERTS, NULL);

    // Alerts armed — version broadcast TX failure is caught by the state machine.
    can_common_version_broadcast();

    typedef enum { TX_ACTIVE, TX_PROBING } tx_state_t;
    bool bus_off = false;
    tx_state_t tx_state = TX_ACTIVE;
    int tx_fail_count = 0;
    const int TX_FAIL_THRESHOLD = 3;
    int64_t last_tx_us = 0;
    const int64_t tx_period_us = CAN_STATUS_PERIOD_MS * 1000LL;
    const int64_t tx_probe_period_us = TX_PROBE_INTERVAL_MS * 1000LL;

    while (1) {
        uint32_t triggered;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(CAN_STATUS_PERIOD_MS));

        // Bus-off: stop transmitting, initiate recovery
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            bus_off = true;
            twai_initiate_recovery();
            // No continue — fall through so RX_DATA in the same poll is still processed.
        }

        // Bus recovered: restart the driver
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
            bus_off = false;
            tx_fail_count = 0;
            tx_state = TX_PROBING;
        }

        if (triggered & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "TWAI error passive (no peers ACKing?)");
        }
        if (triggered & TWAI_ALERT_TX_FAILED) {
            if (tx_state == TX_ACTIVE) {
                tx_fail_count++;
                if (tx_fail_count >= TX_FAIL_THRESHOLD) {
                    tx_state = TX_PROBING;
                    ESP_LOGW(TAG, "TWAI no peers detected, entering slow probe");
                }
            }
        }
        if (triggered & TWAI_ALERT_TX_SUCCESS) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                can_common_version_broadcast();
                ESP_LOGI(TAG, "TWAI probe ACK'd, peer detected, resuming normal TX");
            }
            tx_fail_count = 0;
        }

        // Drain received messages and dispatch
        if (triggered & TWAI_ALERT_RX_DATA) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                can_common_version_broadcast();
                ESP_LOGI(TAG, "TWAI peer detected via RX, resuming normal TX");
            }
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                if (msg.rtr) continue;

                switch (msg.identifier) {
                case CAN_ID_OTA:
                    if (msg.data_length_code >= 3) {
                        ota_handle_trigger(msg.data, msg.data_length_code);
                    }
                    break;

                case CAN_ID_WIFI_CONFIG:
                    if (msg.data_length_code >= 1) {
                        wifi_config_handle_can(msg.data, msg.data_length_code);
                    }
                    break;

                case CAN_ID_DISCOVERY_TRIGGER:
                    discovery_handle_trigger();
                    break;

                default:
                    break;
                }
            }
        }

        // Check wifi config timeout
        wifi_config_check_timeout();

        // Periodic transmit — water tank levels as percentages
        int64_t now_us = esp_timer_get_time();
        int64_t effective_period = (tx_state == TX_PROBING) ? tx_probe_period_us : tx_period_us;
        if (!bus_off && (now_us - last_tx_us >= effective_period)) {
            last_tx_us = now_us;

            // Message 0x3E: WaterTankLevels
            //   Byte 0: Fresh water level (0-100%)
            //   Byte 1: Grey water level  (0-100%)
            //   Byte 2: Black water level (0-100%)
            twai_message_t m = {
                .identifier = CAN_ID_WATER_TANK_LEVELS,
                .data_length_code = 3,
                .data = {
                    tank_levels[0],  // fresh
                    tank_levels[1],  // grey
                    tank_levels[2],  // black
                }
            };

            twai_transmit(&m, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Main application
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "=== TrailCurrent Reservoir ===");
    ESP_LOGI(TAG, "Water Tank Level Monitor (contactless sensors to CAN)");

    // Initialize NVS and load WiFi credentials
    ESP_ERROR_CHECK(wifi_config_init());

    char ssid[33] = {0};
    char password[64] = {0};
    if (wifi_config_load(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "WiFi credentials loaded from NVS");
    } else {
        ESP_LOGI(TAG, "No WiFi credentials — OTA disabled until provisioned via CAN");
    }

    // Initialize discovery and OTA (must be after wifi_config_init)
    discovery_init();
    ota_init();

    // Initialize water level sensor GPIOs
    sensors_init();

    ESP_LOGI(TAG, "Hostname: %s", wifi_config_get_hostname());

    // CAN runs in its own task so bus errors never block sensor polling
    ESP_ERROR_CHECK(can_common_init(CAN_TX_PIN, CAN_RX_PIN));
    xTaskCreatePinnedToCore(twai_task, "twai", 4096, NULL, 5, NULL, 1);

    // Main task: poll water level sensors periodically
    uint8_t prev_levels[NUM_TANKS] = {0xFF, 0xFF, 0xFF};  // force initial log
    int log_counter = 0;
    while (1) {
        poll_sensors();

        // Log on change or every 10 seconds
        bool changed = false;
        for (int t = 0; t < NUM_TANKS; t++) {
            if (tank_levels[t] != prev_levels[t]) {
                changed = true;
                prev_levels[t] = tank_levels[t];
            }
        }
        if (changed || ++log_counter >= (10000 / SENSOR_POLL_MS)) {
            ESP_LOGI(TAG, "Levels: fresh=%d%% grey=%d%% black=%d%%",
                     tank_levels[0], tank_levels[1], tank_levels[2]);
            log_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
    }
}
