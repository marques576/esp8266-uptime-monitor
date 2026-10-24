#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>

#include <ArduinoJson.h>

#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "checks.h"
#include "config_codec.h"
#include "monitor.h"
#include "scheduler.h"
#include "webui.h"

using namespace um;

namespace {

const char* const kConfigPath = "/config.json";
const char* const kApPassword = "uptime1234";
const char* const kMask = "********";

constexpr uint8_t kWebhookSlots = 6;
constexpr uint8_t kWebhookMaxAttempts = 4;

DeviceConfig gCfg;
ESP8266WebServer server(80);
bool gFsReady = false;
bool gApMode = false;
bool gMdnsStarted = false;
bool gTimeConfigured = false;
uint32_t gRebootAtMs = 0;

struct WebhookTask {
  bool used = false;
  Notification n;
  uint8_t attempts = 0;
  uint32_t dueMs = 0;
};

WebhookTask gHooks[kWebhookSlots];

enum class TestHookState : uint8_t { Idle = 0, Queued = 1, Sent = 2, Failed = 3 };
TestHookState gTestHookState = TestHookState::Idle;
char gTestHookMsg[64] = {0};
Monitor gTestMonitor;  // synthetic monitor; kept off the web-server stack

const char* testHookStateName(TestHookState s) {
  switch (s) {
    case TestHookState::Idle: return "idle";
    case TestHookState::Queued: return "queued";
    case TestHookState::Sent: return "sent";
    case TestHookState::Failed: return "failed";
  }
  return "idle";
}

void sendJson(int code, const JsonDocument& doc) {
  String out;
  out.reserve(1024);
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message != nullptr ? message : "error";
  sendJson(code, doc);
}

bool requireAuth() {
  if (gCfg.uiPass[0] == '\0') return true;
  const char* user = gCfg.uiUser[0] != '\0' ? gCfg.uiUser : "admin";
  if (server.authenticate(user, gCfg.uiPass)) return true;
  server.requestAuthentication();
  return false;
}

bool saveConfig() {
  if (!gFsReady) return false;
  File f = LittleFS.open(kConfigPath, "w");
  if (!f) return false;
  JsonDocument doc;
  configToJson(gCfg, doc);
  const size_t written = serializeJsonPretty(doc, f);
  f.close();
  return written > 0;
}

void loadConfig() {
  defaultConfig(gCfg);
  if (!gFsReady) return;

  File f = LittleFS.open(kConfigPath, "r");
  if (!f) {
    saveConfig();
    return;
  }
  JsonDocument doc;
  const DeserializationError derr = deserializeJson(doc, f);
  f.close();

  char err[160] = {0};
  if (derr || !configFromJson(doc, gCfg, err, sizeof(err))) {
    Serial.printf("[config] load failed (%s), using defaults\n",
                  derr ? derr.c_str() : err);
    defaultConfig(gCfg);
    saveConfig();
  }
}

Monitor* findMonitor(uint8_t id) {
  for (uint8_t i = 0; i < gCfg.monitorCount; ++i) {
    if (gCfg.monitors[i].id == id) return &gCfg.monitors[i];
  }
  return nullptr;
}

// Preserve runtime state for monitors that survive a config save (same id,
// type and target) and schedule brand-new ones.
void carryRuntime(const DeviceConfig& oldCfg, DeviceConfig& newCfg, uint32_t now) {
  for (uint8_t i = 0; i < newCfg.monitorCount; ++i) {
    Monitor& m = newCfg.monitors[i];
    bool carried = false;
    for (uint8_t j = 0; j < oldCfg.monitorCount; ++j) {
      const Monitor& o = oldCfg.monitors[j];
      if (o.id != m.id || o.type != m.type) continue;
      if (strcmp(o.host, m.host) != 0 || o.port != m.port) continue;

      m.state = o.state;
      m.consecutiveFailures = o.consecutiveFailures;
      m.downBeats = o.downBeats;
      m.nextDueMs = o.nextDueMs;
      m.lastCheckMs = o.lastCheckMs;
      m.lastLatencyMs = o.lastLatencyMs;
      m.lastHttpStatus = o.lastHttpStatus;
      copyStr(m.lastError, sizeof(m.lastError), o.lastError);

      const uint32_t maxDue = now + m.intervalSec * 1000u;
      if (timeReached(m.nextDueMs, maxDue)) m.nextDueMs = now + 500u;
      carried = true;
      break;
    }
    if (!carried) staggerInitial(m, i, now);
  }
}

void startAp() {
  gApMode = true;
  WiFi.mode(WIFI_AP_STA);
  const char* ssid = gCfg.deviceName[0] != '\0' ? gCfg.deviceName : "uptime-monitor";
  WiFi.softAP(ssid, kApPassword);
  Serial.printf("[wifi] setup AP '%s' pass '%s' -> %s\n", ssid, kApPassword,
                WiFi.softAPIP().toString().c_str());
}

void setupWifi() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (gCfg.deviceName[0] != '\0') WiFi.hostname(gCfg.deviceName);

