#include <unity.h>

#include <string.h>

#include "scheduler.h"

using namespace um;

void setUp() {}
void tearDown() {}

namespace {

Monitor makeMonitor(uint32_t interval = 60, uint8_t retries = 3,
                    uint32_t retryInterval = 60) {
  Monitor m;
  m.id = 1;
  m.type = MonitorType::TcpPort;
  copyStr(m.name, sizeof(m.name), "app");
  copyStr(m.host, sizeof(m.host), "192.0.2.10");
  m.port = 22;
  m.intervalSec = interval;
  m.retries = retries;
  m.retryIntervalSec = retryInterval;
  return m;
}

CheckOutcome okOutcome(uint16_t latency = 12) {
  CheckOutcome r;
  r.ok = true;
  r.latencyMs = latency;
  return r;
}

CheckOutcome failOutcome(const char* msg = "connect timeout") {
  CheckOutcome r;
  r.ok = false;
  r.error = msg;
  return r;
}

void test_initial_stagger_is_ordered_and_not_immediate() {
  Monitor a = makeMonitor();
  Monitor b = makeMonitor();
  staggerInitial(a, 0, 1000);
  staggerInitial(b, 1, 1000);
  TEST_ASSERT_EQUAL_UINT32(1400, a.nextDueMs);
  TEST_ASSERT_EQUAL_UINT32(1650, b.nextDueMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Pending),
                        static_cast<int>(a.state));
  TEST_ASSERT_FALSE(isDue(a, 1000));
  TEST_ASSERT_TRUE(isDue(a, 1400));
}

void test_disabled_monitor_is_never_due() {
  Monitor m = makeMonitor();
  staggerInitial(m, 0, 0);
  m.enabled = false;
  TEST_ASSERT_FALSE(isDue(m, 100000));
}

void test_up_transition_uses_interval() {
  Monitor m = makeMonitor(60, 3, 60);
  staggerInitial(m, 0, 0);
  Notification ev;
  const bool fired = applyCheckResult(m, 2000, okOutcome(42), true, ev);
  TEST_ASSERT_FALSE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Up),
                        static_cast<int>(m.state));
  TEST_ASSERT_EQUAL_UINT8(0, m.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT16(42, m.lastLatencyMs);
  TEST_ASSERT_EQUAL_UINT32(2000 + 60000, m.nextDueMs);
  TEST_ASSERT_EQUAL_STRING("", m.lastError);
}

void test_retries_then_down() {
  Monitor m = makeMonitor(60, 3, 10);
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);  // Up baseline

  uint32_t t = 1000;
  for (uint8_t i = 1; i <= 3; ++i) {
    const bool fired = applyCheckResult(m, t, failOutcome(), true, ev);
    TEST_ASSERT_FALSE(fired);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Pending),
                          static_cast<int>(m.state));
    TEST_ASSERT_EQUAL_UINT8(i, m.consecutiveFailures);
    TEST_ASSERT_EQUAL_UINT32(t + 10000, m.nextDueMs);
    t += 10000;
  }

  // Failure number retries+1 crosses the threshold.
  const bool fired = applyCheckResult(m, t, failOutcome(), true, ev);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Down),
                        static_cast<int>(m.state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Down),
                        static_cast<int>(ev.kind));
  TEST_ASSERT_EQUAL_UINT8(4, ev.failures);
  TEST_ASSERT_EQUAL_UINT8(4, m.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1, m.downBeats);
  TEST_ASSERT_EQUAL_UINT32(t + 60000, m.nextDueMs);
  TEST_ASSERT_EQUAL_STRING("connect timeout", m.lastError);
}

void test_zero_retries_goes_down_on_first_failure() {
  Monitor m = makeMonitor(60, 0, 60);
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);
  const bool fired = applyCheckResult(m, 500, failOutcome(), true, ev);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Down),
                        static_cast<int>(m.state));
}

void test_recovery_emits_up_event() {
  Monitor m = makeMonitor(60, 0, 60);
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);
  applyCheckResult(m, 100, failOutcome(), true, ev);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Down),
                        static_cast<int>(m.state));

  const bool fired = applyCheckResult(m, 200, okOutcome(), true, ev);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Up),
                        static_cast<int>(ev.kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Up),
                        static_cast<int>(m.state));
  TEST_ASSERT_EQUAL_UINT8(0, m.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(200 + 60000, m.nextDueMs);
}

void test_recovery_silent_when_recovery_notifications_disabled() {
  Monitor m = makeMonitor(60, 0, 60);
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);
  applyCheckResult(m, 100, failOutcome(), true, ev);
  const bool fired = applyCheckResult(m, 200, okOutcome(), false, ev);
  TEST_ASSERT_FALSE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Up),
                        static_cast<int>(m.state));
}

