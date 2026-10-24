#include <unity.h>

#include <string.h>

#include "config_codec.h"
#include "scheduler.h"

using namespace um;

void setUp() {}
void tearDown() {}

namespace {

bool parseUrl(const char* url, char* host, uint16_t& port, bool& https, char* path) {
  char err[128] = {0};
  return parseHttpUrl(url, host, 64, port, https, path, 96, err, sizeof(err));
}

void test_url_defaults_to_http() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = true;
  TEST_ASSERT_TRUE(parseUrl("example.com", host, port, https, path));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_UINT16(80, port);
  TEST_ASSERT_FALSE(https);
  TEST_ASSERT_EQUAL_STRING("/", path);
}

void test_url_https_default_port() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = false;
  TEST_ASSERT_TRUE(parseUrl("https://example.com", host, port, https, path));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_UINT16(443, port);
  TEST_ASSERT_TRUE(https);
  TEST_ASSERT_EQUAL_STRING("/", path);
}

void test_url_explicit_port_and_query() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = false;
  TEST_ASSERT_TRUE(parseUrl("http://example.com:8080/health?x=1&y=2", host, port,
                            https, path));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_UINT16(8080, port);
  TEST_ASSERT_FALSE(https);
  TEST_ASSERT_EQUAL_STRING("/health?x=1&y=2", path);
}

void test_url_query_without_path() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = false;
  TEST_ASSERT_TRUE(parseUrl("https://example.com?x=1", host, port, https, path));
  TEST_ASSERT_EQUAL_STRING("/?x=1", path);
}

void test_url_fragment_stripped() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = false;
  TEST_ASSERT_TRUE(parseUrl("https://example.com/path#section", host, port, https,
                            path));
  TEST_ASSERT_EQUAL_STRING("/path", path);
}

void test_url_rejects_invalid() {
  char host[64];
  char path[96];
  uint16_t port = 0;
  bool https = false;
  char err[128];
  const char* bad[] = {
      "",                       // empty
      "ftp://example.com",      // unsupported scheme
      "http://",                // missing host
      "https://host:0",         // port 0
      "https://host:99999",     // port too large
      "http://bad host/path",   // space in host
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    TEST_ASSERT_FALSE_MESSAGE(
        parseHttpUrl(bad[i], host, sizeof(host), port, https, path, sizeof(path),
                     err, sizeof(err)),
        bad[i]);
  }
}

