#pragma once

#include "monitor.h"

namespace um {

// Runs a single probe. Always fills `out`; `out.error` points at a static
// string so the scheduler can copy it without owning memory.
bool runCheck(const Monitor& m, uint32_t timeoutMs, CheckOutcome& out);

// POSTs a JSON event to the configured webhook. Returns false and fills `err`
// on any transport or non-2xx failure so the caller can retry.
bool postWebhook(const DeviceConfig& cfg, const Monitor& m, const Notification& n,
                 const char* deviceIp, char* err, size_t errLen);

// ISO-8601 UTC when NTP has synced, otherwise "uptime:<seconds>".
void isoTimestamp(char* out, size_t outSize);

}  // namespace um