  if (gCfg.wifiSsid[0] == '\0') {
    startAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000u) {
    delay(100);
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected to '%s' -> %s\n", gCfg.wifiSsid,
                  WiFi.localIP().toString().c_str());
    return;
  }
  Serial.println("[wifi] station connect timed out");
  startAp();
}

void onNetworkUp() {
  if (!gTimeConfigured) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    gTimeConfigured = true;
  }
  if (!gMdnsStarted) {
    gMdnsStarted = MDNS.begin(gCfg.deviceName);
    if (gMdnsStarted) MDNS.addService("http", "tcp", 80);
  }
}

void maintainWifi(uint32_t now) {
  static uint32_t lastAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) {
    onNetworkUp();
    return;
  }
  if (gCfg.wifiSsid[0] == '\0') return;
  if (lastAttempt != 0 && now - lastAttempt < 15000u) return;
  lastAttempt = now;
  Serial.println("[wifi] reconnecting");
  WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
}

void enqueueWebhook(const Notification& n) {
  int slot = -1;
  for (uint8_t i = 0; i < kWebhookSlots; ++i) {
    if (!gHooks[i].used) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    slot = 0;
    Serial.println("[webhook] queue full, dropping oldest");
  }
  gHooks[slot].used = true;
  gHooks[slot].n = n;
  gHooks[slot].attempts = 0;
  gHooks[slot].dueMs = millis();
}

void serviceWebhooks(uint32_t now) {
  if (gCfg.webhookUrl[0] == '\0') {
    for (uint8_t i = 0; i < kWebhookSlots; ++i) gHooks[i].used = false;
    return;
  }
  for (uint8_t i = 0; i < kWebhookSlots; ++i) {
    WebhookTask& t = gHooks[i];
    if (!t.used || !timeReached(now, t.dueMs)) continue;

    Monitor* m = findMonitor(t.n.monitorId);
    if (m == nullptr) {
      t.used = false;
      return;
    }

    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                : WiFi.softAPIP().toString();
    char err[64] = {0};
    if (postWebhook(gCfg, *m, t.n, ip.c_str(), err, sizeof(err))) {
      Serial.printf("[webhook] %s sent for '%s'\n", eventName(t.n.kind), m->name);
      t.used = false;
    } else {
      ++t.attempts;
      Serial.printf("[webhook] attempt %u/%u failed: %s\n",
                    static_cast<unsigned>(t.attempts),
                    static_cast<unsigned>(kWebhookMaxAttempts), err);
      if (t.attempts >= kWebhookMaxAttempts) {
        t.used = false;
      } else {
        t.dueMs = millis() + (2000u << t.attempts);  // 4 s, 8 s, 16 s
      }
    }
    return;  // one blocking delivery per loop
  }
}

void serviceTestWebhook() {
  if (gTestHookState != TestHookState::Queued) return;
  if (WiFi.status() != WL_CONNECTED) return;

  Monitor& probe = gTestMonitor;
  probe.id = 0;
  probe.type = MonitorType::Http;
  copyStr(probe.name, sizeof(probe.name), "test webhook");
  copyStr(probe.host, sizeof(probe.host), "example.com");
  probe.port = 443;
  probe.https = true;
  copyStr(probe.path, sizeof(probe.path), "/");
  copyStr(probe.lastError, sizeof(probe.lastError), "test event");
  probe.lastLatencyMs = 0;
  probe.lastHttpStatus = 0;

  Notification n;
  n.kind = EventKind::Down;
  n.failures = 0;

  String ip = WiFi.localIP().toString();
  char err[64] = {0};
  if (postWebhook(gCfg, probe, n, ip.c_str(), err, sizeof(err))) {
    gTestHookState = TestHookState::Sent;
    copyStr(gTestHookMsg, sizeof(gTestHookMsg), "delivered");
    Serial.println("[webhook] test delivered");
  } else {
    gTestHookState = TestHookState::Failed;
    copyStr(gTestHookMsg, sizeof(gTestHookMsg), err[0] != '\0' ? err : "failed");
    Serial.printf("[webhook] test failed: %s\n", err);
  }
}

