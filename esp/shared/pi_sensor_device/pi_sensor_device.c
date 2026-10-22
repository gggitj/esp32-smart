#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "pi_sensor_device.h"

/* Bus 0 - BME280: temperature / humidity / pressure. */
#define BME_SDA_PIN 38
#define BME_SCL_PIN 39
/* Bus 1 - BH1750: ambient light. Separate pins so both sensors can be
 * verified at the same time with pluggable jumper wires (no shared pins). */
#define BH_SDA_PIN  4
#define BH_SCL_PIN  5

#define TIMEOUT_MS 100
static const char *TAG = "SENSOR_TEST";
static SemaphoreHandle_t s_sample_lock;
static pi_sensor_sample_t s_latest;
static bool s_sample_available;

static i2c_master_bus_handle_t bus_bme;
static i2c_master_bus_handle_t bus_bh;
static i2c_master_dev_handle_t bme;
static i2c_master_dev_handle_t bh;
static bool bme_ok;
static bool bh_ok;
static bool has_humidity;
static bool s_hum_ok;
static float s_temp;
static float s_hum;
static float s_press;
static uint16_t t1, p1;
static int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9;
static uint8_t h1, h3;
static int16_t h2, h4, h5;
static int8_t h6;

static uint16_t le16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static int16_t signed12(uint16_t v) { return (int16_t)((v & 0x800) ? (int32_t)v - 4096 : v); }

static esp_err_t bme_read_regs(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(bme, &reg, 1, out, n, TIMEOUT_MS);
}
static esp_err_t bme_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(bme, bytes, sizeof(bytes), TIMEOUT_MS);
}

static void scan_bus(i2c_master_bus_handle_t bus, const char *label)
{
    ESP_LOGI(TAG, "%s scan:", label);
    for (unsigned addr = 8; addr < 0x78; ++addr) {
        if (i2c_master_probe(bus, addr, 30) == ESP_OK) ESP_LOGI(TAG, "  ACK 0x%02X", addr);
    }
}

static bool find_bme280(void)
{
    const uint8_t addresses[] = {0x76, 0x77};
    for (unsigned i = 0; i < sizeof(addresses); ++i) {
        const esp_err_t probe = i2c_master_probe(bus_bme, addresses[i], TIMEOUT_MS);
        if (probe != ESP_OK) {
            if (probe == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "BME probe timeout SDA=%d SCL=%d; recovering I2C bus",
                         gpio_get_level(BME_SDA_PIN), gpio_get_level(BME_SCL_PIN));
                const esp_err_t reset = i2c_master_bus_reset(bus_bme);
                ESP_LOGW(TAG, "BME I2C bus recovery result=%s", esp_err_to_name(reset));
            }
            continue;
        }
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i], .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bus_bme, &cfg, &bme) != ESP_OK) continue;
        uint8_t id = 0;
        if (bme_read_regs(0xD0, &id, 1) == ESP_OK) {
            ESP_LOGI(TAG, "BME addr=0x%02X chip_id=0x%02X", addresses[i], id);
            if (id == 0x60 || id == 0x58 || id == 0x56 || id == 0x57) {
                has_humidity = id == 0x60;
                ESP_LOGI(TAG, "Detected %s",
                         has_humidity ? "BME280 (temperature + humidity + pressure)"
                                      : "BMP280 (temperature + pressure ONLY; NO humidity)");
                return true;
            }
            ESP_LOGW(TAG, "Unknown chip id 0x%02X on bus 0", id);
        }
        i2c_master_bus_rm_device(bme);
        bme = NULL;
    }
    return false;
}

/* Try one SDA/SCL candidate pair for the BH1750 (address 0x23 or 0x5C). */
static bool bh_try_pins(int sda, int scl)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_1, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) return false;
    const uint8_t addresses[] = {0x23, 0x5C};
    for (unsigned i = 0; i < sizeof(addresses); ++i) {
        if (i2c_master_probe(bus, addresses[i], 30) != ESP_OK) continue;
        i2c_device_config_t dcfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i], .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &dcfg, &dev) != ESP_OK) continue;
        uint8_t power_on = 0x01;
        if (i2c_master_transmit(dev, &power_on, 1, TIMEOUT_MS) == ESP_OK) {
            bh = dev;
            bus_bh = bus;
            ESP_LOGI(TAG, "Detected BH1750 at 0x%02X on SDA=GPIO%d SCL=GPIO%d", addresses[i], sda, scl);
            return true;
        }
        i2c_master_bus_rm_device(dev);
    }
    i2c_del_master_bus(bus);
    return false;
}

