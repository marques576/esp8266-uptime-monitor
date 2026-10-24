#pragma once

#include <ArduinoJson.h>

#include "monitor.h"

namespace um {

// Parses "https://host[:port][/path][?query]" into components. A missing scheme
// is treated as http:// so a bare host is accepted from the UI. Fragments are
// stripped; userinfo is rejected.
bool parseHttpUrl(const char* url, char* host, size_t hostSize, uint16_t& port,
                  bool& https, char* path, size_t pathSize, char* err,
                  size_t errSize);

// Renders a monitor target for display / webhook payloads.
void buildMonitorUrl(const Monitor& m, char* out, size_t outSize);

void defaultConfig(DeviceConfig& cfg);

bool validateConfig(const DeviceConfig& cfg, char* err, size_t errSize);

// Accepted-status spec: comma separated codes or ranges, e.g. "200-299,301".
// An empty spec defaults to 2xx. Returns false for malformed specs.
bool statusAccepted(const char* spec, int code);
bool validateStatusSpec(const char* spec, char* err, size_t errSize);

// Serializes config (secrets included; callers must mask before sending).
void configToJson(const DeviceConfig& cfg, JsonDocument& doc);

// Parses and validates. On failure the contents of cfg are unspecified; the
// caller must reload the stored config (never persist a failed parse).
bool configFromJson(const JsonDocument& doc, DeviceConfig& cfg, char* err,
                    size_t errSize);

}  // namespace um
