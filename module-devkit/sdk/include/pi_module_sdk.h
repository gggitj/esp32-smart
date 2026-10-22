#pragma once

// Small, dependency-free C++ helpers for external module authors.  The ABI
// itself remains pi_module_api.h; this header only reduces repetitive safety
// checks and keeps the host callback usage consistent.
#include "pi_module_api.h"
#include <string>

namespace pi_module_sdk {
inline bool valid(const pi_module_handle_t *module, const char *text) {
    return module && text && *text;
}

inline int send_json(pi_module_handle_t *module, const pi_host_services_t *host,
                     uint8_t transport, const std::string &json) {
    if (!module || !host || !host->send_data || json.size() > UINT32_MAX)
        return -1;
    return host->send_data(host->context, transport,
                           reinterpret_cast<const uint8_t *>(json.data()),
                           static_cast<uint32_t>(json.size()));
}

inline void publish(const pi_host_services_t *host, const char *type,
                    const std::string &json) {
    if (!host || !type) return;
    if (host->emit_event)
        host->emit_event(host->context, type, json.c_str());
    else if (host->publish_event)
        host->publish_event(type, json.c_str());
}

inline void log(const pi_host_services_t *host, int level,
                const std::string &message) {
    if (host && host->log) host->log(level, message.c_str());
}
} // namespace pi_module_sdk
