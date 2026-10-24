#include "checks.h"

#include <Arduino.h>
#include <ESP8266Ping.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>

#include <ArduinoJson.h>

#include <memory>
#include <new>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "scheduler.h"
#include "config_codec.h"

namespace um {
namespace {

void setErr(char* err, size_t errLen, const char* msg) {
  copyStr(err, errLen, msg);
}

uint16_t clampMs(uint32_t ms) {
  return ms > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(ms);
}

bool openClient(const char* host, uint16_t port, bool https, uint32_t timeoutMs,
                std::unique_ptr<WiFiClient>& client);

bool startsWithCI(const char* s, const char* prefix) {
  while (*prefix != '\0') {
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

// Reads one CRLF-terminated line. Returns the length, 0 for a blank line, or
// -1 on timeout / closed socket.
int readLine(WiFiClient* client, char* out, size_t cap, uint32_t deadline) {
  size_t len = 0;
  while (!timeReached(millis(), deadline)) {
    const int ch = client->read();
    if (ch < 0) {
      if (!client->connected() && client->available() == 0) return -1;
      delay(2);
      yield();
      continue;
    }
    if (ch == '\n') {
      out[len] = '\0';
      return static_cast<int>(len);
    }
    if (ch != '\r' && len + 1 < cap) out[len++] = static_cast<char>(ch);
  }
  return -1;
}

// Parses the status line and headers, capturing Location for redirects.
bool readResponseHead(WiFiClient* client, uint32_t timeoutMs, int& code,
                      char* location, size_t locationSize) {
  const uint32_t deadline = millis() + timeoutMs;
  char line[160];

  if (readLine(client, line, sizeof(line), deadline) <= 0) return false;
  const char* space = strchr(line, ' ');
  if (space == nullptr) return false;
  code = atoi(space + 1);
  if (code < 100 || code > 599) return false;

  if (locationSize > 0) location[0] = '\0';
  size_t headerBytes = 0;
  while (headerBytes < 4096) {
    const int n = readLine(client, line, sizeof(line), deadline);
    if (n < 0) return false;
    if (n == 0) return true;  // blank line: end of headers
    headerBytes += static_cast<size_t>(n);
    if (locationSize > 0 && startsWithCI(line, "location:")) {
      const char* value = line + 9;
      while (*value == ' ' || *value == '\t') ++value;
      copyStr(location, locationSize, value);
    }
  }
  return true;
}

// Resolves absolute, protocol-relative and root-relative redirect targets.
bool resolveLocation(const char* location, char* host, size_t hostSize,
                     uint16_t& port, bool& https, char* path, size_t pathSize,
                     char* err, size_t errSize) {
  if (location == nullptr || *location == '\0') {
    setErr(err, errSize, "redirect has no location");
    return false;
  }
  if (startsWithCI(location, "http://") || startsWithCI(location, "https://")) {
    return parseHttpUrl(location, host, hostSize, port, https, path, pathSize, err,
                        errSize);
  }
  if (location[0] == '/' && location[1] == '/') {
    char absolute[192];
    snprintf(absolute, sizeof(absolute), "%s:%s", https ? "https" : "http",
             location);
    return parseHttpUrl(absolute, host, hostSize, port, https, path, pathSize, err,
                        errSize);
  }
  if (location[0] == '/') {
    const char* hash = strchr(location, '#');
    const size_t len =
        hash != nullptr ? static_cast<size_t>(hash - location) : strlen(location);
    if (len + 1 > pathSize) {
      setErr(err, errSize, "redirect path is too long");
      return false;
    }
    memcpy(path, location, len);
    path[len] = '\0';
    return true;
  }
  setErr(err, errSize, "unsupported redirect");
  return false;
}

// Streaming substring search so a large body never has to be buffered.
bool bodyContains(WiFiClient* client, const char* needle, uint32_t timeoutMs,
                  size_t maxBytes) {
  const size_t n = strlen(needle);
  if (n == 0) return true;
  char window[64];
  size_t filled = 0;
  size_t total = 0;
  const uint32_t deadline = millis() + timeoutMs;

  while (total < maxBytes && !timeReached(millis(), deadline)) {
    const int ch = client->read();
    if (ch < 0) {
      if (!client->connected() && client->available() == 0) break;
      delay(2);
      yield();
      continue;
    }
    ++total;
    if (filled < n) {
      window[filled++] = static_cast<char>(ch);
    } else {
      memmove(window, window + 1, n - 1);
      window[n - 1] = static_cast<char>(ch);
    }
    if (filled == n && memcmp(window, needle, n) == 0) return true;
  }
  return false;
}

bool checkTcp(const Monitor& m, uint32_t timeoutMs, CheckOutcome& out) {
  WiFiClient client;
  client.setTimeout(timeoutMs);

  const uint32_t started = millis();
  const bool connected = client.connect(m.host, m.port) == 1;
  out.latencyMs = clampMs(millis() - started);
  out.ok = connected;
  if (!connected) out.error = "tcp connect failed";
  client.stop();
  return connected;
}

bool checkHttp(const Monitor& m, uint32_t timeoutMs, CheckOutcome& out) {
  char host[64];
  char path[96];
  uint16_t port = m.port;
  bool https = m.https;
  copyStr(host, sizeof(host), m.host);
  copyStr(path, sizeof(path), m.path[0] != '\0' ? m.path : "/");

  const uint32_t started = millis();
  const uint8_t maxHops =
      m.maxRedirects > kMaxRedirects ? kMaxRedirects : m.maxRedirects;

  for (uint8_t hop = 0;; ++hop) {
    if (https && ESP.getFreeHeap() < 20000) {
      out.latencyMs = clampMs(millis() - started);
      out.error = "low heap for tls";
      return false;
    }
    std::unique_ptr<WiFiClient> client;
    if (!openClient(host, port, https, timeoutMs, client)) {
      out.latencyMs = clampMs(millis() - started);
      out.error = "connect failed";
      return false;
    }

    char hostHeader[96];
    const bool defaultPort = (https && port == 443) || (!https && port == 80);
    if (defaultPort) {
      copyStr(hostHeader, sizeof(hostHeader), host);
    } else {
      snprintf(hostHeader, sizeof(hostHeader), "%s:%u", host,
               static_cast<unsigned>(port));
    }

    client->printf(
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: uptime-monitor/%s\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n",
        path, hostHeader, UM_FW_VERSION);

    int code = 0;
    char location[192];
    if (!readResponseHead(client.get(), timeoutMs, code, location,
                          sizeof(location))) {
      out.latencyMs = clampMs(millis() - started);
      out.error = "no http response";
      client->stop();
      return false;
    }

    if (code >= 300 && code < 400 && location[0] != '\0') {
      if (hop < maxHops) {
        client->stop();
        char redirectErr[64] = {0};
        if (!resolveLocation(location, host, sizeof(host), port, https, path,
                             sizeof(path), redirectErr, sizeof(redirectErr))) {
          out.latencyMs = clampMs(millis() - started);
          out.error = "bad redirect";
          return false;
        }
        continue;
      }
      if (maxHops > 0) {
        out.latencyMs = clampMs(millis() - started);
        out.error = "too many redirects";
        client->stop();
        return false;
      }
    }

    out.httpStatus = code > 0 && code <= 65535 ? static_cast<uint16_t>(code) : 0;
    if (!statusAccepted(m.acceptedStatus, code)) {
      out.latencyMs = clampMs(millis() - started);
      out.error = "unexpected http status";
      client->stop();
      return false;
    }

    if (m.keyword[0] != '\0' &&
        !bodyContains(client.get(), m.keyword, timeoutMs, 16384)) {
      out.latencyMs = clampMs(millis() - started);
      out.error = "keyword not found";
      client->stop();
      return false;
    }

    out.ok = true;
    out.latencyMs = clampMs(millis() - started);
    client->stop();
    return true;
  }
}

bool checkPing(const Monitor& m, uint32_t, CheckOutcome& out) {
  const uint32_t started = millis();
  const bool alive = Ping.ping(m.host, 1);
  const uint32_t elapsed = millis() - started;
  out.ok = alive;
  if (alive) {
    const int avg = Ping.averageTime();
    out.latencyMs = clampMs(avg > 0 ? static_cast<uint32_t>(avg) : elapsed);
  } else {
    out.error = "no icmp reply";
    out.latencyMs = clampMs(elapsed);
  }
  return alive;
}

bool openClient(const char* host, uint16_t port, bool https, uint32_t timeoutMs,
                std::unique_ptr<WiFiClient>& client) {
  if (https) {
    // Default 16 KB RX + SSL contexts needs ~17 KB; with ~28 KB free that has
    // corrupted the heap during handshake. 8 KB still holds normal cert chains.
    if (ESP.getFreeHeap() < 20000) return false;
    auto* secure = new (std::nothrow) BearSSL::WiFiClientSecure();
    if (secure == nullptr) return false;
    secure->setInsecure();
    secure->setBufferSizes(8192, 512);
    secure->setTimeout(timeoutMs > 12000 ? timeoutMs : 12000);
    client.reset(secure);
  } else {
    auto* plain = new (std::nothrow) WiFiClient();
    if (plain == nullptr) return false;
    plain->setTimeout(timeoutMs);
    client.reset(plain);
  }
  const bool ok = client->connect(host, port) == 1;
  if (ok) client->setTimeout(timeoutMs);
  return ok;
}

}  // namespace

bool runCheck(const Monitor& m, uint32_t timeoutMs, CheckOutcome& out) {
  out = CheckOutcome();
  const uint32_t effectiveTimeout = m.timeoutMs != 0 ? m.timeoutMs : timeoutMs;
  switch (m.type) {
    case MonitorType::TcpPort:
      return checkTcp(m, effectiveTimeout, out);
    case MonitorType::Ping:
      return checkPing(m, effectiveTimeout, out);
    case MonitorType::Http:
      return checkHttp(m, effectiveTimeout, out);
  }
  out.error = "unknown monitor type";
  return false;
}

void isoTimestamp(char* out, size_t outSize) {
  if (outSize == 0) return;
  const time_t now = time(nullptr);
  if (now < 1600000000) {  // NTP has not synced yet
    snprintf(out, outSize, "uptime:%lu",
             static_cast<unsigned long>(millis() / 1000u));
    return;
  }
  struct tm tmNow;
  gmtime_r(&now, &tmNow);
  strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &tmNow);
}

bool postWebhook(const DeviceConfig& cfg, const Monitor& m, const Notification& n,
                 const char* deviceIp, char* err, size_t errLen) {
  if (cfg.webhookUrl[0] == '\0') {
    setErr(err, errLen, "webhook not configured");
    return false;
  }

  char host[64];
  char path[192];  // Discord's /api/webhooks/<id>/<token> exceeds 96 chars
  uint16_t port = 0;
  bool https = false;
  if (!parseHttpUrl(cfg.webhookUrl, host, sizeof(host), port, https, path,
                    sizeof(path), err, errLen)) {
    return false;
  }

  JsonDocument doc;
  char target[128];
  buildMonitorUrl(m, target, sizeof(target));
  char when[32];
  isoTimestamp(when, sizeof(when));
  const char* ip = deviceIp != nullptr ? deviceIp : "";

  if (cfg.webhookFormat == WebhookFormat::Discord) {
    char detail[128];
    if (n.kind == EventKind::Up) {
      snprintf(detail, sizeof(detail), "Recovered");
    } else if (n.kind == EventKind::Resend) {
      snprintf(detail, sizeof(detail), "Still down - failure %u%s%s",
               static_cast<unsigned>(n.failures), m.lastError[0] ? " - " : "",
               m.lastError);
    } else {
      snprintf(detail, sizeof(detail), "Failure %u%s%s",
               static_cast<unsigned>(n.failures), m.lastError[0] ? " - " : "",
               m.lastError);
    }
    const char* tag = n.kind == EventKind::Up
                          ? "UP"
                          : (n.kind == EventKind::Resend ? "STILL DOWN" : "DOWN");
    // Built on the heap: a 512-byte stack buffer here is enough to overflow the
    // 4 KB stack when BearSSL is also on the call path.
    String content;
    content.reserve(384);
    content += "[";
    content += tag;
    content += "] **";
    content += m.name;
    content += "** (";
    content += monitorTypeName(m.type);
    content += ")\n`";
    content += target;
    content += "`\n";
    content += detail;
    content += "\nDevice: ";
    content += cfg.deviceName;
    content += " (";
    content += ip;
    content += ")\nTime: ";
    content += when;
    doc["username"] = cfg.deviceName;
    doc["content"] = content;
    JsonObject mentions = doc["allowed_mentions"].to<JsonObject>();
    mentions["parse"].to<JsonArray>();  // never ping @everyone / roles
  } else {
    doc["event"] = eventName(n.kind);
    doc["status"] = n.kind == EventKind::Up ? "up" : "down";
    doc["monitor"] = m.name;
    doc["id"] = m.id;
    doc["type"] = monitorTypeName(m.type);
    doc["target"] = target;
    doc["failures"] = n.failures;
    doc["latency_ms"] = m.lastLatencyMs;
    if (m.type == MonitorType::Http) doc["http_status"] = m.lastHttpStatus;
    doc["error"] = m.lastError;
    doc["time"] = when;
    doc["uptime_s"] = millis() / 1000u;
    doc["device"] = cfg.deviceName;
    doc["ip"] = ip;
  }

  String body;
  body.reserve(512);
  serializeJson(doc, body);

  std::unique_ptr<WiFiClient> client;
  if (!openClient(host, port, https, 6000, client)) {
    setErr(err, errLen, "webhook connect failed");
    return false;
  }

  char hostHeader[80];
  const bool defaultPort = (https && port == 443) || (!https && port == 80);
  if (defaultPort) {
    copyStr(hostHeader, sizeof(hostHeader), host);
  } else {
    snprintf(hostHeader, sizeof(hostHeader), "%s:%u", host,
             static_cast<unsigned>(port));
  }

  client->printf(
      "POST %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "User-Agent: uptime-monitor/%s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %u\r\n"
      "Connection: close\r\n",
      path, hostHeader, UM_FW_VERSION, static_cast<unsigned>(body.length()));

  if (cfg.webhookToken[0] != '\0') {
    client->printf("Authorization: Bearer %s\r\n", cfg.webhookToken);
  }
  client->print("\r\n");
  client->print(body);

  int code = 0;
  char location[1] = {0};
  readResponseHead(client.get(), 6000, code, location, sizeof(location));
  client->stop();

  if (code < 200 || code >= 300) {
    if (code == 0) {
      setErr(err, errLen, "no webhook response");
    } else {
      snprintf(err, errLen, "webhook returned %d", code);
    }
    return false;
  }
  return true;
}

}  // namespace um
