#pragma once

#include <cstddef>
#include "panel_types.h"

enum class AlarmoFailure { None, InvalidCode, MissingCode, NotAllowed, OpenSensors };
struct AlarmoEvent {
  AlarmoFailure failure = AlarmoFailure::None;
  char command[32] = "";
};

// Alarmo's default event topics for this project's area slugs. No master events:
// all commands sent by this panel are addressed to an individual area.
const char *alarmoEventTopic(AlarmTarget target);
bool alarmoEventTarget(const char *topic, AlarmTarget &target);
bool parseAlarmoEvent(const char *payload, size_t length, AlarmoEvent &event);
