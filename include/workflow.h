#pragma once

#include "panel_types.h"
#include "alarmo_events.h"

enum class QuickAction { DisarmHouse, OpenGarage, ArmAway, GoodNight, Individual };
enum class WorkflowPhase { Idle, WaitingAlarms, WaitingDoor, Complete, Failed, Cancelled };

struct WorkflowBackend {
  AlarmLifecycleState (*state)(AlarmTarget);
  bool (*connected)();
  bool (*publish)(AlarmTarget, AlarmAction, ArmMode, const char *);
};

// No networking or graphics dependencies: both firmware and desktop use this engine.
class PanelWorkflow {
 public:
  explicit PanelWorkflow(WorkflowBackend backend) : backend_(backend) {}
  bool start(QuickAction action, const char *pin, uint32_t now,
             AlarmTarget target = AlarmTarget::Perimeter,
             AlarmAction individualAction = AlarmAction::Disarm,
             ArmMode mode = ArmMode::Home);
  void tick(uint32_t now);
  void cancel();
  bool handleFailure(AlarmTarget target, const AlarmoEvent &event);
  bool canRetryPin() const { return phase_ == WorkflowPhase::Failed && retryPin_; }
  bool busy() const;
  WorkflowPhase phase() const { return phase_; }
  const char *message() const { return message_; }
  uint8_t confirmed() const { return confirmed_; }
  uint8_t requested() const { return requested_; }

 private:
  void fail(const char *message);
  void openDoor(uint32_t now);
  bool matches(AlarmLifecycleState state) const;
  WorkflowBackend backend_;
  QuickAction action_ = QuickAction::Individual;
  WorkflowPhase phase_ = WorkflowPhase::Idle;
  AlarmAction alarmAction_ = AlarmAction::Disarm;
  ArmMode mode_ = ArmMode::Home;
  uint32_t started_ = 0;
  uint8_t activeMask_ = 0;
  uint8_t sentMask_ = 0;
  bool retryPin_ = false;
  uint8_t requested_ = 0;
  uint8_t confirmed_ = 0;
  char message_[96] = "";
};
