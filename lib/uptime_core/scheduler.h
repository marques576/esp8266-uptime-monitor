// Scheduling and the Uptime Kuma retry state machine.
//
// Semantics (identical to Kuma's monitor form):
//   interval        cadence while the monitor is healthy or already down
//   retries         consecutive failures tolerated before the monitor is DOWN
//   retryInterval   cadence used while retries are in flight (state Pending)
//   resendEvery     while DOWN, re-send the notification every N failed beats
//   upsideDown      a reachable target is reported as DOWN
//
// A monitor is marked DOWN on failure number (retries + 1): the initial failed
// beat plus `retries` retries.
#pragma once

#include "monitor.h"

namespace um {

constexpr uint8_t kNotificationQueueCap = 8;

// Wraparound-safe "now >= due" for millis() / uint32 timestamps.
inline bool timeReached(uint32_t now, uint32_t due) {
  return static_cast<int32_t>(now - due) >= 0;
}

// First check is nearly immediate but staggered so N monitors don't all fire
// on the same loop iteration; the rest follow at their configured interval.
void staggerInitial(Monitor& m, uint8_t index, uint32_t nowMs);

bool isDue(const Monitor& m, uint32_t nowMs);

// Applies a probe result. Returns true and fills `ev` when a webhook-triggering
// event happened (initial DOWN, recovery, or a scheduled resend).
bool applyCheckResult(Monitor& m, uint32_t nowMs, const CheckOutcome& r,
                      bool notifyOnRecovery, Notification& ev);

class NotificationQueue {
 public:
  bool push(const Notification& n);
  bool pop(Notification& n);
  uint8_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  void clear() { head_ = tail_ = count_ = 0; }

 private:
  Notification buf_[kNotificationQueueCap];
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint8_t count_ = 0;
};

}  // namespace um
