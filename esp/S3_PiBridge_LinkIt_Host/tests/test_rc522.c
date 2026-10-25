#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc522.h"
#include "rfid_tracker.h"
#include "driver/gpio.h"
#include "freertos/task.h"

typedef struct {
    uint8_t tx[9], rx[5];
    size_t tx_len, rx_len;
    uint8_t irq, errors, last_bits;
} response_t;

static uint8_t regs[64], fifo_tx[64], fifo_rx[64];
static size_t fifo_tx_len, fifo_rx_len, fifo_rx_pos;
static response_t script[10];
static size_t steps, next_step;
static int64_t clock_us;
static bool fail_spi;

esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *bus, int dma)
{
    assert(host == SPI3_HOST && dma == SPI_DMA_DISABLED);
    assert(bus->mosi_io_num == 11 && bus->miso_io_num == 13 && bus->sclk_io_num == 12);
    return ESP_OK;
}
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *dev, spi_device_handle_t *out)
{
    assert(host == SPI3_HOST && dev->clock_speed_hz == 1000000);
    assert(dev->mode == 0 && dev->spics_io_num == 10);
    *out = (void *)1;
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t spi) { (void)spi; return ESP_OK; }
esp_err_t spi_bus_free(int host) { (void)host; return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *cfg)
{
    assert(cfg->pin_bit_mask == (UINT64_C(1) << 14));
    return ESP_OK;
}
esp_err_t gpio_set_level(int gpio, int value) { assert(gpio == 14); (void)value; return ESP_OK; }
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(TickType_t ticks) { clock_us += 1000 * ticks; }

esp_err_t spi_device_polling_transmit(spi_device_handle_t spi, spi_transaction_t *t)
{
    assert(spi && t->length == 16);
    if (fail_spi) { fail_spi = false; return ESP_ERR_INVALID_STATE; }
    const uint8_t reg = (t->tx_data[0] & 0x7e) >> 1;
    if (t->tx_data[0] & 0x80) {
        assert(t->flags & SPI_TRANS_USE_RXDATA);
        if (reg == 0x09) { assert(fifo_rx_pos < fifo_rx_len); t->rx_data[1] = fifo_rx[fifo_rx_pos++]; }
        else if (reg == 0x0a) t->rx_data[1] = (uint8_t)fifo_rx_len;
        else t->rx_data[1] = regs[reg];
    } else {
        const uint8_t value = t->tx_data[1];
        regs[reg] = value;
        if (reg == 0x01 && value == 0x0f) regs[reg] = 0;
        if (reg == 0x04) regs[reg] = 0; /* IRQ write-one-to-clear. */
        if (reg == 0x0a && value & 0x80) fifo_tx_len = fifo_rx_len = fifo_rx_pos = 0;
        if (reg == 0x09) { assert(fifo_tx_len < sizeof(fifo_tx)); fifo_tx[fifo_tx_len++] = value; }
        if (reg == 0x0d && value & 0x80) {
            assert(next_step < steps);
            const response_t *s = &script[next_step++];
            assert(fifo_tx_len == s->tx_len && memcmp(fifo_tx, s->tx, s->tx_len) == 0);
            assert((value & 7) == (s->tx[0] == 0x52 ? 7 : 0));
            memcpy(fifo_rx, s->rx, s->rx_len);
            fifo_rx_len = s->rx_len;
            regs[0x04] = s->irq;
            regs[0x06] = s->errors;
            regs[0x0c] = s->last_bits;
        }
    }
    return ESP_OK;
}

/* Independent bitwise CRC-A reference (reflected polynomial 0x8408). */
static void crc_append(uint8_t *packet, size_t len)
{
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < len; ++i) {
        crc ^= packet[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    packet[len] = (uint8_t)crc;
    packet[len + 1] = (uint8_t)(crc >> 8);
}

static void script_reset(void) { steps = next_step = 0; memset(script, 0, sizeof(script)); }
static response_t *step(const uint8_t *tx, size_t tx_len, const uint8_t *rx, size_t rx_len)
{
    assert(steps < sizeof(script) / sizeof(script[0]));
    response_t *s = &script[steps++];
    memcpy(s->tx, tx, tx_len); s->tx_len = tx_len;
    if (rx_len) memcpy(s->rx, rx, rx_len);
    s->rx_len = rx_len; s->irq = rx_len ? 0x30 : 0x01;
    return s;
}
static void wake_response(void)
{
    const uint8_t wake[] = {0x52}, atqa[] = {0x04, 0};
    step(wake, 1, atqa, 2);
}

static void card_script(const uint8_t *uid, size_t len)
{
    script_reset(); wake_response();
    size_t pos = 0;
    unsigned levels = (unsigned)((len - 1) / 3);
    for (unsigned level = 0; level < levels; ++level) {
        bool cascade = level + 1 < levels;
        uint8_t anti[2] = {(uint8_t)(0x93 + 2 * level), 0x20}, block[5];
        if (cascade) { block[0] = 0x88; memcpy(block + 1, uid + pos, 3); pos += 3; }
        else { memcpy(block, uid + pos, 4); pos += 4; }
        block[4] = block[0] ^ block[1] ^ block[2] ^ block[3];
        step(anti, 2, block, 5);
        uint8_t select[9] = {anti[0], 0x70}, sak[3] = {cascade ? 0x04 : 0x08, 0, 0};
        memcpy(select + 2, block, 5); crc_append(select, 7); crc_append(sak, 1);
        step(select, 9, sak, 3);
    }
    uint8_t halt[4] = {0x50, 0, 0, 0}; crc_append(halt, 2);
    /* A known vector prevents the test reference and driver sharing a CRC bug. */
    assert(halt[2] == 0x57 && halt[3] == 0xcd);
    step(halt, 4, NULL, 0);
}

static void test_uids(rc522_t *reader)
{
    const uint8_t bytes[10] = {0, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x17, 0x28, 0x39};
    const unsigned lengths[] = {4, 7, 10};
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned len = lengths[i];
        card_script(bytes, len);
        rc522_uid_t uid;
        bool present = false;
        assert(rc522_poll(reader, &uid, &present) == ESP_OK);
        assert(present && uid.length == len && memcmp(uid.bytes, bytes, len) == 0);
        assert(next_step == steps);
        char hex[21];
        assert(rc522_uid_hex(&uid, hex, sizeof(hex)));
        assert(strlen(hex) == len * 2 && !strncmp(hex, "00A1B2C3", 8));
        assert(!rc522_uid_hex(&uid, hex, len * 2));
    }
    puts("PASS: SPI3/pins/mode/1MHz, 4/7/10-byte UID cascade, leading zero, BCC/CRC-A, HLTA");
}

