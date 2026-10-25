#include "rc522.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    REG_COMMAND = 0x01, REG_COM_IRQ = 0x04, REG_ERROR = 0x06,
    REG_FIFO_DATA = 0x09, REG_FIFO_LEVEL = 0x0a, REG_CONTROL = 0x0c,
    REG_BIT_FRAMING = 0x0d, REG_COLL = 0x0e, REG_MODE = 0x11,
    REG_TX_MODE = 0x12, REG_RX_MODE = 0x13, REG_TX_CONTROL = 0x14,
    REG_TX_ASK = 0x15, REG_MOD_WIDTH = 0x24, REG_T_MODE = 0x2a,
    REG_T_PRESCALER = 0x2b, REG_T_RELOAD_H = 0x2c,
    REG_T_RELOAD_L = 0x2d, REG_VERSION = 0x37,
};

#define TRY(call) do { esp_err_t err_ = (call); if (err_ != ESP_OK) return err_; } while (0)

static TickType_t ticks_at_least_one(unsigned ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

static esp_err_t write_reg(rc522_t *reader, uint8_t reg, uint8_t value)
{
    spi_transaction_t transfer = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = {(uint8_t)((reg << 1) & 0x7e), value},
    };
    return spi_device_polling_transmit(reader->spi, &transfer);
}

static esp_err_t read_reg(rc522_t *reader, uint8_t reg, uint8_t *value)
{
    spi_transaction_t transfer = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = {(uint8_t)(0x80 | ((reg << 1) & 0x7e)), 0},
    };
    TRY(spi_device_polling_transmit(reader->spi, &transfer));
    *value = transfer.rx_data[1];
    return ESP_OK;
}

static esp_err_t check_reader(rc522_t *reader)
{
    uint8_t version;
    TRY(read_reg(reader, REG_VERSION, &version));
    if (version == 0 || version == 0xff) return ESP_ERR_INVALID_RESPONSE;
    reader->version = version; /* Includes compatible clones. */
    return ESP_OK;
}

esp_err_t rc522_open(rc522_t *reader, int ss, int sck, int mosi, int miso, int rst)
{
    if (!reader) return ESP_ERR_INVALID_ARG;
    memset(reader, 0, sizeof(*reader));
    reader->reset_gpio = rst;
    const spi_bus_config_t bus = {
        .mosi_io_num = mosi, .miso_io_num = miso, .sclk_io_num = sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .data4_io_num = -1, .data5_io_num = -1,
        .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = 64,
    };
    TRY(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_DISABLED));
    const spi_device_interface_config_t device = {
        .clock_speed_hz = 1000000, .mode = 0,
        .spics_io_num = ss, .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(SPI3_HOST, &device, &reader->spi);
    if (err != ESP_OK) {
        spi_bus_free(SPI3_HOST);
        return err;
    }
    const gpio_config_t reset = {
        .pin_bit_mask = UINT64_C(1) << rst, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&reset);
    if (err != ESP_OK) {
        spi_bus_remove_device(reader->spi);
        spi_bus_free(SPI3_HOST);
        reader->spi = NULL;
    }
    return err;
}

