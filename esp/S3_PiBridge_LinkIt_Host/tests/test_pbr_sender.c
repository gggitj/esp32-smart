#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define PBRIDGE_MAX_PAYLOAD 2048U
#define PBRIDGE_UART 0
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
typedef CRITICAL_SECTION *SemaphoreHandle_t;
static uint32_t s_pbr_sequence = 1;
static SemaphoreHandle_t s_pbr_tx_mutex;
static uint8_t capture[65536];
static size_t capture_len;
static bool reject_lock;

static int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    (void)timeout;
    if (reject_lock) return 0;
    EnterCriticalSection(mutex);
    return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) { LeaveCriticalSection(mutex); }
static int uart_write_bytes(int port, const void *bytes, size_t len)
{
    assert(port == 0);
    size_t chunk = len > 7 ? 7 : len;
    assert(capture_len + chunk <= sizeof(capture));
    memcpy(capture + capture_len, bytes, chunk); capture_len += chunk;
    Sleep(0); /* Force opportunities for the other producer to interleave. */
    return (int)chunk;
}

#include "sender_under_test.h"

static DWORD WINAPI produce(LPVOID arg)
{
    const char *json = arg;
    for (unsigned i = 0; i < 100; ++i) assert(pbr_send_frame(1, json));
    return 0;
}

int main(void)
{
    CRITICAL_SECTION mutex;
    InitializeCriticalSection(&mutex); s_pbr_tx_mutex = &mutex;
    char oversized[2050]; memset(oversized, 'x', sizeof(oversized)); oversized[2049] = 0;
    assert(!pbr_send_frame(1, NULL) && !pbr_send_frame(1, oversized));
    reject_lock = true; assert(!pbr_send_frame(1, "{}")); reject_lock = false;
    assert(s_pbr_sequence == 1 && capture_len == 0);
    const char *a = "{\"event\":\"rfid_card\",\"uid\":\"00112233\"}";
    const char *b = "{\"event\":\"detach\",\"module_id\":80}";
    HANDLE threads[2] = {CreateThread(NULL, 0, produce, (LPVOID)a, 0, NULL),
                         CreateThread(NULL, 0, produce, (LPVOID)b, 0, NULL)};
    assert(threads[0] && threads[1]);
    assert(WaitForMultipleObjects(2, threads, TRUE, 20000) == WAIT_OBJECT_0);
    CloseHandle(threads[0]); CloseHandle(threads[1]);
    FILE *file = fopen("capture.bin", "wb"); assert(file);
    assert(fwrite(capture, 1, capture_len, file) == capture_len); fclose(file);
    capture_len = 0; s_pbr_sequence = UINT32_MAX;
    assert(pbr_send_frame(1, "{}") && pbr_send_frame(1, "{}"));
    assert(capture[6] == 0xff && capture[9] == 0xff);
    assert(capture[16 + 6] == 1 && capture[16 + 7] == 0 && s_pbr_sequence == 2);
    DeleteCriticalSection(&mutex);
    puts("PASS: real PBR1 sender bounds, lock failure, fragmented UART writes, sequence wrap skips zero");
    return 0;
}