void test_resend_every_n_down_beats() {
  Monitor m = makeMonitor(60, 0, 60);
  m.resendEvery = 2;
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);

  bool fired = applyCheckResult(m, 100, failOutcome(), true, ev);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Down),
                        static_cast<int>(ev.kind));

  fired = applyCheckResult(m, 200, failOutcome(), true, ev);  // downBeats=2
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Resend),
                        static_cast<int>(ev.kind));

  fired = applyCheckResult(m, 300, failOutcome(), true, ev);  // downBeats=3
  TEST_ASSERT_FALSE(fired);

  fired = applyCheckResult(m, 400, failOutcome(), true, ev);  // downBeats=4
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Resend),
                        static_cast<int>(ev.kind));
}

void test_resend_disabled_never_emits_again() {
  Monitor m = makeMonitor(60, 0, 60);
  m.resendEvery = 0;
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);
  applyCheckResult(m, 100, failOutcome(), true, ev);
  for (uint32_t i = 1; i <= 20; ++i) {
    TEST_ASSERT_FALSE(applyCheckResult(m, 100 + i * 100, failOutcome(), true, ev));
  }
}

void test_upside_down_inverts_reachability() {
  Monitor m = makeMonitor(60, 0, 60);
  m.upsideDown = true;
  staggerInitial(m, 0, 0);
  Notification ev;

  // Reachable but upside down => counted as a failure, and it goes DOWN.
  const bool fired = applyCheckResult(m, 100, okOutcome(), true, ev);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Down),
                        static_cast<int>(m.state));

  // Unreachable => healthy => UP.
  const bool recovered = applyCheckResult(m, 200, failOutcome(), true, ev);
  TEST_ASSERT_TRUE(recovered);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Up),
                        static_cast<int>(m.state));
}

void test_notify_off_suppresses_events_but_still_marks_down() {
  Monitor m = makeMonitor(60, 0, 60);
  m.notify = false;
  staggerInitial(m, 0, 0);
  Notification ev;
  applyCheckResult(m, 0, okOutcome(), true, ev);
  const bool fired = applyCheckResult(m, 100, failOutcome(), true, ev);
  TEST_ASSERT_FALSE(fired);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MonitorState::Down),
                        static_cast<int>(m.state));
}

void test_millis_wraparound() {
  TEST_ASSERT_FALSE(timeReached(0xFFFFFF00u, 0x00000100u));
  TEST_ASSERT_TRUE(timeReached(0x00000101u, 0x00000100u));
  TEST_ASSERT_TRUE(timeReached(0x00000200u, 0xFFFFFF00u));
  TEST_ASSERT_TRUE(timeReached(0x00000000u, 0xFFFFFF00u));
}

void test_notification_queue_fifo_and_overflow() {
  NotificationQueue q;
  TEST_ASSERT_TRUE(q.empty());

  for (uint8_t i = 1; i <= kNotificationQueueCap; ++i) {
    Notification n;
    n.monitorId = i;
    n.kind = EventKind::Down;
    TEST_ASSERT_TRUE(q.push(n));
  }
  TEST_ASSERT_EQUAL_UINT8(kNotificationQueueCap, q.size());

  Notification overflow;
  overflow.monitorId = 99;
  TEST_ASSERT_FALSE(q.push(overflow));

  for (uint8_t i = 1; i <= kNotificationQueueCap; ++i) {
    Notification n;
    TEST_ASSERT_TRUE(q.pop(n));
    TEST_ASSERT_EQUAL_UINT8(i, n.monitorId);
  }
  TEST_ASSERT_TRUE(q.empty());
  Notification n;
  TEST_ASSERT_FALSE(q.pop(n));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_stagger_is_ordered_and_not_immediate);
  RUN_TEST(test_disabled_monitor_is_never_due);
  RUN_TEST(test_up_transition_uses_interval);
  RUN_TEST(test_retries_then_down);
  RUN_TEST(test_zero_retries_goes_down_on_first_failure);
  RUN_TEST(test_recovery_emits_up_event);
  RUN_TEST(test_recovery_silent_when_recovery_notifications_disabled);
  RUN_TEST(test_resend_every_n_down_beats);
  RUN_TEST(test_resend_disabled_never_emits_again);
  RUN_TEST(test_upside_down_inverts_reachability);
  RUN_TEST(test_notify_off_suppresses_events_but_still_marks_down);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_notification_queue_fifo_and_overflow);
  return UNITY_END();
}