esp_err_t rc522_reset(rc522_t *reader)
{
    if (!reader || !reader->spi) return ESP_ERR_INVALID_STATE;
    TRY(gpio_set_level(reader->reset_gpio, 0));
    vTaskDelay(ticks_at_least_one(2));
    TRY(gpio_set_level(reader->reset_gpio, 1));
    vTaskDelay(ticks_at_least_one(50));
    TRY(check_reader(reader));
    TRY(write_reg(reader, REG_COMMAND, 0x0f)); /* SoftReset */
    const int64_t deadline = esp_timer_get_time() + 100000;
    uint8_t command;
    do {
        vTaskDelay(ticks_at_least_one(1));
        TRY(read_reg(reader, REG_COMMAND, &command));
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
    } while (command & 0x10);
    TRY(check_reader(reader));
    TRY(write_reg(reader, REG_TX_MODE, 0));
    TRY(write_reg(reader, REG_RX_MODE, 0));
    TRY(write_reg(reader, REG_MOD_WIDTH, 0x26));
    /* Automatic timer, 40 kHz tick, 25 ms RF-response deadline. */
    TRY(write_reg(reader, REG_T_MODE, 0x80));
    TRY(write_reg(reader, REG_T_PRESCALER, 0xa9));
    TRY(write_reg(reader, REG_T_RELOAD_H, 0x03));
    TRY(write_reg(reader, REG_T_RELOAD_L, 0xe8));
    TRY(write_reg(reader, REG_TX_ASK, 0x40)); /* 100% ASK */
    TRY(write_reg(reader, REG_MODE, 0x3d));
    TRY(write_reg(reader, REG_COLL, 0x80));
    uint8_t antenna;
    TRY(read_reg(reader, REG_TX_CONTROL, &antenna));
    TRY(write_reg(reader, REG_TX_CONTROL, antenna | 0x03));
    vTaskDelay(ticks_at_least_one(5));
    return ESP_OK;
}

static esp_err_t exchange(rc522_t *reader, const uint8_t *tx, size_t tx_len,
                          uint8_t tx_last_bits, uint8_t *rx, size_t capacity,
                          size_t *rx_len, uint8_t *rx_last_bits)
{
    if (!tx_len || tx_len > 64) return ESP_ERR_INVALID_ARG;
    *rx_len = 0;
    *rx_last_bits = 0;
    TRY(write_reg(reader, REG_COMMAND, 0));
    TRY(write_reg(reader, REG_COM_IRQ, 0x7f));
    TRY(write_reg(reader, REG_FIFO_LEVEL, 0x80));
    for (size_t i = 0; i < tx_len; ++i) TRY(write_reg(reader, REG_FIFO_DATA, tx[i]));
    TRY(write_reg(reader, REG_BIT_FRAMING, tx_last_bits));
    TRY(write_reg(reader, REG_COMMAND, 0x0c)); /* Transceive */
    TRY(write_reg(reader, REG_BIT_FRAMING, tx_last_bits | 0x80));

    const int64_t deadline = esp_timer_get_time() + 40000;
    esp_err_t result = ESP_ERR_TIMEOUT;
    for (;;) {
        uint8_t irq, errors, count, control;
        result = read_reg(reader, REG_COM_IRQ, &irq);
        if (result != ESP_OK) break;
        if (irq & 0x30) { /* RxIRq or IdleIRq */
            result = read_reg(reader, REG_ERROR, &errors);
            if (result != ESP_OK) break;
            if (errors & 0x1f) { result = ESP_ERR_INVALID_RESPONSE; break; }
            result = read_reg(reader, REG_FIFO_LEVEL, &count);
            if (result != ESP_OK) break;
            if (!count || count > capacity || count > 64) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            for (size_t i = 0; i < count; ++i) {
                result = read_reg(reader, REG_FIFO_DATA, rx + i);
                if (result != ESP_OK) break;
            }
            if (result != ESP_OK) break;
            result = read_reg(reader, REG_CONTROL, &control);
            if (result != ESP_OK) break;
            *rx_len = count;
            *rx_last_bits = control & 7;
            break;
        }
        if (irq & 0x01) { result = ESP_ERR_NOT_FOUND; break; } /* RF timer */
        if (esp_timer_get_time() >= deadline) { result = ESP_ERR_TIMEOUT; break; }
        vTaskDelay(ticks_at_least_one(1));
    }
    /* Abort even on timeouts; the next poll starts with a clean FIFO. */
    esp_err_t stop = write_reg(reader, REG_BIT_FRAMING, 0);
    esp_err_t idle = write_reg(reader, REG_COMMAND, 0);
    if (stop != ESP_OK) return stop;
    if (idle != ESP_OK) return idle;
    return result;
}

