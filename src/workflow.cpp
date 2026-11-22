#include "workflow.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr AlarmTarget areas[] = {AlarmTarget::Perimeter, AlarmTarget::Interior, AlarmTarget::Garage};
constexpr uint32_t alarmTimeout = 90000; // Allow HA's configured exit delay to finish.
constexpr uint32_t doorTimeout = 20000;
}

bool PanelWorkflow::busy() const {
  return phase_ == WorkflowPhase::WaitingAlarms || phase_ == WorkflowPhase::WaitingDoor;
}

void PanelWorkflow::fail(const char *message) {
  phase_ = WorkflowPhase::Failed;
  std::snprintf(message_, sizeof(message_), "%s\nConfirmed: %u/%u", message,
                unsigned(confirmed_), unsigned(requested_));
}

bool PanelWorkflow::matches(AlarmLifecycleState state) const {
  if (alarmAction_ == AlarmAction::Disarm) return state == AlarmLifecycleState::Disarmed;
  switch (mode_) {
    case ArmMode::Home: return state == AlarmLifecycleState::ArmedHome;
    case ArmMode::Away: return state == AlarmLifecycleState::ArmedAway;
    case ArmMode::Night: return state == AlarmLifecycleState::ArmedNight;
    case ArmMode::Vacation: return state == AlarmLifecycleState::ArmedVacation;
  }
  return false;
}

bool PanelWorkflow::start(QuickAction action, const char *pin, uint32_t now,
                          AlarmTarget target, AlarmAction individualAction, ArmMode mode) {
  if (busy()) return false;
  action_ = action;
  retryPin_ = false;
  sentMask_ = activeMask_ = 0;
  confirmed_ = requested_ = 0;
  started_ = now;
  mode_ = action == QuickAction::GoodNight ? ArmMode::Night :
          action == QuickAction::ArmAway ? ArmMode::Away : mode;
  alarmAction_ = action == QuickAction::DisarmHouse || action == QuickAction::OpenGarage
                     ? AlarmAction::Disarm : action == QuickAction::Individual
                     ? individualAction : AlarmAction::Arm;
  if (!backend_.connected()) { fail("OFFLINE / no command sent"); return false; }

  if (action == QuickAction::OpenGarage) {
    const auto door = backend_.state(AlarmTarget::GarageDoor);
    if (door == AlarmLifecycleState::Open) {
      phase_ = WorkflowPhase::Complete;
      std::snprintf(message_, sizeof(message_), "Door already open");
      return true;
    }
    if (door != AlarmLifecycleState::Closed) { fail("Door moving or unknown"); return false; }
  }
  if (action == QuickAction::Individual && target == AlarmTarget::GarageDoor) {
    const auto door = backend_.state(target);
    if (individualAction == AlarmAction::Arm) { return start(QuickAction::OpenGarage, pin, now); }
    else {
      if (door != AlarmLifecycleState::Open) { fail("Door moving or unknown"); return false; }
      if (!backend_.publish(target, AlarmAction::Disarm, mode_, "")) {
        fail("Close command not sent"); return false;
      }
      phase_ = WorkflowPhase::WaitingDoor;
      std::snprintf(message_, sizeof(message_), "CLOSING DOOR...");
      return true;
    }
  }

  uint8_t mask = action_ == QuickAction::DisarmHouse ? 3 :
                 action_ == QuickAction::OpenGarage ? 4 :
                 action_ == QuickAction::Individual ? uint8_t(1 << unsigned(target)) : 7;
  // Validate every area before sending any command.
  for (unsigned i = 0; i < 3; ++i) {
    if (!(mask & (1 << i))) continue;
    if (backend_.state(areas[i]) == AlarmLifecycleState::Unknown) {
      fail("Area state unknown"); return false;
    }
    if (!matches(backend_.state(areas[i])) && alarmAction_ == AlarmAction::Disarm &&
        (pin == nullptr || pin[0] == '\0')) {
      fail("Enter PIN first"); return false;
    }
  }
  requested_ = 0;
  for (unsigned i = 0; i < 3; ++i) if (mask & (1 << i)) ++requested_;
  phase_ = WorkflowPhase::WaitingAlarms;
  // Keep the mask, not the PIN: credentials are never retained by the workflow.
  // Bits 0..2 identify requested areas, counts are computed afresh during tick.
  activeMask_ = mask;
  for (unsigned i = 0; i < 3; ++i) {
    if (!(mask & (1 << i))) continue;
    if (matches(backend_.state(areas[i]))) { ++confirmed_; continue; }
    sentMask_ |= (1 << i);
    if (!backend_.publish(areas[i], alarmAction_, mode_, pin ? pin : "")) {
      fail("Command not sent\nCheck area statuses"); return false;
    }
  }
  tick(now);
  return phase_ != WorkflowPhase::Failed;
}