/* Search the plausible free header pairs, both polarities, so a swapped SDA/SCL
 * or a different pin choice still gets found. */
static bool find_bh1750(void)
{
    static const int pairs[][2] = {
        // Product wiring is fixed. Do not probe unrelated GPIOs in runtime.
        {BH_SDA_PIN, BH_SCL_PIN},
    };
    for (unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        if (bh_try_pins(pairs[i][0], pairs[i][1])) return true;
    }
    return false;
}

static bool read_bh1750(float *lux)
{
    if (!bh_ok) return false;
    uint8_t cmd = 0x20; /* one-time H-resolution mode 1, ~180 ms conversion */
    if (i2c_master_transmit(bh, &cmd, 1, TIMEOUT_MS) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(180));
    uint8_t raw[2] = {0};
    if (i2c_master_receive(bh, raw, sizeof(raw), TIMEOUT_MS) != ESP_OK) return false;
    uint16_t value = ((uint16_t)raw[0] << 8) | raw[1];
    *lux = (float)value / 1.2f;
    return true;
}

/* ===================================================================
 * MAX17043 battery fuel gauge on a software (bit-banged) I2C bus.
 *
 * Both hardware I2C controllers are already taken (BME280 on 38/39,
 * BH1750 on 4/5), so the gauge gets its own two ordinary GPIOs and the
 * levels are flipped in software. It is read once a second, which is far
 * slower than bit-banging can manage.
 *
 * Wiring: SDA -> GPIO6, SCL -> GPIO7, VCC/+ -> battery +, GND/- -> GND.
 * =================================================================== */
#define BAT_SDA_PIN 6
#define BAT_SCL_PIN 7
#define MAX17043_ADDR7 0x36

static void bat_delay(void) { esp_rom_delay_us(5); }
static void bat_scl_hi(void) { gpio_set_level(BAT_SCL_PIN, 1); bat_delay(); }
static void bat_scl_lo(void) { gpio_set_level(BAT_SCL_PIN, 0); bat_delay(); }
static void bat_sda_hi(void) { gpio_set_level(BAT_SDA_PIN, 1); bat_delay(); }
static void bat_sda_lo(void) { gpio_set_level(BAT_SDA_PIN, 0); bat_delay(); }
static int bat_sda_rd(void) { return gpio_get_level(BAT_SDA_PIN); }

static void battery_bus_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BAT_SDA_PIN) | (1ULL << BAT_SCL_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, /* open drain; pull-ups live on the module */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    bat_sda_hi();
    bat_scl_hi();
}

static void bat_start(void) { bat_sda_hi(); bat_scl_hi(); bat_sda_lo(); bat_scl_lo(); }
static void bat_stop(void) { bat_sda_lo(); bat_scl_hi(); bat_sda_hi(); }

/* returns 0 on ACK, non-zero on NACK */
static int bat_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; --i) {
        if (b & (1u << i)) bat_sda_hi(); else bat_sda_lo();
        bat_scl_hi();
        bat_scl_lo();
    }
    bat_sda_hi(); /* release SDA so the gauge can pull it low for ACK */
    bat_scl_hi();
    const int ack = bat_sda_rd();
    bat_scl_lo();
    return ack;
}

static uint8_t bat_read_byte(int send_ack)
{
    uint8_t b = 0;
    bat_sda_hi();
    for (int i = 7; i >= 0; --i) {
        bat_scl_hi();
        if (bat_sda_rd()) b |= (uint8_t)(1u << i);
        bat_scl_lo();
    }
    if (send_ack) bat_sda_lo(); else bat_sda_hi();
    bat_scl_hi();
    bat_scl_lo();
    bat_sda_hi();
    return b;
}

