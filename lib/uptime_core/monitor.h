// Portable data model for the uptime monitor.
//
// Deliberately free of Arduino / String / filesystem dependencies: everything
// here is fixed-size POD so it can be unit tested on the host and so the
// firmware never fragments the ESP8266 heap while rescheduling monitors.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef UM_MAX_MONITORS
#define UM_MAX_MONITORS 12
#endif

#ifndef UM_FW_VERSION
#define UM_FW_VERSION "0.1.0"
#endif

namespace um {

constexpr uint8_t kMaxMonitors = UM_MAX_MONITORS;

// Field sizes exclude the trailing NUL.
constexpr size_t kNameLen = 31;
constexpr size_t kHostLen = 63;
constexpr size_t kPathLen = 95;
constexpr size_t kErrorLen = 47;
constexpr size_t kDescLen = 95;
constexpr size_t kSsidLen = 32;
constexpr size_t kPassLen = 64;
constexpr size_t kUrlLen = 191;
constexpr size_t kTokenLen = 63;
constexpr size_t kUserLen = 31;
constexpr size_t kKeywordLen = 63;
constexpr size_t kStatusSpecLen = 31;

constexpr uint32_t kMinIntervalSec = 1;
constexpr uint32_t kMaxIntervalSec = 86400;  // 24 h
constexpr uint8_t kMaxRetries = 20;
constexpr uint16_t kMaxResend = 1000;
constexpr uint32_t kMinTimeoutMs = 500;
constexpr uint32_t kMaxTimeoutMs = 30000;
constexpr uint32_t kMaxProbeTimeoutMs = 60000;
constexpr uint8_t kMaxRedirects = 10;
constexpr uint16_t kMinPort = 1;
constexpr uint16_t kMaxPort = 65535;

enum class MonitorType : uint8_t { Http = 0, TcpPort = 1, Ping = 2 };

// Pending is Kuma's "unknown": at least one retry is in flight after a failure.
enum class MonitorState : uint8_t { Pending = 0, Up = 1, Down = 2 };

enum class EventKind : uint8_t { Down = 0, Up = 1, Resend = 2 };

enum class WebhookFormat : uint8_t { Generic = 0, Discord = 1 };

// Bounded string copy; always NUL terminates when dstSize > 0.
inline void copyStr(char* dst, size_t dstSize, const char* src) {
  if (dstSize == 0) return;
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  size_t n = strlen(src);
  if (n >= dstSize) n = dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

struct Monitor {
  // Persisted configuration (mirrors the Uptime Kuma monitor form).
  uint8_t id = 0;
  MonitorType type = MonitorType::Http;
  bool enabled = true;
  bool upsideDown = false;
  bool notify = true;
  char name[kNameLen + 1] = {0};
  char host[kHostLen + 1] = {0};
  uint16_t port = 0;
  bool https = false;
  char path[kPathLen + 1] = {0};
  char description[kDescLen + 1] = {0};
  uint32_t intervalSec = 60;        // "Heartbeat Interval"
  uint8_t retries = 3;              // "Retries"
  uint32_t retryIntervalSec = 60;   // "Heartbeat Retry Interval"
  uint16_t resendEvery = 0;         // 0 = resend disabled

  // HTTP-only fields (mirroring Kuma's HTTP options).
  char keyword[kKeywordLen + 1] = {0};            // empty = status-only check
  char acceptedStatus[kStatusSpecLen + 1] = "200-299";
  uint8_t maxRedirects = 5;
  uint16_t timeoutMs = 0;                         // 0 = inherit the device timeout

  // Runtime only, never written to flash.
  MonitorState state = MonitorState::Pending;
  uint8_t consecutiveFailures = 0;
  uint32_t downBeats = 0;
  uint32_t nextDueMs = 0;
  uint32_t lastCheckMs = 0;
  uint16_t lastLatencyMs = 0;
  uint16_t lastHttpStatus = 0;
  char lastError[kErrorLen + 1] = {0};
};

struct CheckOutcome {
  bool ok = false;
  uint16_t latencyMs = 0;
  uint16_t httpStatus = 0;
  const char* error = nullptr;  // static string, copied by applyCheckResult
};

struct Notification {
  uint8_t monitorId = 0;
  EventKind kind = EventKind::Down;
  uint32_t atMs = 0;
  uint8_t failures = 0;
};

struct DeviceConfig {
  char deviceName[kNameLen + 1] = {0};
  char wifiSsid[kSsidLen + 1] = {0};
  char wifiPass[kPassLen + 1] = {0};
  char webhookUrl[kUrlLen + 1] = {0};
  char webhookToken[kTokenLen + 1] = {0};
  WebhookFormat webhookFormat = WebhookFormat::Generic;
  bool notifyOnRecovery = true;
  char uiUser[kUserLen + 1] = {0};
  char uiPass[kPassLen + 1] = {0};
  uint32_t timeoutMs = 5000;
  Monitor monitors[kMaxMonitors];
  uint8_t monitorCount = 0;
};

inline const char* monitorTypeName(MonitorType t) {
  switch (t) {
    case MonitorType::Http: return "http";
    case MonitorType::TcpPort: return "tcp";
    case MonitorType::Ping: return "ping";
  }
  return "http";
}

inline bool parseMonitorType(const char* s, MonitorType& out) {
  if (s == nullptr) return false;
  if (strcmp(s, "http") == 0 || strcmp(s, "https") == 0) {
    out = MonitorType::Http;
    return true;
  }
  if (strcmp(s, "tcp") == 0 || strcmp(s, "port") == 0) {
    out = MonitorType::TcpPort;
    return true;
  }
  if (strcmp(s, "ping") == 0 || strcmp(s, "icmp") == 0) {
    out = MonitorType::Ping;
    return true;
  }
  return false;
}

inline const char* stateName(MonitorState s) {
  switch (s) {
    case MonitorState::Pending: return "pending";
    case MonitorState::Up: return "up";
    case MonitorState::Down: return "down";
  }
  return "pending";
}

inline const char* eventName(EventKind k) {
  switch (k) {
    case EventKind::Down: return "down";
    case EventKind::Up: return "up";
    case EventKind::Resend: return "resend";
  }
  return "down";
}

inline const char* webhookFormatName(WebhookFormat f) {
  switch (f) {
    case WebhookFormat::Generic: return "generic";
    case WebhookFormat::Discord: return "discord";
  }
  return "generic";
}

inline bool parseWebhookFormat(const char* s, WebhookFormat& out) {
  if (s == nullptr) return false;
  if (strcmp(s, "generic") == 0) {
    out = WebhookFormat::Generic;
    return true;
  }
  if (strcmp(s, "discord") == 0) {
    out = WebhookFormat::Discord;
    return true;
  }
  return false;
}

}  // namespace um