void test_round_trip() {
  DeviceConfig cfg;
  defaultConfig(cfg);
  copyStr(cfg.deviceName, sizeof(cfg.deviceName), "lab-monitor");
  copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), "home");
  copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), "secret123");
  copyStr(cfg.webhookUrl, sizeof(cfg.webhookUrl), "https://example.com/hook");
  copyStr(cfg.webhookToken, sizeof(cfg.webhookToken), "tok");
  cfg.notifyOnRecovery = false;
  cfg.timeoutMs = 7000;
  cfg.monitorCount = 2;

  Monitor& a = cfg.monitors[0];
  a = Monitor();
  a.id = 7;
  a.type = MonitorType::TcpPort;
  copyStr(a.name, sizeof(a.name), "app");
  copyStr(a.host, sizeof(a.host), "192.0.2.10");
  a.port = 22;
  a.intervalSec = 60;
  a.retries = 3;
  a.retryIntervalSec = 60;

  Monitor& b = cfg.monitors[1];
  b = Monitor();
  b.id = 9;
  b.type = MonitorType::Http;
  copyStr(b.name, sizeof(b.name), "web");
  copyStr(b.host, sizeof(b.host), "example.com");
  b.port = 443;
  b.https = true;
  copyStr(b.path, sizeof(b.path), "/health?x=1");
  copyStr(b.description, sizeof(b.description), "public site");
  b.intervalSec = 120;
  b.retries = 1;
  b.retryIntervalSec = 15;
  b.resendEvery = 5;
  b.upsideDown = true;
  b.notify = false;

  JsonDocument doc;
  configToJson(cfg, doc);

  DeviceConfig out;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, out, err, sizeof(err)), err);

  TEST_ASSERT_EQUAL_STRING("lab-monitor", out.deviceName);
  TEST_ASSERT_EQUAL_STRING("home", out.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("secret123", out.wifiPass);
  TEST_ASSERT_EQUAL_STRING("https://example.com/hook", out.webhookUrl);
  TEST_ASSERT_EQUAL_STRING("tok", out.webhookToken);
  TEST_ASSERT_FALSE(out.notifyOnRecovery);
  TEST_ASSERT_EQUAL_UINT32(7000, out.timeoutMs);
  TEST_ASSERT_EQUAL_UINT8(2, out.monitorCount);

  TEST_ASSERT_EQUAL_UINT8(7, out.monitors[0].id);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorType::TcpPort),
                        static_cast<int>(out.monitors[0].type));
  TEST_ASSERT_EQUAL_STRING("app", out.monitors[0].name);
  TEST_ASSERT_EQUAL_STRING("192.0.2.10", out.monitors[0].host);
  TEST_ASSERT_EQUAL_UINT16(22, out.monitors[0].port);
  TEST_ASSERT_EQUAL_UINT32(60, out.monitors[0].intervalSec);
  TEST_ASSERT_EQUAL_UINT8(3, out.monitors[0].retries);

  TEST_ASSERT_EQUAL_UINT8(9, out.monitors[1].id);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorType::Http),
                        static_cast<int>(out.monitors[1].type));
  TEST_ASSERT_EQUAL_STRING("example.com", out.monitors[1].host);
  TEST_ASSERT_EQUAL_UINT16(443, out.monitors[1].port);
  TEST_ASSERT_TRUE(out.monitors[1].https);
  TEST_ASSERT_EQUAL_STRING("/health?x=1", out.monitors[1].path);
  TEST_ASSERT_EQUAL_UINT32(120, out.monitors[1].intervalSec);
  TEST_ASSERT_EQUAL_UINT8(1, out.monitors[1].retries);
  TEST_ASSERT_EQUAL_UINT32(15, out.monitors[1].retryIntervalSec);
  TEST_ASSERT_EQUAL_UINT16(5, out.monitors[1].resendEvery);
  TEST_ASSERT_TRUE(out.monitors[1].upsideDown);
  TEST_ASSERT_FALSE(out.monitors[1].notify);

  char url[128];
  buildMonitorUrl(out.monitors[1], url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("https://example.com/health?x=1", url);
  buildMonitorUrl(out.monitors[0], url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("192.0.2.10:22", url);
}

void test_url_field_overrides_components() {
  const char* json = R"({
    "monitors": [{
      "type": "http", "name": "site", "host": "stale.example", "port": 1,
      "url": "https://right.example/ok", "interval": 60, "retries": 3,
      "retry_interval": 60
    }]
  })";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_STRING("right.example", cfg.monitors[0].host);
  TEST_ASSERT_EQUAL_UINT16(443, cfg.monitors[0].port);
  TEST_ASSERT_TRUE(cfg.monitors[0].https);
  TEST_ASSERT_EQUAL_STRING("/ok", cfg.monitors[0].path);
}

void test_ids_are_assigned_and_deduplicated() {
  const char* json = R"({
    "monitors": [
      {"type":"ping","name":"a","host":"1.1.1.1"},
      {"type":"ping","name":"b","host":"8.8.8.8","id":2},
      {"type":"ping","name":"c","host":"9.9.9.9","id":2}
    ]
  })";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.monitorCount);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.monitors[0].id);
  TEST_ASSERT_EQUAL_UINT8(2, cfg.monitors[1].id);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.monitors[2].id);
}

void test_name_defaults_to_host() {
  const char* json = R"({"monitors":[{"type":"tcp","host":"10.0.0.5","port":443}]})";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", cfg.monitors[0].name);
}

void test_validation_rejects_out_of_range() {
  const char* bad[] = {
      R"({"monitors":[{"type":"tcp","name":"x","host":"h","port":554,"interval":0}]})",
      R"({"monitors":[{"type":"tcp","name":"x","host":"h","port":554,"retries":21}]})",
      R"({"monitors":[{"type":"tcp","name":"x","host":"h","port":0}]})",
      R"({"monitors":[{"type":"tcp","name":"x","host":"h"}]})",
      R"({"monitors":[{"type":"carrier-pigeon","name":"x","host":"h"}]})",
      R"({"webhook":{"url":"example.com/hook"}})",
      R"({"timeout_ms":50})",
      R"({"device_name":""})",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    JsonDocument doc;
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, bad[i]), bad[i]);
    DeviceConfig cfg;
    char err[160] = {0};
    TEST_ASSERT_FALSE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), bad[i]);
  }
}

void test_too_many_monitors_rejected() {
  JsonDocument doc;
  JsonArray arr = doc["monitors"].to<JsonArray>();
  for (int i = 0; i < kMaxMonitors + 1; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["type"] = "ping";
    o["name"] = "m";
    o["host"] = "1.1.1.1";
  }
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_FALSE(configFromJson(doc, cfg, err, sizeof(err)));
}

