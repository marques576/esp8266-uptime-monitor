#include "scheduler.h"

namespace um {
namespace {

uint32_t intervalMs(uint32_t seconds) { return seconds * 1000u; }

}  // namespace

void staggerInitial(Monitor& m, uint8_t index, uint32_t nowMs) {
  m.state = MonitorState::Pending;
  m.consecutiveFailures = 0;
  m.downBeats = 0;
  m.lastCheckMs = 0;
  m.lastLatencyMs = 0;
  m.lastHttpStatus = 0;
  m.lastError[0] = '\0';
  // 400 ms base + 250 ms per index keeps a 12-monitor fleet under 3.5 s.
  m.nextDueMs = nowMs + 400u + (static_cast<uint32_t>(index) * 250u);
}

bool isDue(const Monitor& m, uint32_t nowMs) {
  if (!m.enabled) return false;
  return timeReached(nowMs, m.nextDueMs);
}

bool applyCheckResult(Monitor& m, uint32_t nowMs, const CheckOutcome& r,
                      bool notifyOnRecovery, Notification& ev) {
  const bool healthy = m.upsideDown ? !r.ok : r.ok;

  m.lastCheckMs = nowMs;
  m.lastLatencyMs = r.latencyMs;
  m.lastHttpStatus = r.httpStatus;
  if (healthy) {
    m.lastError[0] = '\0';
  } else {
    copyStr(m.lastError, sizeof(m.lastError), r.error != nullptr ? r.error : "check failed");
  }

  if (healthy) {
    const bool recovered = (m.state == MonitorState::Down);
    m.state = MonitorState::Up;
    m.consecutiveFailures = 0;
    m.downBeats = 0;
    m.nextDueMs = nowMs + intervalMs(m.intervalSec);
    if (recovered && notifyOnRecovery && m.notify) {
      ev.monitorId = m.id;
      ev.kind = EventKind::Up;
      ev.atMs = nowMs;
      ev.failures = 0;
      return true;
    }
    return false;
  }

  if (m.consecutiveFailures < 255) m.consecutiveFailures++;

  if (m.state == MonitorState::Down) {
    // Already alerted: keep watching at the normal heartbeat cadence and
    // optionally repeat the notification every `resendEvery` failed beats.
    if (m.downBeats < 0xFFFFFFFFu) m.downBeats++;
    m.nextDueMs = nowMs + intervalMs(m.intervalSec);
    if (m.notify && m.resendEvery > 0 && (m.downBeats % m.resendEvery) == 0) {
      ev.monitorId = m.id;
      ev.kind = EventKind::Resend;
      ev.atMs = nowMs;
      ev.failures = m.consecutiveFailures;
      return true;
    }
    return false;
  }

  // State was Up or Pending: this failure is either a retry or the one that
  // crosses the threshold and turns the monitor DOWN.
  if (m.consecutiveFailures > m.retries) {
    m.state = MonitorState::Down;
    m.downBeats = 1;
    m.nextDueMs = nowMs + intervalMs(m.intervalSec);
    if (m.notify) {
      ev.monitorId = m.id;
      ev.kind = EventKind::Down;
      ev.atMs = nowMs;
      ev.failures = m.consecutiveFailures;
      return true;
    }
    return false;
  }

  m.state = MonitorState::Pending;
  // A zero retry interval would spin hot; validation forbids it, but clamp here
  // as well because schedule code is also reachable from tests/other callers.
  const uint32_t retrySec = m.retryIntervalSec == 0 ? 1 : m.retryIntervalSec;
  m.nextDueMs = nowMs + intervalMs(retrySec);
  return false;
}

bool NotificationQueue::push(const Notification& n) {
  if (count_ >= kNotificationQueueCap) return false;
  buf_[tail_] = n;
  tail_ = static_cast<uint8_t>((tail_ + 1) % kNotificationQueueCap);
  count_++;
  return true;
}

bool NotificationQueue::pop(Notification& n) {
  if (count_ == 0) return false;
  n = buf_[head_];
  head_ = static_cast<uint8_t>((head_ + 1) % kNotificationQueueCap);
  count_--;
  return true;
}

}  // namespace um
