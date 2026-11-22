#include "workflow.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
using S = AlarmLifecycleState;
std::array<S, 4> states;
bool online;
int failTarget;
struct Command { AlarmTarget target; AlarmAction action; ArmMode mode; std::string pin; };
std::vector<Command> commands;
S state(AlarmTarget t) { return states[unsigned(t)]; }
bool connected() { return online; }
bool publish(AlarmTarget t, AlarmAction a, ArmMode m, const char *pin) {
  if (int(t) == failTarget) return false;
  commands.push_back({t, a, m, pin}); return true;
}
void check(bool condition, const char *description) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
PanelWorkflow fresh() {
  states = {S::ArmedNight, S::ArmedNight, S::ArmedNight, S::Closed};
  commands.clear(); online = true; failTarget = -1;
  return PanelWorkflow({state, connected, publish});
}
}
int main() {
  {
    auto w = fresh(); check(w.start(QuickAction::DisarmHouse, "1234", 0), "start house");
    check(commands.size() == 2, "house sends two commands");
    check(commands[0].target == AlarmTarget::Perimeter && commands[1].target == AlarmTarget::Interior, "house excludes garage");
    check(commands[0].pin == "1234" && commands[1].pin == "1234", "same PIN for both areas");
    states[0] = S::Disarmed; w.tick(1);
    check(w.busy() && w.confirmed() == 1, "partial confirmation stays pending");
    states[1] = S::Disarmed; w.tick(2);
    check(w.phase() == WorkflowPhase::Complete && states[2] == S::ArmedNight, "house confirmed, garage untouched");
  }
  for (auto action : {QuickAction::ArmAway, QuickAction::GoodNight}) {
    auto w = fresh(); states[0] = states[1] = states[2] = S::Disarmed;
    w.start(action, "", 0); check(commands.size() == 3, "group arm includes three areas");
    ArmMode mode = action == QuickAction::ArmAway ? ArmMode::Away : ArmMode::Night;
    for (auto &c : commands) check(c.mode == mode && c.action == AlarmAction::Arm, "correct arm mode");
    states[0] = states[1] = states[2] = action == QuickAction::ArmAway ? S::ArmedAway : S::ArmedNight;
    w.tick(1); check(w.phase() == WorkflowPhase::Complete, "group arm confirmed");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "1234", 10);
    check(commands.size() == 1 && commands[0].target == AlarmTarget::Garage && commands[0].action == AlarmAction::Disarm, "garage disarms first");
    w.tick(1000); check(commands.size() == 1, "no open before confirmation");
    states[2] = S::Disarmed; w.tick(1100);
    check(commands.size() == 2 && commands[1].target == AlarmTarget::GarageDoor, "confirmed disarm opens door");
    w.tick(1200); check(commands.size() == 2, "never repeat TOGGLE");
    states[3] = S::Open; w.tick(1300); check(w.phase() == WorkflowPhase::Complete, "door open confirmed");
  }
  {
    auto w = fresh(); states[2] = S::Disarmed;
    w.start(QuickAction::OpenGarage, "", 0);
    check(commands.size() == 1 && commands[0].target == AlarmTarget::GarageDoor, "no PIN when garage already disarmed");
    w.tick(20000); check(w.phase() == WorkflowPhase::Failed && commands.size() == 1, "door timeout never retries");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "bad", 0); w.tick(90000);
    check(w.phase() == WorkflowPhase::Failed && commands.size() == 1, "unconfirmed PIN never opens door");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "1234", 0); w.cancel(); states[2] = S::Disarmed; w.tick(10);
    check(w.phase() == WorkflowPhase::Cancelled && commands.size() == 1, "cancel prevents later open");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "1234", 0); online = false; states[2] = S::Disarmed; w.tick(10);
    check(w.phase() == WorkflowPhase::Failed && commands.size() == 1, "disconnect prevents open");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "1234", 0); states[3] = S::Closing; states[2] = S::Disarmed; w.tick(10);
    check(w.phase() == WorkflowPhase::Failed && commands.size() == 1, "changed door state prevents TOGGLE");
  }
  for (auto door : {S::Unknown, S::Opening, S::Closing}) {
    auto w = fresh(); states[3] = door; w.start(QuickAction::OpenGarage, "1234", 0);
    check(w.phase() == WorkflowPhase::Failed && commands.empty(), "moving/unknown door blocked");
  }
  {
    auto w = fresh(); states[3] = S::Open; w.start(QuickAction::OpenGarage, "1234", 0);
    check(w.phase() == WorkflowPhase::Complete && commands.empty(), "already open does not toggle");
  }
  {
    auto w = fresh(); states[1] = S::Unknown; w.start(QuickAction::DisarmHouse, "1234", 0);
    check(commands.empty() && w.phase() == WorkflowPhase::Failed, "validate all states before publishing");
  }
  {
    auto w = fresh(); online = false; w.start(QuickAction::DisarmHouse, "1234", 0);
    check(commands.empty() && w.phase() == WorkflowPhase::Failed, "offline blocks sends");
  }
  {
    auto w = fresh(); failTarget = 1; w.start(QuickAction::DisarmHouse, "1234", 0);
    check(commands.size() == 1 && w.phase() == WorkflowPhase::Failed, "publish failure reports partial dispatch");
  }
  {
    auto w = fresh(); w.start(QuickAction::DisarmHouse, "1234", 0); states[0] = S::Disarmed; w.tick(90000);
    check(w.phase() == WorkflowPhase::Failed && w.confirmed() == 1, "partial timeout preserves confirmed count");
  }
  {
    auto w = fresh(); w.start(QuickAction::Individual, "1234", 0, AlarmTarget::GarageDoor, AlarmAction::Arm);
    check(commands.size() == 1 && commands[0].action == AlarmAction::Disarm, "individual door uses safe disarm flow");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "1234", UINT32_MAX - 1000); w.tick(90000);
    check(w.phase() == WorkflowPhase::Failed, "timeout survives clock rollover");
  }

  {
    AlarmoEvent event;
    auto parse = [&](const char *json) { return parseAlarmoEvent(json, std::string(json).size(), event); };
    check(parse("{\"event\": \"INVALID_CODE_PROVIDED\"}") && event.failure == AlarmoFailure::InvalidCode, "decode invalid PIN");
    check(parse("{ \"event\" : \"NO_CODE_PROVIDED\" }") && event.failure == AlarmoFailure::MissingCode, "decode missing PIN");
    check(parse("{\"sensors\":[{\"name\":\"Door \\\"A\\\"\",\"meta\":[true,null,1.5e2]}],\"event\":\"FAILED_TO_ARM\"}") && event.failure == AlarmoFailure::OpenSensors, "skip nested sensor metadata");
    check(parse("{\"event\":\"COMMAND_NOT_ALLOWED\",\"command\":\"ARM_AWAY\",\"state\":\"pending\"}") && event.failure == AlarmoFailure::NotAllowed && std::string(event.command) == "ARM_AWAY", "decode rejected command");
    check(parse("{\"event\":\"ARM_NIGHT\",\"delay\":30}") && event.failure == AlarmoFailure::None, "non-failure events do not fail actions");
    for (const char *json : {"{\"event\":\"INVALID_CODE_PROVIDED\"", "{\"event\":\"INVALID_CODE_PROVIDED\",}",
             "{\"event\":\"INVALID_CODE_PROVIDED\"} trailing", "{\"event\":123}",
             "{\"metadata\":{\"event\":\"INVALID_CODE_PROVIDED\"}}",
             "{\"event\":\"ARM_NIGHT\",\"event\":\"INVALID_CODE_PROVIDED\"}"})
      check(!parse(json), "reject malformed, nested-only, or duplicate event payloads");
    const char data[] = "{\"event\":\"INVALID_CODE_PROVIDED\"}";
    check(!parseAlarmoEvent(data, sizeof(data) - 2, event), "bounded parser rejects truncation");
    AlarmTarget target;
    check(alarmoEventTarget("alarmo/interior/event", target) && target == AlarmTarget::Interior, "area topic mapping");
    check(!alarmoEventTarget("alarmo/event", target) && !alarmoEventTarget("alarmo/garage/state", target), "master and state topics do not route failure");
  }
  {
    auto w = fresh(); w.start(QuickAction::OpenGarage, "bad", 0);
    check(w.handleFailure(AlarmTarget::Garage, {AlarmoFailure::InvalidCode, ""}), "accept pending garage failure");
    check(w.phase() == WorkflowPhase::Failed && w.canRetryPin(), "immediate PIN result is retryable");
    states[2] = S::Disarmed; w.tick(100);
    check(commands.size() == 1, "late disarm after rejection never opens door");
  }
  {
    auto w = fresh(); w.start(QuickAction::DisarmHouse, "bad", 0);
    check(!w.handleFailure(AlarmTarget::Garage, {AlarmoFailure::InvalidCode, ""}), "ignore unrelated area");
    states[0] = S::Disarmed;
    check(!w.handleFailure(AlarmTarget::Perimeter, {AlarmoFailure::InvalidCode, ""}), "ignore already confirmed area");
    check(w.handleFailure(AlarmTarget::Interior, {AlarmoFailure::MissingCode, ""}) && w.confirmed() == 1, "failure preserves partial success");
    commands.clear(); w.start(QuickAction::DisarmHouse, "5678", 10);
    check(commands.size() == 1 && commands[0].target == AlarmTarget::Interior && commands[0].pin == "5678", "retry only unresolved area with fresh PIN");
    states[1] = S::Disarmed; w.tick(20); check(w.phase() == WorkflowPhase::Complete, "fresh retry confirms");
  }
  {
    auto w = fresh(); states[0] = states[1] = states[2] = S::Disarmed; w.start(QuickAction::ArmAway, "", 0);
    check(!w.handleFailure(AlarmTarget::Interior, {AlarmoFailure::NotAllowed, "ARM_NIGHT"}), "ignore mismatched command");
    check(w.handleFailure(AlarmTarget::Interior, {AlarmoFailure::OpenSensors, ""}) && !w.canRetryPin(), "open sensors fail immediately without PIN retry");
  }
  {
    auto w = fresh(); w.start(QuickAction::DisarmHouse, "1234", 0); w.cancel();
    check(!w.handleFailure(AlarmTarget::Interior, {AlarmoFailure::InvalidCode, ""}), "cancelled actions ignore events");
  }
  {
    auto w = fresh(); states[2] = S::Disarmed; w.start(QuickAction::OpenGarage, "", 0);
    check(!w.handleFailure(AlarmTarget::Garage, {AlarmoFailure::InvalidCode, ""}), "door movement ignores alarm rejection");
  }
  std::puts("PASS: grouped modes, PIN, partial result, garage sequencing, cancellation, offline, unknown, timeout, rollover");
}