void runDueCheck(uint32_t now) {
  int8_t pick = -1;
  for (uint8_t i = 0; i < gCfg.monitorCount; ++i) {
    const Monitor& m = gCfg.monitors[i];
    if (!isDue(m, now)) continue;
    if (pick < 0 || timeReached(gCfg.monitors[pick].nextDueMs, m.nextDueMs)) {
      pick = static_cast<int8_t>(i);
    }
  }
  if (pick < 0) return;

  Monitor& m = gCfg.monitors[pick];
  CheckOutcome result;
  runCheck(m, gCfg.timeoutMs, result);

  const uint32_t finished = millis();
  Notification ev;
  const bool notify = applyCheckResult(m, finished, result, gCfg.notifyOnRecovery, ev);

  Serial.printf("[check] %-16s %-4s %-7s %4ums heap=%u%s%s\n", m.name,
                monitorTypeName(m.type), stateName(m.state),
                static_cast<unsigned>(m.lastLatencyMs), ESP.getFreeHeap(),
                result.ok ? "" : "  err=", result.ok ? "" : m.lastError);

  if (notify) enqueueWebhook(ev);
}

void updateLed(uint32_t now) {
  bool anyDown = false;
  for (uint8_t i = 0; i < gCfg.monitorCount; ++i) {
    if (gCfg.monitors[i].enabled && gCfg.monitors[i].state == MonitorState::Down) {
      anyDown = true;
      break;
    }
  }
  if (anyDown) {
    digitalWrite(LED_BUILTIN, ((now / 150u) % 2u) != 0 ? LOW : HIGH);
  } else {
    digitalWrite(LED_BUILTIN, (now % 2000u) < 60u ? LOW : HIGH);
  }
}

void handleState() {
  if (!requireAuth()) return;

  JsonDocument doc;
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  String ip = wifiUp ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  doc["device"] = gCfg.deviceName;
  doc["fw"] = UM_FW_VERSION;
  doc["ip"] = ip;
  doc["wifi"] = wifiUp;
  doc["ap"] = gApMode;
  doc["ssid"] = wifiUp ? WiFi.SSID() : "";
  doc["rssi"] = wifiUp ? WiFi.RSSI() : 0;
  doc["uptime_s"] = millis() / 1000u;
  doc["heap"] = ESP.getFreeHeap();
  doc["webhook_configured"] = gCfg.webhookUrl[0] != '\0';
  doc["test_webhook"] = testHookStateName(gTestHookState);
  doc["test_webhook_msg"] = gTestHookMsg;
  char ts[32];
  isoTimestamp(ts, sizeof(ts));
  doc["time"] = ts;

  JsonArray arr = doc["monitors"].to<JsonArray>();
  for (uint8_t i = 0; i < gCfg.monitorCount; ++i) {
    const Monitor& m = gCfg.monitors[i];
    JsonObject o = arr.add<JsonObject>();
    char target[128];
    buildMonitorUrl(m, target, sizeof(target));
    o["id"] = m.id;
    o["name"] = m.name;
    o["type"] = monitorTypeName(m.type);
    o["target"] = target;
    o["state"] = stateName(m.state);
    o["enabled"] = m.enabled;
    o["failures"] = m.consecutiveFailures;
    o["latency_ms"] = m.lastLatencyMs;
    o["http_status"] = m.lastHttpStatus;
    o["last_error"] = m.lastError;
    o["last_check_s"] = m.lastCheckMs / 1000u;
    o["interval"] = m.intervalSec;
    o["retries"] = m.retries;
    o["retry_interval"] = m.retryIntervalSec;
    o["resend_every"] = m.resendEvery;
    o["upside_down"] = m.upsideDown;
    o["notify"] = m.notify;
    o["description"] = m.description;
  }

  sendJson(200, doc);
}

void handleConfigGet() {
  if (!requireAuth()) return;
  JsonDocument doc;
  configToJson(gCfg, doc);

  JsonObject wifi = doc["wifi"].as<JsonObject>();
  wifi["pass"] = gCfg.wifiPass[0] != '\0' ? kMask : "";
  JsonObject hook = doc["webhook"].as<JsonObject>();
  hook["token"] = gCfg.webhookToken[0] != '\0' ? kMask : "";
  JsonObject ui = doc["ui"].as<JsonObject>();
  ui["pass"] = gCfg.uiPass[0] != '\0' ? kMask : "";

  sendJson(200, doc);
}

