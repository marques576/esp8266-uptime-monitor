#include "config_codec.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace um {
namespace {

void setErr(char* err, size_t errSize, const char* msg) {
  if (err == nullptr || errSize == 0) return;
  copyStr(err, errSize, msg);
}

void setErrf(char* err, size_t errSize, const char* fmt, ...) {
  if (err == nullptr || errSize == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errSize, fmt, ap);
  va_end(ap);
}

bool startsWithCI(const char* s, const char* prefix) {
  while (*prefix) {
    if (*s == '\0') return false;
    if (tolower(static_cast<unsigned char>(*s)) !=
        tolower(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++s;
    ++prefix;
  }
  return true;
}

bool hasHttpScheme(const char* s) {
  return s != nullptr && (startsWithCI(s, "http://") || startsWithCI(s, "https://"));
}

bool readStr(JsonVariantConst v, const char* def, char* dst, size_t dstSize,
             const char* label, char* err, size_t errSize) {
  const char* s = v | def;
  if (s == nullptr) s = def;
  if (strlen(s) >= dstSize) {
    setErrf(err, errSize, "%s is too long (max %u)", label,
            static_cast<unsigned>(dstSize - 1));
    return false;
  }
  copyStr(dst, dstSize, s);
  return true;
}

bool readU32(JsonVariantConst v, uint32_t def, uint32_t lo, uint32_t hi,
             uint32_t& out, const char* label, char* err, size_t errSize) {
  const double d = v | static_cast<double>(def);
  if (!(d >= static_cast<double>(lo) && d <= static_cast<double>(hi))) {
    setErrf(err, errSize, "%s must be %u..%u", label, static_cast<unsigned>(lo),
            static_cast<unsigned>(hi));
    return false;
  }
  out = static_cast<uint32_t>(d);
  return true;
}

// Exactly three digits, so "20-299" and "2000" are rejected.
bool parseCode(const char** p, int& out) {
  int value = 0;
  for (int i = 0; i < 3; ++i) {
    if (!isdigit(static_cast<unsigned char>(**p))) return false;
    value = value * 10 + (**p - '0');
    ++*p;
  }
  if (isdigit(static_cast<unsigned char>(**p))) return false;
  out = value;
  return true;
}

bool parseStatusSpec(const char* spec, int* lo, int* hi, const char** next) {
  const char* p = spec;
  while (*p == ',' || isspace(static_cast<unsigned char>(*p))) ++p;
  if (*p == '\0') {
    *next = p;
    return false;
  }
  int a = 0;
  if (!parseCode(&p, a)) return false;
  int b = a;
  if (*p == '-') {
    ++p;
    if (!parseCode(&p, b)) return false;
  }
  if (a < 100 || a > 599 || b < a || b > 599) return false;
  *lo = a;
  *hi = b;
  *next = p;
  return true;
}

}  // namespace

bool validateStatusSpec(const char* spec, char* err, size_t errSize) {
  if (spec == nullptr || *spec == '\0') {
    setErr(err, errSize, "accepted status is empty");
    return false;
  }
  const char* p = spec;
  while (*p != '\0') {
    int lo = 0;
    int hi = 0;
    const char* next = nullptr;
    if (!parseStatusSpec(p, &lo, &hi, &next)) {
      setErr(err, errSize, "accepted status must look like 200-299 or 200,204,301");
      return false;
    }
    p = next;
    while (isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p != '\0' && *p != ',') {
      setErr(err, errSize, "accepted status has an unexpected character");
      return false;
    }
  }
  return true;
}

bool statusAccepted(const char* spec, int code) {
  if (spec == nullptr || *spec == '\0') return code >= 200 && code < 300;
  const char* p = spec;
  while (*p != '\0') {
    int lo = 0;
    int hi = 0;
    const char* next = nullptr;
    if (!parseStatusSpec(p, &lo, &hi, &next)) return false;
    if (code >= lo && code <= hi) return true;
    p = next;
    while (*p != '\0' && *p != ',') ++p;
  }
  return false;
}

bool parseHttpUrl(const char* url, char* host, size_t hostSize, uint16_t& port,
                  bool& https, char* path, size_t pathSize, char* err,
                  size_t errSize) {
  if (url == nullptr || *url == '\0') {
    setErr(err, errSize, "URL is empty");
    return false;
  }

  const char* p = url;
  bool tls = false;
  if (startsWithCI(p, "https://")) {
    tls = true;
    p += 8;
  } else if (startsWithCI(p, "http://")) {
    p += 7;
  } else if (strstr(p, "://") != nullptr) {
    setErr(err, errSize, "only http:// and https:// are supported");
    return false;
  }

  const char* hostBegin = p;
  while (*p != '\0' && *p != '/' && *p != '?' && *p != '#' && *p != ':') ++p;
  const size_t hostLen = static_cast<size_t>(p - hostBegin);
  if (hostLen == 0) {
    setErr(err, errSize, "URL has no host");
    return false;
  }
  if (hostLen >= hostSize) {
    setErr(err, errSize, "host is too long");
    return false;
  }
  for (size_t i = 0; i < hostLen; ++i) {
    const unsigned char c = static_cast<unsigned char>(hostBegin[i]);
    if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) {
      setErr(err, errSize, "host contains an invalid character");
      return false;
    }
  }
  memcpy(host, hostBegin, hostLen);
  host[hostLen] = '\0';

  uint32_t portNum = tls ? 443u : 80u;
  if (*p == ':') {
    ++p;
    if (!isdigit(static_cast<unsigned char>(*p))) {
      setErr(err, errSize, "URL has an invalid port");
      return false;
    }
    uint32_t value = 0;
    while (isdigit(static_cast<unsigned char>(*p))) {
      value = value * 10u + static_cast<uint32_t>(*p - '0');
      if (value > kMaxPort) {
        setErr(err, errSize, "port is out of range");
        return false;
      }
      ++p;
    }
    if (value < kMinPort) {
      setErr(err, errSize, "port is out of range");
      return false;
    }
    portNum = value;
  }

  port = static_cast<uint16_t>(portNum);
  https = tls;

  if (*p == '\0' || *p == '#') {
    copyStr(path, pathSize, "/");
  } else if (*p == '?') {
    const char* hash = strchr(p, '#');
    const size_t qlen = hash != nullptr ? static_cast<size_t>(hash - p) : strlen(p);
    if (qlen + 2 > pathSize) {
      setErr(err, errSize, "path is too long");
      return false;
    }
    path[0] = '/';
    memcpy(path + 1, p, qlen);
    path[qlen + 1] = '\0';
  } else {
    const char* hash = strchr(p, '#');
    const size_t plen = hash != nullptr ? static_cast<size_t>(hash - p) : strlen(p);
    if (plen + 1 > pathSize) {
      setErr(err, errSize, "path is too long");
      return false;
    }
    memcpy(path, p, plen);
    path[plen] = '\0';
  }

  return true;
}

void buildMonitorUrl(const Monitor& m, char* out, size_t outSize) {
  if (outSize == 0) return;
  if (m.type == MonitorType::TcpPort) {
    snprintf(out, outSize, "%s:%u", m.host, static_cast<unsigned>(m.port));
    return;
  }
  if (m.type == MonitorType::Ping) {
    snprintf(out, outSize, "%s", m.host);
    return;
  }
  const char* scheme = m.https ? "https" : "http";
  const bool defaultPort = (m.https && m.port == 443) || (!m.https && m.port == 80);
  const char* path = m.path[0] != '\0' ? m.path : "/";
  if (defaultPort) {
    snprintf(out, outSize, "%s://%s%s", scheme, m.host, path);
  } else {
    snprintf(out, outSize, "%s://%s:%u%s", scheme, m.host,
             static_cast<unsigned>(m.port), path);
  }
}

void defaultConfig(DeviceConfig& cfg) {
  copyStr(cfg.deviceName, sizeof(cfg.deviceName), "uptime-monitor");
  copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), "");
  copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), "");
  copyStr(cfg.webhookUrl, sizeof(cfg.webhookUrl), "");
  copyStr(cfg.webhookToken, sizeof(cfg.webhookToken), "");
  cfg.notifyOnRecovery = true;
  copyStr(cfg.uiUser, sizeof(cfg.uiUser), "");
  copyStr(cfg.uiPass, sizeof(cfg.uiPass), "");
  cfg.timeoutMs = 5000;
  cfg.monitorCount = 0;
  for (uint8_t i = 0; i < kMaxMonitors; ++i) {
    cfg.monitors[i] = Monitor();
  }
}

