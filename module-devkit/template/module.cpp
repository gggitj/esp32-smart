#include "pi_module_sdk.h"
#include "module_manifest.h"
#include <string>
struct pi_module_handle { const pi_host_services_t *host; uint8_t transport = PI_TRANSPORT_BLE; };
extern "C" uint32_t pi_module_api_version() { return 1; }
extern "C" const char *pi_module_manifest_json() { return MODULE_MANIFEST_JSON; }
extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *h) { return new pi_module_handle{h}; }
extern "C" void pi_module_destroy(pi_module_handle_t *m) { delete m; }
extern "C" int pi_module_on_attach(pi_module_handle_t *, const char *) { return 0; }
extern "C" void pi_module_on_transport_connected(pi_module_handle_t *m, uint8_t t) { if (m) m->transport = t; }
extern "C" int pi_module_on_data(pi_module_handle_t *, uint8_t, const uint8_t *, uint32_t) { return 0; }
extern "C" void pi_module_on_disconnect_request(pi_module_handle_t *, const char *) {}
extern "C" void pi_module_on_detach(pi_module_handle_t *) {}
extern "C" void pi_module_stop(pi_module_handle_t *) {}
extern "C" const char *pi_module_qml_page(pi_module_handle_t *) { return "qml/ModulePage.qml"; }
extern "C" int pi_module_action(pi_module_handle_t *m, const char *a) {
    if (!m || !a) return -1;
    return pi_module_sdk::send_json(m, m->host, m->transport, std::string("{\"command\":\"") + a + "\"}");
}