static uint16_t crc_a(const uint8_t *bytes, size_t length)
{
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < length; ++i) {
        uint8_t b = bytes[i] ^ (uint8_t)crc;
        b ^= (uint8_t)(b << 4);
        crc = (crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ (b >> 4);
    }
    return crc;
}

static void append_crc(uint8_t *bytes, size_t length)
{
    const uint16_t crc = crc_a(bytes, length);
    bytes[length] = (uint8_t)crc;
    bytes[length + 1] = (uint8_t)(crc >> 8);
}

esp_err_t rc522_poll(rc522_t *reader, rc522_uid_t *uid, bool *present)
{
    if (!reader || !reader->spi || !uid || !present) return ESP_ERR_INVALID_ARG;
    *present = false;
    memset(uid, 0, sizeof(*uid));
    TRY(check_reader(reader));
    uint8_t rx[5], last_bits;
    size_t count;
    const uint8_t wake = 0x52; /* WUPA also detects our previously halted card. */
    esp_err_t err = exchange(reader, &wake, 1, 7, rx, sizeof(rx), &count, &last_bits);
    if (err == ESP_ERR_NOT_FOUND) return err;
    /* A damaged/colliding RF reply is not evidence of card removal. */
    *present = true;
    if (err != ESP_OK) return err;
    if (count != 2 || last_bits) return ESP_ERR_INVALID_RESPONSE;

    for (unsigned level = 0; level < 3; ++level) {
        const uint8_t select = (uint8_t)(0x93 + 2 * level);
        const uint8_t anticollision[2] = {select, 0x20};
        TRY(exchange(reader, anticollision, sizeof(anticollision), 0,
                     rx, sizeof(rx), &count, &last_bits));
        if (count != 5 || last_bits || (rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4])
            return ESP_ERR_INVALID_RESPONSE;

        uint8_t packet[9] = {select, 0x70};
        memcpy(packet + 2, rx, 5);
        append_crc(packet, 7);
        const bool cascade_tag = rx[0] == 0x88;
        if (cascade_tag && level == 2) return ESP_ERR_INVALID_RESPONSE;
        const size_t bytes = cascade_tag ? 3 : 4;
        if (uid->length + bytes > sizeof(uid->bytes)) return ESP_ERR_INVALID_RESPONSE;
        memcpy(uid->bytes + uid->length, rx + (cascade_tag ? 1 : 0), bytes);
        uid->length += bytes;

        TRY(exchange(reader, packet, sizeof(packet), 0,
                     rx, sizeof(rx), &count, &last_bits));
        if (count != 3 || last_bits) return ESP_ERR_INVALID_RESPONSE;
        const uint16_t crc = crc_a(rx, 1);
        if (rx[1] != (uint8_t)crc || rx[2] != (uint8_t)(crc >> 8))
            return ESP_ERR_INVALID_CRC;
        if (!!(rx[0] & 0x04) != cascade_tag) return ESP_ERR_INVALID_RESPONSE;
        if (!(rx[0] & 0x04)) {
            /* HLTA normally has no reply. The following WUPA wakes it again. */
            uint8_t halt[4] = {0x50, 0};
            append_crc(halt, 2);
            err = exchange(reader, halt, sizeof(halt), 0, rx, sizeof(rx), &count, &last_bits);
            return err == ESP_ERR_NOT_FOUND ? ESP_OK :
                   (err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err);
        }
    }
    return ESP_ERR_INVALID_RESPONSE;
}

bool rc522_uid_hex(const rc522_uid_t *uid, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!uid || !output || (uid->length != 4 && uid->length != 7 && uid->length != 10) ||
        capacity < (size_t)uid->length * 2 + 1) return false;
    for (unsigned i = 0; i < uid->length; ++i) {
        output[2 * i] = hex[uid->bytes[i] >> 4];
        output[2 * i + 1] = hex[uid->bytes[i] & 15];
    }
    output[2 * uid->length] = '\0';
    return true;
}
