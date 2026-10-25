#include "pi_module_api.h"
#include "example_manifest.h"
#include <string>
struct pi_module_handle { const pi_host_services_t *host; uint8_t transport = PI_TRANSPORT_HOTSPOT_LAN; };
extern "C" uint32_t pi_module_api_version() { return 1; }
extern "C" const char *pi_module_manifest_json() { return EXAMPLE_MANIFEST; }
extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *host) { return new pi_module_handle{host}; }
extern "C" void pi_module_destroy(pi_module_handle_t *m) { delete m; }
extern "C" int pi_module_on_attach(pi_module_handle_t *, const char *) { return 0; }
extern "C" void pi_module_on_transport_connected(pi_module_handle_t *m, uint8_t transport) { m->transport = transport; }
extern "C" int pi_module_on_data(pi_module_handle_t *m, uint8_t, const uint8_t *p, uint32_t n) {
    if (m && m->host && m->host->emit_event && p) {
        std::string json(reinterpret_cast<const char *>(p), n);
        m->host->emit_event(m->host->context, "sample", json.c_str());
    }
    return 0;
}
extern "C" int pi_module_action(pi_module_handle_t *m, const char *a) {
    if (!m || !m->host || !m->host->send_data || !a) return -1;
    const std::string json = std::string("{\"command\":\"") + a + "\"}";
    return m->host->send_data(m->host->context, m->transport, reinterpret_cast<const uint8_t *>(json.data()), uint32_t(json.size()));
}
extern "C" void pi_module_on_disconnect_request(pi_module_handle_t *, const char *) {}
extern "C" void pi_module_on_detach(pi_module_handle_t *) {}
extern "C" void pi_module_stop(pi_module_handle_t *) {} // no threads, timers or retained callbacks
extern "C" const char *pi_module_qml_page(pi_module_handle_t *) { return "qml/ExamplePage.qml"; }