void PanelWorkflow::openDoor(uint32_t now) {
  // Recheck at the moment of publishing. Never retry a TOGGLE command.
  const auto door = backend_.state(AlarmTarget::GarageDoor);
  if (door == AlarmLifecycleState::Open || door == AlarmLifecycleState::Opening) {
    phase_ = WorkflowPhase::Complete;
    std::snprintf(message_, sizeof(message_), "Door already open/opening");
    return;
  }
  if (door != AlarmLifecycleState::Closed ||
      backend_.state(AlarmTarget::Garage) != AlarmLifecycleState::Disarmed) {
    fail("Door state changed\nOpen cancelled"); return;
  }
  if (!backend_.publish(AlarmTarget::GarageDoor, AlarmAction::Arm, mode_, "")) {
    fail("Open command not sent"); return;
  }
  phase_ = WorkflowPhase::WaitingDoor;
  started_ = now;
  std::snprintf(message_, sizeof(message_), "OPENING DOOR...");
}

void PanelWorkflow::tick(uint32_t now) {
  if (!busy()) return;
  if (!backend_.connected()) { fail("Connection lost\nCheck area statuses"); return; }
  if (phase_ == WorkflowPhase::WaitingDoor) {
    const auto state = backend_.state(AlarmTarget::GarageDoor);
    const bool opening = action_ == QuickAction::OpenGarage;
    if (state == (opening ? AlarmLifecycleState::Open : AlarmLifecycleState::Closed)) {
      phase_ = WorkflowPhase::Complete;
      std::snprintf(message_, sizeof(message_), opening ? "GARAGE OPEN" : "GARAGE CLOSED");
    } else if (uint32_t(now - started_) >= doorTimeout) {
      fail("Door not confirmed\nCheck before retrying");
    }
    return;
  }
  confirmed_ = 0;
  for (unsigned i = 0; i < 3; ++i)
    if ((activeMask_ & (1 << i)) && matches(backend_.state(areas[i]))) ++confirmed_;
  if (confirmed_ == requested_) {
    if (action_ == QuickAction::OpenGarage) { openDoor(now); return; }
    phase_ = WorkflowPhase::Complete;
    std::snprintf(message_, sizeof(message_), "CONFIRMED / %u OF %u", unsigned(confirmed_), unsigned(requested_));
  } else if (uint32_t(now - started_) >= alarmTimeout) {
    fail("No confirmation\nRequest timed out");
  } else {
    std::snprintf(message_, sizeof(message_), "%s...\nConfirmed: %u/%u",
                  alarmAction_ == AlarmAction::Disarm ? "DISARMING" : "ARMING",
                  unsigned(confirmed_), unsigned(requested_));
  }
}

bool PanelWorkflow::handleFailure(AlarmTarget target, const AlarmoEvent &event) {
  if (phase_ != WorkflowPhase::WaitingAlarms || unsigned(target) >= 3 ||
      !(sentMask_ & (1 << unsigned(target))) || matches(backend_.state(target))) return false;
  const char *expected = alarmAction_ == AlarmAction::Disarm ? "DISARM" :
      mode_ == ArmMode::Away ? "ARM_AWAY" : mode_ == ArmMode::Night ? "ARM_NIGHT" :
      mode_ == ArmMode::Vacation ? "ARM_VACATION" : "ARM_HOME";
  if (event.command[0] && std::strcmp(event.command, expected) != 0) return false;
  if (event.failure == AlarmoFailure::None ||
      (event.failure == AlarmoFailure::OpenSensors && alarmAction_ != AlarmAction::Arm)) return false;
  confirmed_ = 0;
  for (unsigned i = 0; i < 3; ++i)
    if ((activeMask_ & (1 << i)) && matches(backend_.state(areas[i]))) ++confirmed_;
  switch (event.failure) {
    case AlarmoFailure::InvalidCode: fail("PIN NOT ACCEPTED"); break;
    case AlarmoFailure::MissingCode: fail("PIN REQUIRED"); break;
    case AlarmoFailure::NotAllowed: fail("COMMAND NOT ALLOWED"); break;
    case AlarmoFailure::OpenSensors: fail("OPEN SENSORS\nUnable to arm"); break;
    default: return false;
  }
  retryPin_ = alarmAction_ == AlarmAction::Disarm &&
      (event.failure == AlarmoFailure::InvalidCode || event.failure == AlarmoFailure::MissingCode);
  return true;
}

void PanelWorkflow::cancel() {
  if (!busy()) return;
  phase_ = WorkflowPhase::Cancelled;
  std::snprintf(message_, sizeof(message_), "CANCELLED\nSent commands may finish");
}