static bool max17043_read(uint8_t reg, uint16_t *out)
{
    bat_start();
    if (bat_write_byte(MAX17043_ADDR7 << 1)) { bat_stop(); return false; }
    if (bat_write_byte(reg)) { bat_stop(); return false; }
    bat_start();
    if (bat_write_byte((MAX17043_ADDR7 << 1) | 1)) { bat_stop(); return false; }
    const uint8_t hi = bat_read_byte(1);
    const uint8_t lo = bat_read_byte(0);
    bat_stop();
    *out = ((uint16_t)hi << 8) | lo;
    return true;
}

static bool max17043_write(uint8_t reg, uint16_t val)
{
    bat_start();
    if (bat_write_byte(MAX17043_ADDR7 << 1)) { bat_stop(); return false; }
    if (bat_write_byte(reg)) { bat_stop(); return false; }
    if (bat_write_byte((uint8_t)(val >> 8))) { bat_stop(); return false; }
    if (bat_write_byte((uint8_t)(val & 0xFF))) { bat_stop(); return false; }
    bat_stop();
    return true;
}

/* VCELL 0x02: 12-bit, 1.25 mV/LSB.  SOC 0x04: high byte = whole percent. */
static bool read_max17043(float *volts, int *percent)
{
    uint16_t vcell = 0;
    if (!max17043_read(0x02, &vcell)) return false;
    *volts = (float)((vcell >> 4) * 125u) / 100000.0f; /* mV -> V */
    /* No cell fitted (or the gauge is only parasitically powered through the
     * I2C pull-ups) reads 0 V. Treat that as "no battery", not as 0 %. */
    if (*volts < 2.0f) return false;
    uint16_t soc = 0;
    if (!max17043_read(0x04, &soc)) return false;
    *percent = (int)(soc >> 8);
    if (*percent > 100) *percent = 100;
    if (*volts > 4.5f) return false;
    return true;
}

static void max17043_quick_start(void)
{
    /* MODE register 0x06, 0x4000 = quick start (re-estimate state of charge) */
    if (max17043_write(0x06, 0x4000)) ESP_LOGI(TAG, "MAX17043 quick start issued");
    else ESP_LOGW(TAG, "MAX17043 not responding on SDA=GPIO%d SCL=GPIO%d",
                  BAT_SDA_PIN, BAT_SCL_PIN);
}