void handleConfigPost() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) {
    sendError(400, "missing request body");
    return;
  }

  JsonDocument doc;
  const DeserializationError derr = deserializeJson(doc, server.arg("plain"));
  if (derr) {
    sendError(400, derr.c_str());
    return;
  }

  char err[160] = {0};
  // Staged on the heap rather than in .bss: a second DeviceConfig costs ~6 KB of
  // permanently reserved RAM, which HTTPS cannot spare on this chip.
  std::unique_ptr<DeviceConfig> pending(new (std::nothrow) DeviceConfig());
  if (!pending) {
    sendError(500, "out of memory");
    return;
  }
  if (!configFromJson(doc, *pending, err, sizeof(err))) {
    sendError(400, err);
    return;
  }

  // Masked secrets mean "keep the existing value".
  if (strcmp(pending->wifiPass, kMask) == 0) {
    copyStr(pending->wifiPass, sizeof(pending->wifiPass), gCfg.wifiPass);
  }
  if (strcmp(pending->webhookToken, kMask) == 0) {
    copyStr(pending->webhookToken, sizeof(pending->webhookToken),
            gCfg.webhookToken);
  }
  if (strcmp(pending->uiPass, kMask) == 0) {
    copyStr(pending->uiPass, sizeof(pending->uiPass), gCfg.uiPass);
  }
  if (pending->uiPass[0] != '\0' && pending->uiUser[0] == '\0') {
    copyStr(pending->uiUser, sizeof(pending->uiUser), "admin");
  }

  if (!validateConfig(*pending, err, sizeof(err))) {
    sendError(400, err);
    return;
  }

  const bool wifiChanged =
      strcmp(pending->wifiSsid, gCfg.wifiSsid) != 0 ||
      strcmp(pending->wifiPass, gCfg.wifiPass) != 0;

  carryRuntime(gCfg, *pending, millis());
  gCfg = *pending;
  if (!saveConfig()) Serial.println("[config] write failed");

  JsonDocument resp;
  resp["ok"] = true;
  resp["reboot"] = wifiChanged;
  sendJson(200, resp);

  if (wifiChanged) gRebootAtMs = millis() + 1200u;
}

void handleTestWebhook() {
  if (!requireAuth()) return;
  if (gCfg.webhookUrl[0] == '\0') {
    sendError(400, "webhook URL is not configured");
    return;
  }

  // Sending from inside a web-server callback overflows the 4 KB stack once
  // BearSSL is involved. Queue it for loop() instead, where real alerts run.
  gTestHookState = TestHookState::Queued;
  copyStr(gTestHookMsg, sizeof(gTestHookMsg), "queued");

  JsonDocument resp;
  resp["ok"] = true;
  resp["queued"] = true;
  sendJson(200, resp);
}

void handleReboot() {
  if (!requireAuth()) return;
  JsonDocument resp;
  resp["ok"] = true;
  sendJson(200, resp);
  gRebootAtMs = millis() + 800u;
}

void registerRoutes() {
  server.on("/", HTTP_GET, []() {
    if (!requireAuth()) return;
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/test-webhook", HTTP_POST, handleTestWebhook);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.onNotFound([]() { sendError(404, "not found"); });
}

}  // namespace

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  Serial.printf("\n\nuptime-monitor %s (ESP8266)\n", UM_FW_VERSION);

  gFsReady = LittleFS.begin();
  if (!gFsReady) {
    Serial.println("[fs] mount failed, formatting");
    LittleFS.format();
    gFsReady = LittleFS.begin();
  }
  loadConfig();

  const uint32_t now = millis();
  for (uint8_t i = 0; i < gCfg.monitorCount; ++i) {
    staggerInitial(gCfg.monitors[i], i, now);
  }

  setupWifi();

  if (WiFi.status() == WL_CONNECTED) {
    onNetworkUp();
  }

  registerRoutes();
  server.begin();

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                              : WiFi.softAPIP().toString();
  Serial.printf("[http] http://%s/  monitors=%u  heap=%u\n", ip.c_str(),
                static_cast<unsigned>(gCfg.monitorCount), ESP.getFreeHeap());
}

void loop() {
  server.handleClient();
  if (gMdnsStarted) MDNS.update();

  const uint32_t now = millis();
  if (gRebootAtMs != 0 && timeReached(now, gRebootAtMs)) {
    Serial.println("[sys] rebooting");
    delay(100);
    ESP.restart();
  }

  maintainWifi(now);
  // Probes are meaningless without an uplink, and would turn every monitor
  // DOWN during AP-only setup. Wait for the station link instead.
  if (WiFi.status() == WL_CONNECTED) {
    runDueCheck(now);
    serviceWebhooks(now);
    serviceTestWebhook();
  }
  updateLed(now);
  yield();
}