static void test_failures(rc522_t *reader)
{
    rc522_uid_t uid;
    bool present = true;
    script_reset(); const uint8_t wake[] = {0x52}; step(wake, 1, NULL, 0);
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_NOT_FOUND && !present);
    regs[0x37] = 0;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_RESPONSE && !present);
    regs[0x37] = 0xff;
    assert(rc522_reset(reader) == ESP_ERR_INVALID_RESPONSE);
    regs[0x37] = 0x92;
    assert(rc522_reset(reader) == ESP_OK);
    script_reset(); wake_response(); script[0].errors = 0x08;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_RESPONSE && present);
    script_reset(); step(wake, 1, NULL, 0)->irq = 0;
    const int64_t before = clock_us;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_TIMEOUT && present);
    assert(clock_us - before >= 40000 && clock_us - before < 50000);
    const uint8_t bytes[4] = {1, 2, 3, 4};
    card_script(bytes, 4); script[1].rx[4] ^= 1;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_RESPONSE && present);
    card_script(bytes, 4); script[2].rx[1] ^= 1;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_CRC && present);
    card_script(bytes, 4); script[1].last_bits = 1;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_RESPONSE && present);
    fail_spi = true;
    assert(rc522_poll(reader, &uid, &present) == ESP_ERR_INVALID_STATE && !present);
    assert(regs[0x01] == 0 && regs[0x0d] == 0);
    puts("PASS: no card, reader missing/reconnect, collision, bounded timeout, bad BCC/SAK CRC, partial response, SPI failure");
}

static void test_tracker(void)
{
    rfid_tracker_t t;
    const rc522_uid_t a = {.bytes = {0, 1, 2, 3}, .length = 4};
    const rc522_uid_t b = {.bytes = {0, 1, 2, 4}, .length = 4};
    rfid_tracker_init(&t);
    assert(rfid_tracker_should_report(&t, &a));
    /* Failed send: do not mark reported, so the next poll can retry. */
    assert(rfid_tracker_should_report(&t, &a));
    rfid_tracker_reported(&t, &a);
    for (unsigned i = 0; i < 500; ++i) {
        rfid_tracker_present(&t); assert(!rfid_tracker_should_report(&t, &a));
    }
    rfid_tracker_absent(&t, 0, 400); rfid_tracker_absent(&t, 399, 400);
    assert(!rfid_tracker_should_report(&t, &a));
    rfid_tracker_present(&t); /* RF error or reader outage interrupts absence. */
    rfid_tracker_absent(&t, 1000, 400); rfid_tracker_absent(&t, 1399, 400);
    assert(!rfid_tracker_should_report(&t, &a));
    rfid_tracker_absent(&t, 1400, 400);
    assert(rfid_tracker_should_report(&t, &a));
    rfid_tracker_reported(&t, &a);
    assert(rfid_tracker_should_report(&t, &b));
    rc522_uid_t invalid = {.length = 11};
    assert(!rfid_tracker_should_report(&t, &invalid));
    char hex[30]; assert(!rc522_uid_hex(&invalid, hex, sizeof(hex)));
    puts("PASS: held-card dedupe, 400ms continuous removal, error debounce, card swap, retry after send failure");
}

int main(void)
{
    rc522_t reader;
    regs[0x37] = 0x92;
    assert(rc522_open(&reader, 10, 12, 11, 13, 14) == ESP_OK);
    assert(rc522_reset(&reader) == ESP_OK);
    assert(reader.version == 0x92 && regs[0x2a] == 0x80 && regs[0x2b] == 0xa9);
    test_uids(&reader);
    test_failures(&reader);
    test_tracker();
    return 0;
}