static esp_err_t initialize_sensor(void)
{
    ESP_RETURN_ON_ERROR(bme_write_reg(0xE0, 0xB6), TAG, "sensor reset failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t status = 0;
    for (unsigned i = 0; i < 20; ++i) {
        ESP_RETURN_ON_ERROR(bme_read_regs(0xF3, &status, 1), TAG, "status read failed");
        if (!(status & 1)) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (status & 1) return ESP_ERR_TIMEOUT;
    uint8_t c[26];
    ESP_RETURN_ON_ERROR(bme_read_regs(0x88, c, sizeof(c)), TAG, "calibration read failed");
    t1 = le16(c); t2 = (int16_t)le16(c + 2); t3 = (int16_t)le16(c + 4);
    p1 = le16(c + 6); p2 = (int16_t)le16(c + 8); p3 = (int16_t)le16(c + 10);
    p4 = (int16_t)le16(c + 12); p5 = (int16_t)le16(c + 14); p6 = (int16_t)le16(c + 16);
    p7 = (int16_t)le16(c + 18); p8 = (int16_t)le16(c + 20); p9 = (int16_t)le16(c + 22);
    if (!t1 || t1 == 0xffff || !p1 || p1 == 0xffff) return ESP_ERR_INVALID_RESPONSE;
    if (has_humidity) {
        h1 = c[25];
        uint8_t h[7];
        ESP_RETURN_ON_ERROR(bme_read_regs(0xE1, h, sizeof(h)), TAG, "humidity calibration failed");
        h2 = (int16_t)le16(h); h3 = h[2];
        h4 = signed12(((uint16_t)h[3] << 4) | (h[4] & 15));
        h5 = signed12(((uint16_t)h[5] << 4) | (h[4] >> 4)); h6 = (int8_t)h[6];
        ESP_RETURN_ON_ERROR(bme_write_reg(0xF2, 1), TAG, "humidity setup failed");
    }
    ESP_RETURN_ON_ERROR(bme_write_reg(0xF5, 0), TAG, "config failed");
    ESP_LOGI(TAG, "BME280 calibration loaded (forced mode, 1 sample/s).");
    return ESP_OK;
}

static esp_err_t sample(void)
{
    ESP_RETURN_ON_ERROR(bme_write_reg(0xF4, 0x25), TAG, "trigger failed");
    vTaskDelay(pdMS_TO_TICKS(30));
    uint8_t status = 0;
    for (unsigned i = 0; i < 20; ++i) {
        ESP_RETURN_ON_ERROR(bme_read_regs(0xF3, &status, 1), TAG, "status failed");
        if (!(status & 8)) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (status & 8) return ESP_ERR_TIMEOUT;
    uint8_t d[8] = {0};
    ESP_RETURN_ON_ERROR(bme_read_regs(0xF7, d, has_humidity ? 8 : 6), TAG, "measurement read failed");
    uint32_t raw_p = ((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4);
    uint32_t raw_t = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4);
    if (raw_t == 0x80000 || raw_p == 0x80000) return ESP_ERR_INVALID_RESPONSE;
    /* Bosch floating-point compensation, using this chip's factory coefficients. */
    double v1 = (raw_t / 16384.0 - t1 / 1024.0) * t2;
    double v2 = (raw_t / 131072.0 - t1 / 8192.0);
    v2 = v2 * v2 * t3;
    double fine = v1 + v2;
    double temperature = fine / 5120.0;
    v1 = fine / 2.0 - 64000.0;
    v2 = v1 * v1 * p6 / 32768.0;
    v2 += v1 * p5 * 2.0;
    v2 = v2 / 4.0 + p4 * 65536.0;
    v1 = (p3 * v1 * v1 / 524288.0 + p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * p1;
    if (v1 == 0.0) return ESP_ERR_INVALID_RESPONSE;
    double pressure = 1048576.0 - raw_p;
    pressure = (pressure - v2 / 4096.0) * 6250.0 / v1;
    v1 = p9 * pressure * pressure / 2147483648.0;
    v2 = pressure * p8 / 32768.0;
    pressure += (v1 + v2 + p7) / 16.0;
    if (has_humidity) {
        uint16_t raw_h = ((uint16_t)d[6] << 8) | d[7];
        if (raw_h == 0x8000) return ESP_ERR_INVALID_RESPONSE;
        double h = fine - 76800.0;
        h = (raw_h - (h4 * 64.0 + h5 / 16384.0 * h)) *
            (h2 / 65536.0 * (1.0 + h6 / 67108864.0 * h * (1.0 + h3 / 67108864.0 * h)));
        h *= (1.0 - h1 * h / 524288.0);
        if (h < 0) h = 0;
        if (h > 100) h = 100;
        s_hum = (float)h;
        s_hum_ok = true;
    } else {
        s_hum_ok = false;
    }
    s_temp = (float)temperature;
    s_press = (float)(pressure / 100.0);
    return ESP_OK;
}

static void sensor_task(void *arg)
{
    (void)arg;
    i2c_master_bus_config_t cfg_bme = {
        .i2c_port = I2C_NUM_0, .sda_io_num = BME_SDA_PIN, .scl_io_num = BME_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg_bme, &bus_bme) != ESP_OK) {
        ESP_LOGE(TAG, "BME I2C bus unavailable; communication remains running");
        vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "Sensor diagnostic v5: BME280 SDA=GPIO%d SCL=GPIO%d | BH1750 SDA=GPIO%d SCL=GPIO%d | MAX17043 sw-I2C SDA=GPIO%d SCL=GPIO%d",
             BME_SDA_PIN, BME_SCL_PIN, BH_SDA_PIN, BH_SCL_PIN, BAT_SDA_PIN, BAT_SCL_PIN);

    battery_bus_init();
    max17043_quick_start();

    bool bme_ready = false;
    bool bh_scanned_once = false;
    TickType_t last_bh_scan = 0;
    for (;;) {
        if (!bme_ok && find_bme280()) {
            bme_ok = true;
            bme_ready = initialize_sensor() == ESP_OK;
            if (!bme_ready) {
                ESP_LOGE(TAG, "BME280 calibration failed; retrying");
                i2c_master_bus_rm_device(bme); bme = NULL; bme_ok = false;
                (void)i2c_master_bus_reset(bus_bme);
            }
        }
        if (!bh_ok) {
            TickType_t now = xTaskGetTickCount();
            if (!bh_scanned_once || (now - last_bh_scan) >= pdMS_TO_TICKS(3000)) {
                bh_scanned_once = true;
                last_bh_scan = now;
                if (find_bh1750()) bh_ok = true;
            }
        }
        if (!bme_ok && !bh_ok) {
            xSemaphoreTake(s_sample_lock, portMAX_DELAY);
            s_latest = (pi_sensor_sample_t){0}; s_sample_available = true;
            xSemaphoreGive(s_sample_lock);
            scan_bus(bus_bme, "Bus0 (BME280: SDA38/SCL39)");
            ESP_LOGW(TAG, "No supported sensor found; rescan in 3 s. For BH1750 check VCC/GND and SDA/SCL pins.");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        bool bme_reading = false;
        if (bme_ok && bme_ready) {
            esp_err_t err = sample();
            if (err == ESP_OK) {
                bme_reading = true;
            } else {
                ESP_LOGE(TAG, "BME280 read failed: %s", esp_err_to_name(err));
                i2c_master_bus_rm_device(bme);
                bme = NULL;
                bme_ok = false;
                bme_ready = false;
                (void)i2c_master_bus_reset(bus_bme);
            }
        }

        float lux = 0.0f;
        bool lux_ok = read_bh1750(&lux);
        if (bh_ok && !lux_ok) {
            ESP_LOGW(TAG, "BH1750 read failed; will rescan");
            i2c_master_bus_rm_device(bh);
            i2c_del_master_bus(bus_bh);
            bh = NULL;
            bus_bh = NULL;
            bh_ok = false;
        }

        float batt_v = 0.0f;
        int batt_pct = 0;
        const bool batt_ok = read_max17043(&batt_v, &batt_pct);

        const pi_sensor_sample_t result = {
            .temperature_valid = bme_reading, .pressure_valid = bme_reading,
            .humidity_valid = bme_reading && s_hum_ok, .light_valid = lux_ok,
            .battery_valid = batt_ok,
            .temperature_c = s_temp, .humidity_rh = s_hum,
            .pressure_hpa = s_press, .light_lux = lux,
            .battery_volts = batt_v, .battery_percent = batt_pct,
            .sample_time_us = esp_timer_get_time(),
        };
        xSemaphoreTake(s_sample_lock, portMAX_DELAY);
        s_latest = result; s_sample_available = true;
        xSemaphoreGive(s_sample_lock);
        ESP_LOGI(TAG, "sample BME=%d humidity=%d BH=%d BAT=%d T=%.2f RH=%.2f P=%.2f lux=%.1f batt=%.2fV/%d%%",
                 bme_reading, result.humidity_valid, lux_ok, batt_ok,
                 s_temp, s_hum, s_press, lux, batt_ok ? batt_v : 0.0f, batt_ok ? batt_pct : 0);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t pi_sensor_device_start(void)
{
    if (s_sample_lock) return ESP_ERR_INVALID_STATE;
    s_sample_lock = xSemaphoreCreateMutex();
    if (!s_sample_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sensor_task, "pi_sensors", 8192, NULL, 3, NULL) != pdPASS) {
        vSemaphoreDelete(s_sample_lock); s_sample_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool pi_sensor_device_latest(pi_sensor_sample_t *out)
{
    if (!out || !s_sample_lock) return false;
    xSemaphoreTake(s_sample_lock, portMAX_DELAY);
    *out = s_latest;
    const bool available = s_sample_available;
    xSemaphoreGive(s_sample_lock);
    return available;
}