void test_ping_clears_port() {
  const char* json = R"({"monitors":[{"type":"ping","name":"dns","host":"1.1.1.1","port":53}]})";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.monitors[0].port);
}

void test_default_config_is_valid() {
  DeviceConfig cfg;
  defaultConfig(cfg);
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(validateConfig(cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.monitorCount);
  TEST_ASSERT_EQUAL_STRING("uptime-monitor", cfg.deviceName);
}

void test_status_spec_matching() {
  TEST_ASSERT_TRUE(statusAccepted("200-299", 200));
  TEST_ASSERT_TRUE(statusAccepted("200-299", 299));
  TEST_ASSERT_FALSE(statusAccepted("200-299", 300));
  TEST_ASSERT_FALSE(statusAccepted("200-299", 199));
  TEST_ASSERT_TRUE(statusAccepted("200,204,301-302", 204));
  TEST_ASSERT_TRUE(statusAccepted("200,204,301-302", 302));
  TEST_ASSERT_FALSE(statusAccepted("200,204,301-302", 404));
  TEST_ASSERT_TRUE(statusAccepted("", 204));   // empty = default 2xx
  TEST_ASSERT_FALSE(statusAccepted("", 301));
}

void test_status_spec_validation() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(validateStatusSpec("200-299", err, sizeof(err)));
  TEST_ASSERT_TRUE(validateStatusSpec("200,204,301-302", err, sizeof(err)));
  TEST_ASSERT_TRUE(validateStatusSpec("100-599", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("99", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("600", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("20-299", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("200-", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("300-200", err, sizeof(err)));
  TEST_ASSERT_FALSE(validateStatusSpec("abc", err, sizeof(err)));
}

void test_http_options_round_trip() {
  const char* json = R"({
    "webhook": { "format": "discord" },
    "monitors": [{
      "type": "http", "name": "k", "url": "https://example.com/x",
      "keyword": "hello world", "accepted_status": "200,204,301-302",
      "max_redirects": 7, "timeout_ms": 48000,
      "interval": 60, "retries": 0, "retry_interval": 60
    }]
  })";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WebhookFormat::Discord),
                        static_cast<int>(cfg.webhookFormat));
  const Monitor& m = cfg.monitors[0];
  TEST_ASSERT_EQUAL_STRING("hello world", m.keyword);
  TEST_ASSERT_EQUAL_STRING("200,204,301-302", m.acceptedStatus);
  TEST_ASSERT_EQUAL_UINT8(7, m.maxRedirects);
  TEST_ASSERT_EQUAL_UINT16(48000, m.timeoutMs);
  TEST_ASSERT_TRUE(m.https);
  TEST_ASSERT_EQUAL_STRING("/x", m.path);
}

void test_invalid_status_spec_rejected() {
  const char* json =
      R"({"monitors":[{"type":"http","url":"http://h/","accepted_status":"abc"}]})";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_FALSE(configFromJson(doc, cfg, err, sizeof(err)));
}

void test_invalid_webhook_format_rejected() {
  const char* json = R"({"webhook":{"format":"slack"}})";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_FALSE(configFromJson(doc, cfg, err, sizeof(err)));
}

void test_webhook_format_defaults_to_generic() {
  const char* json = R"({"webhook":{"url":"https://example.com/hook"}})";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(configFromJson(doc, cfg, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WebhookFormat::Generic),
                        static_cast<int>(cfg.webhookFormat));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_url_defaults_to_http);
  RUN_TEST(test_url_https_default_port);
  RUN_TEST(test_url_explicit_port_and_query);
  RUN_TEST(test_url_query_without_path);
  RUN_TEST(test_url_fragment_stripped);
  RUN_TEST(test_url_rejects_invalid);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_url_field_overrides_components);
  RUN_TEST(test_ids_are_assigned_and_deduplicated);
  RUN_TEST(test_name_defaults_to_host);
  RUN_TEST(test_validation_rejects_out_of_range);
  RUN_TEST(test_too_many_monitors_rejected);
  RUN_TEST(test_ping_clears_port);
  RUN_TEST(test_default_config_is_valid);
  RUN_TEST(test_status_spec_matching);
  RUN_TEST(test_status_spec_validation);
  RUN_TEST(test_http_options_round_trip);
  RUN_TEST(test_invalid_status_spec_rejected);
  RUN_TEST(test_invalid_webhook_format_rejected);
  RUN_TEST(test_webhook_format_defaults_to_generic);
  return UNITY_END();
}
