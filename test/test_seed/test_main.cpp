// Validates seed configs with the same codec the firmware uses.
//
// data/config.json is gitignored because it contains Wi-Fi credentials, the
// webhook secret and the monitor inventory, so that test is skipped when the
// file is absent. data/config.example.json is committed and must always pass.
#include <unity.h>

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "config_codec.h"

void setUp() {}
void tearDown() {}

namespace {

bool readFile(const char* path, char* buf, size_t cap, size_t& len) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return false;
  len = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[len] = '\0';
  return true;
}

bool readAny(const char* const* candidates, size_t count, char* buf, size_t cap,
             size_t& len) {
  for (size_t i = 0; i < count; ++i) {
    if (readFile(candidates[i], buf, cap, len)) return true;
  }
  return false;
}

void assertStructurallyValid(const um::DeviceConfig& cfg) {
  TEST_ASSERT_TRUE(cfg.monitorCount > 0);
  TEST_ASSERT_TRUE(cfg.monitorCount <= um::kMaxMonitors);
  TEST_ASSERT_TRUE(cfg.deviceName[0] != '\0');
  TEST_ASSERT_TRUE(cfg.webhookFormat == um::WebhookFormat::Generic ||
                   cfg.webhookFormat == um::WebhookFormat::Discord);

  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    const um::Monitor& m = cfg.monitors[i];
    TEST_ASSERT_TRUE(m.id != 0);
    TEST_ASSERT_TRUE(m.name[0] != '\0');
    TEST_ASSERT_TRUE(m.host[0] != '\0');
    TEST_ASSERT_TRUE(m.intervalSec >= um::kMinIntervalSec);
    TEST_ASSERT_TRUE(m.intervalSec <= um::kMaxIntervalSec);
    TEST_ASSERT_TRUE(m.retryIntervalSec >= um::kMinIntervalSec);
    if (m.type == um::MonitorType::TcpPort) {
      TEST_ASSERT_TRUE(m.port >= um::kMinPort);
    }
    if (m.type == um::MonitorType::Http) {
      TEST_ASSERT_TRUE(m.path[0] == '/');
      char statusErr[64] = {0};
      TEST_ASSERT_TRUE(
          um::validateStatusSpec(m.acceptedStatus, statusErr, sizeof(statusErr)));
    }
  }
}

void test_local_seed_config_is_valid() {
  static char buf[16384];
  const char* candidates[] = {"data/config.json", "../data/config.json",
                              "../../data/config.json"};
  size_t len = 0;
  if (!readAny(candidates, sizeof(candidates) / sizeof(candidates[0]), buf,
               sizeof(buf), len)) {
    TEST_IGNORE_MESSAGE("data/config.json not present");
    return;
  }

  JsonDocument doc;
  const DeserializationError derr = deserializeJson(doc, buf, len);
  TEST_ASSERT_FALSE_MESSAGE(derr, derr.c_str());

  um::DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(um::configFromJson(doc, cfg, err, sizeof(err)), err);
  assertStructurallyValid(cfg);
}

void test_example_config_is_valid() {
  static char buf[16384];
  const char* candidates[] = {"data/config.example.json",
                              "../data/config.example.json",
                              "../../data/config.example.json"};
  size_t len = 0;
  TEST_ASSERT_TRUE_MESSAGE(
      readAny(candidates, sizeof(candidates) / sizeof(candidates[0]), buf,
              sizeof(buf), len),
      "data/config.example.json is missing");

  JsonDocument doc;
  const DeserializationError derr = deserializeJson(doc, buf, len);
  TEST_ASSERT_FALSE_MESSAGE(derr, derr.c_str());

  um::DeviceConfig cfg;
  char err[160] = {0};
  TEST_ASSERT_TRUE_MESSAGE(um::configFromJson(doc, cfg, err, sizeof(err)), err);
  assertStructurallyValid(cfg);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_local_seed_config_is_valid);
  RUN_TEST(test_example_config_is_valid);
  return UNITY_END();
}
