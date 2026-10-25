"""Compile real firmware C against fake SPI, then verify concurrent PBR1 bytes."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
STUBS = {
    "esp_err.h": """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_CRC 0x109
""",
    "driver/spi_master.h": """
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *spi_device_handle_t;
typedef struct {
 int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num;
 int data4_io_num,data5_io_num,data6_io_num,data7_io_num,max_transfer_sz;
} spi_bus_config_t;
typedef struct { int clock_speed_hz,mode,spics_io_num,queue_size; } spi_device_interface_config_t;
typedef struct { unsigned flags; size_t length; uint8_t tx_data[4],rx_data[4]; } spi_transaction_t;
#define SPI3_HOST 3
#define SPI_DMA_DISABLED 0
#define SPI_TRANS_USE_TXDATA 1
#define SPI_TRANS_USE_RXDATA 2
esp_err_t spi_bus_initialize(int,const spi_bus_config_t *,int);
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t *,spi_device_handle_t *);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_bus_free(int);
esp_err_t spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t *);
""",
    "driver/gpio.h": """
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(int,int);
""",
    "esp_timer.h": """
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
    "freertos/FreeRTOS.h": """
#pragma once
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) ((unsigned)(ms))
""",
    "freertos/task.h": """
#pragma once
#include "freertos/FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
""",
}


def run(args, **kwargs):
    result = subprocess.run(args, check=True, text=True, capture_output=True, **kwargs)
    return result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=r"S:\Qt\Tools\mingw1310_64\bin\gcc.exe")
    args = parser.parse_args()
    stage = Path(tempfile.mkdtemp(prefix="pibridge-host-"))
    for name, content in STUBS.items():
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    for name in ("rc522.c", "rc522.h", "rfid_tracker.c", "rfid_tracker.h"):
        shutil.copy2(PROJECT / "main" / name, stage / name)
    for name in ("test_rc522.c", "test_pbr_sender.c"):
        shutil.copy2(PROJECT / "tests" / name, stage / name)

    # Exercise the production implementation without duplicating its codec.
    main_source = (PROJECT / "main/main.c").read_text(encoding="utf-8")
    sender = main_source.split("static uint16_t pbr_crc16(", 1)[1]
    sender = "static uint16_t pbr_crc16(" + sender.split("static bool pbr_send_link_event(", 1)[0]
    (stage / "sender_under_test.h").write_text(sender, encoding="utf-8")
    env = dict(os.environ)
    env["PATH"] = str(Path(args.cc).parent) + os.pathsep + env.get("PATH", "")
    common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(stage)]
    output = run(common + ["test_rc522.c", "rc522.c", "rfid_tracker.c", "-o", "test_rc522.exe"], cwd=stage, env=env)
    output += run([str(stage / "test_rc522.exe")], cwd=stage, env=env)
    output += run(common + ["test_pbr_sender.c", "-o", "test_pbr_sender.exe"], cwd=stage, env=env)
    output += run([str(stage / "test_pbr_sender.exe")], cwd=stage, env=env)

    spec = importlib.util.spec_from_file_location("pbr1_tool", PROJECT / "tools/pbr1_tool.py")
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    frames = list(codec.iter_frames(bytearray((stage / "capture.bin").read_bytes())))
    assert len(frames) == 200
    assert [f.sequence for f in frames] == list(range(1, 201))
    assert all(f.direction == 1 and f.kind == 1 and f.crc_ok for f in frames)
    payloads = [json.loads(f.payload) for f in frames]
    assert sum(p["event"] == "rfid_card" for p in payloads) == 100
    assert sum(p["event"] == "detach" for p in payloads) == 100
    output += "PASS: two concurrent writers, 200 intact frames, CRC and sequence verified\n"
    output += "First frame hex: " + frames[0].raw.hex() + "\n"
    output += frames[0].describe() + "\n"

    # Compile the actual pin guards with both valid and conflicting assignments.
    shutil.copy2(PROJECT / "main/bridge_pins.h", stage / "bridge_pins.h")
    (stage / "linkit_pins.h").write_text(
        "#define POGO_PIN_TX 17\n#define POGO_PIN_RX 18\n#define POGO_PIN_DET 21\n", encoding="utf-8")
    (stage / "check_pins.c").write_text('#include "bridge_pins.h"\nint main(void) { return 0; }\n', encoding="utf-8")
    values = dict(BRIDGE_RFID_SS_GPIO=10, BRIDGE_RFID_SCK_GPIO=12,
                  BRIDGE_RFID_MOSI_GPIO=11, BRIDGE_RFID_MISO_GPIO=13,
                  BRIDGE_RFID_RST_GPIO=14, BOOT_ROM_LOG_ALWAYS_ON=1)
    cases = [("valid", {}, True), ("MAX17043 pin reused", {"BRIDGE_RFID_SS_GPIO": 9}, False),
             ("LinkIt pin reused", {"BRIDGE_RFID_RST_GPIO": 17}, False),
             ("PSRAM pin", {"BRIDGE_RFID_SS_GPIO": 35}, False),
             ("USB pin", {"BRIDGE_RFID_SS_GPIO": 19}, False),
             ("strapping pin", {"BRIDGE_RFID_SS_GPIO": 3}, False),
             ("unsafe ROM scheme", {"BOOT_ROM_LOG_ALWAYS_ON": 0}, False)]
    for label, overrides, expected in cases:
        config = {**values, **overrides}
        (stage / "sdkconfig.h").write_text("".join(f"#define CONFIG_{k} {v}\n" for k, v in config.items()), encoding="utf-8")
        check = subprocess.run(common + ["check_pins.c", "-o", "check_pins.exe"], cwd=stage,
                               env=env, capture_output=True, text=True)
        assert (check.returncode == 0) == expected, f"GPIO guard test failed: {label}\n{check.stderr}"
    output += "PASS: pin guards accept allocation; reject MAX17043/LinkIt collisions, PSRAM/USB/strapping pins, unsafe ROM scheme\n"
    output += "Host tests use fake hardware; no serial port was opened.\n"
    output += f"Test staging directory: {stage}\n"
    verification = PROJECT / "verification"
    verification.mkdir(exist_ok=True)
    (verification / "host-tests.txt").write_text(output, encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