bool validateConfig(const DeviceConfig& cfg, char* err, size_t errSize) {
  if (cfg.deviceName[0] == '\0') {
    setErr(err, errSize, "device_name must not be empty");
    return false;
  }
  if (cfg.timeoutMs < kMinTimeoutMs || cfg.timeoutMs > kMaxTimeoutMs) {
    setErrf(err, errSize, "timeout_ms must be %u..%u",
            static_cast<unsigned>(kMinTimeoutMs),
            static_cast<unsigned>(kMaxTimeoutMs));
    return false;
  }
  if (cfg.webhookUrl[0] != '\0' && !hasHttpScheme(cfg.webhookUrl)) {
    setErr(err, errSize, "webhook URL must start with http:// or https://");
    return false;
  }
  if (cfg.monitorCount > kMaxMonitors) {
    setErr(err, errSize, "too many monitors");
    return false;
  }

  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    const Monitor& m = cfg.monitors[i];
    if (m.name[0] == '\0') {
      setErrf(err, errSize, "monitor %u: name must not be empty",
              static_cast<unsigned>(i + 1));
      return false;
    }
    if (m.host[0] == '\0') {
      setErrf(err, errSize, "monitor %u: host must not be empty",
              static_cast<unsigned>(i + 1));
      return false;
    }
    if (m.intervalSec < kMinIntervalSec || m.intervalSec > kMaxIntervalSec) {
      setErrf(err, errSize, "monitor %u: interval must be %u..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMinIntervalSec),
              static_cast<unsigned>(kMaxIntervalSec));
      return false;
    }
    if (m.retries > kMaxRetries) {
      setErrf(err, errSize, "monitor %u: retries must be 0..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMaxRetries));
      return false;
    }
    if (m.retryIntervalSec < kMinIntervalSec || m.retryIntervalSec > kMaxIntervalSec) {
      setErrf(err, errSize, "monitor %u: retry interval must be %u..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMinIntervalSec),
              static_cast<unsigned>(kMaxIntervalSec));
      return false;
    }
    if (m.resendEvery > kMaxResend) {
      setErrf(err, errSize, "monitor %u: resend_every must be 0..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMaxResend));
      return false;
    }
    if (m.maxRedirects > kMaxRedirects) {
      setErrf(err, errSize, "monitor %u: max_redirects must be 0..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMaxRedirects));
      return false;
    }
    if (m.timeoutMs != 0 &&
        (m.timeoutMs < kMinTimeoutMs || m.timeoutMs > kMaxProbeTimeoutMs)) {
      setErrf(err, errSize, "monitor %u: timeout_ms must be 0 or %u..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMinTimeoutMs),
              static_cast<unsigned>(kMaxProbeTimeoutMs));
      return false;
    }
    if (m.type == MonitorType::Http) {
      char statusErr[64] = {0};
      if (!validateStatusSpec(m.acceptedStatus, statusErr, sizeof(statusErr))) {
        setErrf(err, errSize, "monitor %u: %s", static_cast<unsigned>(i + 1),
                statusErr);
        return false;
      }
    }
    if (m.type == MonitorType::TcpPort &&
        (m.port < kMinPort || m.port > kMaxPort)) {
      setErrf(err, errSize, "monitor %u: port must be %u..%u",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(kMinPort),
              static_cast<unsigned>(kMaxPort));
      return false;
    }
    if (m.type == MonitorType::Http) {
      if (m.port < kMinPort || m.port > kMaxPort) {
        setErrf(err, errSize, "monitor %u: HTTP port must be %u..%u",
                static_cast<unsigned>(i + 1), static_cast<unsigned>(kMinPort),
                static_cast<unsigned>(kMaxPort));
        return false;
      }
      if (m.path[0] != '/') {
        setErrf(err, errSize, "monitor %u: path must start with '/'",
                static_cast<unsigned>(i + 1));
        return false;
      }
    }
  }

  return true;
}

void configToJson(const DeviceConfig& cfg, JsonDocument& doc) {
  doc.clear();
  doc["version"] = 1;
  doc["device_name"] = cfg.deviceName;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cfg.wifiSsid;
  wifi["pass"] = cfg.wifiPass;

  JsonObject hook = doc["webhook"].to<JsonObject>();
  hook["url"] = cfg.webhookUrl;
  hook["token"] = cfg.webhookToken;
  hook["format"] = webhookFormatName(cfg.webhookFormat);
  hook["notify_on_recovery"] = cfg.notifyOnRecovery;

  JsonObject ui = doc["ui"].to<JsonObject>();
  ui["user"] = cfg.uiUser;
  ui["pass"] = cfg.uiPass;

  doc["timeout_ms"] = cfg.timeoutMs;

  JsonArray arr = doc["monitors"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    const Monitor& m = cfg.monitors[i];
    JsonObject o = arr.add<JsonObject>();
    o["id"] = m.id;
    o["name"] = m.name;
    o["type"] = monitorTypeName(m.type);
    o["enabled"] = m.enabled;
    o["upside_down"] = m.upsideDown;
    o["notify"] = m.notify;
    o["host"] = m.host;
    o["port"] = m.port;
    o["https"] = m.https;
    o["path"] = m.path;
    o["interval"] = m.intervalSec;
    o["retries"] = m.retries;
    o["retry_interval"] = m.retryIntervalSec;
    o["resend_every"] = m.resendEvery;
    o["description"] = m.description;
    o["keyword"] = m.keyword;
    o["accepted_status"] = m.acceptedStatus;
    o["max_redirects"] = m.maxRedirects;
    o["timeout_ms"] = m.timeoutMs;
    if (m.type == MonitorType::Http) {
      char url[kUrlLen + 1];
      buildMonitorUrl(m, url, sizeof(url));
      o["url"] = url;
    }
  }
}

bool configFromJson(const JsonDocument& doc, DeviceConfig& cfg, char* err,
                    size_t errSize) {
  JsonVariantConst root = doc;

  copyStr(cfg.deviceName, sizeof(cfg.deviceName), "");
  copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), "");
  copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), "");
  copyStr(cfg.webhookUrl, sizeof(cfg.webhookUrl), "");
  copyStr(cfg.webhookToken, sizeof(cfg.webhookToken), "");
  cfg.notifyOnRecovery = true;
  copyStr(cfg.uiUser, sizeof(cfg.uiUser), "");
  copyStr(cfg.uiPass, sizeof(cfg.uiPass), "");
  cfg.timeoutMs = 5000;
  cfg.monitorCount = 0;
  for (uint8_t i = 0; i < kMaxMonitors; ++i) {
    cfg.monitors[i] = Monitor();
  }

  JsonVariantConst wifi = root["wifi"];
  JsonVariantConst hook = root["webhook"];
  JsonVariantConst ui = root["ui"];

  if (!readStr(root["device_name"], "uptime-monitor", cfg.deviceName,
               sizeof(cfg.deviceName), "device_name", err, errSize)) {
    return false;
  }
  if (!readStr(wifi["ssid"], "", cfg.wifiSsid, sizeof(cfg.wifiSsid), "wifi.ssid",
               err, errSize)) {
    return false;
  }
  if (!readStr(wifi["pass"], "", cfg.wifiPass, sizeof(cfg.wifiPass), "wifi.pass",
               err, errSize)) {
    return false;
  }
  if (!readStr(hook["url"], "", cfg.webhookUrl, sizeof(cfg.webhookUrl),
               "webhook.url", err, errSize)) {
    return false;
  }
  if (!readStr(hook["token"], "", cfg.webhookToken, sizeof(cfg.webhookToken),
               "webhook.token", err, errSize)) {
    return false;
  }
  {
    const char* fmt = hook["format"] | "generic";
    if (fmt == nullptr || !parseWebhookFormat(fmt, cfg.webhookFormat)) {
      setErr(err, errSize, "webhook.format must be generic or discord");
      return false;
    }
  }
  cfg.notifyOnRecovery = hook["notify_on_recovery"] | true;
  if (!readStr(ui["user"], "", cfg.uiUser, sizeof(cfg.uiUser), "ui.user", err,
               errSize)) {
    return false;
  }
  if (!readStr(ui["pass"], "", cfg.uiPass, sizeof(cfg.uiPass), "ui.pass", err,
               errSize)) {
    return false;
  }
  if (!readU32(root["timeout_ms"], 5000, kMinTimeoutMs, kMaxTimeoutMs,
               cfg.timeoutMs, "timeout_ms", err, errSize)) {
    return false;
  }

  JsonArrayConst arr = root["monitors"].as<JsonArrayConst>();
  uint8_t count = 0;
  uint8_t nextId = 1;
  bool used[256] = {false};

  for (JsonVariantConst v : arr) {
    if (count >= kMaxMonitors) {
      setErr(err, errSize, "too many monitors");
      return false;
    }
    JsonObjectConst o = v.as<JsonObjectConst>();
    Monitor& m = cfg.monitors[count];
    m = Monitor();

    char label[40];
    snprintf(label, sizeof(label), "monitor %u type", static_cast<unsigned>(count + 1));
    const char* typeStr = o["type"] | "http";
    if (typeStr == nullptr || !parseMonitorType(typeStr, m.type)) {
      setErrf(err, errSize, "%s is invalid", label);
      return false;
    }

    snprintf(label, sizeof(label), "monitor %u name", static_cast<unsigned>(count + 1));
    if (!readStr(o["name"], "", m.name, sizeof(m.name), label, err, errSize)) {
      return false;
    }

    m.enabled = o["enabled"] | true;
    m.upsideDown = o["upside_down"] | false;
    m.notify = o["notify"] | true;

    snprintf(label, sizeof(label), "monitor %u interval", static_cast<unsigned>(count + 1));
    if (!readU32(o["interval"], 60, kMinIntervalSec, kMaxIntervalSec, m.intervalSec,
                 label, err, errSize)) {
      return false;
    }

    uint32_t retries = 3;
    snprintf(label, sizeof(label), "monitor %u retries", static_cast<unsigned>(count + 1));
    if (!readU32(o["retries"], 3, 0, kMaxRetries, retries, label, err, errSize)) {
      return false;
    }
    m.retries = static_cast<uint8_t>(retries);

    snprintf(label, sizeof(label), "monitor %u retry_interval",
             static_cast<unsigned>(count + 1));
    if (!readU32(o["retry_interval"], 60, kMinIntervalSec, kMaxIntervalSec,
                 m.retryIntervalSec, label, err, errSize)) {
      return false;
    }

    uint32_t resend = 0;
    snprintf(label, sizeof(label), "monitor %u resend_every",
             static_cast<unsigned>(count + 1));
    if (!readU32(o["resend_every"], 0, 0, kMaxResend, resend, label, err,
                 errSize)) {
      return false;
    }
    m.resendEvery = static_cast<uint16_t>(resend);

    snprintf(label, sizeof(label), "monitor %u description",
             static_cast<unsigned>(count + 1));
    if (!readStr(o["description"], "", m.description, sizeof(m.description), label,
                 err, errSize)) {
      return false;
    }

    snprintf(label, sizeof(label), "monitor %u host", static_cast<unsigned>(count + 1));
    if (!readStr(o["host"], "", m.host, sizeof(m.host), label, err, errSize)) {
      return false;
    }

    uint32_t port = 0;
    snprintf(label, sizeof(label), "monitor %u port", static_cast<unsigned>(count + 1));
    if (!readU32(o["port"], 0, 0, kMaxPort, port, label, err, errSize)) {
      return false;
    }
    m.port = static_cast<uint16_t>(port);
    m.https = o["https"] | false;

    snprintf(label, sizeof(label), "monitor %u path", static_cast<unsigned>(count + 1));
    if (!readStr(o["path"], "/", m.path, sizeof(m.path), label, err, errSize)) {
      return false;
    }

    snprintf(label, sizeof(label), "monitor %u keyword", static_cast<unsigned>(count + 1));
    if (!readStr(o["keyword"], "", m.keyword, sizeof(m.keyword), label, err, errSize)) {
      return false;
    }

    snprintf(label, sizeof(label), "monitor %u accepted_status",
             static_cast<unsigned>(count + 1));
    if (!readStr(o["accepted_status"], "200-299", m.acceptedStatus,
                 sizeof(m.acceptedStatus), label, err, errSize)) {
      return false;
    }

    uint32_t redirects = 5;
    snprintf(label, sizeof(label), "monitor %u max_redirects",
             static_cast<unsigned>(count + 1));
    if (!readU32(o["max_redirects"], 5, 0, kMaxRedirects, redirects, label, err,
                 errSize)) {
      return false;
    }
    m.maxRedirects = static_cast<uint8_t>(redirects);

    uint32_t monitorTimeout = 0;
    snprintf(label, sizeof(label), "monitor %u timeout_ms",
             static_cast<unsigned>(count + 1));
    if (!readU32(o["timeout_ms"], 0, 0, kMaxProbeTimeoutMs, monitorTimeout, label,
                 err, errSize)) {
      return false;
    }
    m.timeoutMs = static_cast<uint16_t>(monitorTimeout);

    // The UI sends `url` for HTTP monitors; accept it as the authoritative
    // source when present so the user doesn't have to think in components.
    if (m.type == MonitorType::Http) {
      const char* url = o["url"] | "";
      if (url != nullptr && *url != '\0') {
        if (!parseHttpUrl(url, m.host, sizeof(m.host), m.port, m.https, m.path,
                          sizeof(m.path), err, errSize)) {
          return false;
        }
      }
    } else if (m.type == MonitorType::Ping) {
      m.port = 0;
      if (m.path[0] == '\0') copyStr(m.path, sizeof(m.path), "/");
    }

    if (m.name[0] == '\0') {
      copyStr(m.name, sizeof(m.name), m.host);
    }

    if (m.type == MonitorType::Http && m.path[0] != '/') {
      const size_t n = strlen(m.path);
      if (n + 2 > sizeof(m.path)) {
        setErrf(err, errSize, "monitor %u path is too long",
                static_cast<unsigned>(count + 1));
        return false;
      }
      memmove(m.path + 1, m.path, n + 1);
      m.path[0] = '/';
    }

    uint8_t id = static_cast<uint8_t>(o["id"] | 0);
    if (id == 0 || used[id]) {
      while (nextId != 0 && used[nextId]) ++nextId;
      if (nextId == 0) {
        setErr(err, errSize, "too many monitors");
        return false;
      }
      id = nextId;
      ++nextId;
    }
    used[id] = true;
    m.id = id;

    ++count;
  }

  cfg.monitorCount = count;
  return validateConfig(cfg, err, errSize);
}

}  // namespace um
