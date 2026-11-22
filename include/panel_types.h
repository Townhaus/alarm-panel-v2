#pragma once

#include <cstdint>

// --- Enums ---

enum class DisplayState {
  Active,
  Screensaver,
  Sleeping,
};

enum class AlarmTarget {
  Perimeter,
  Interior,
  Garage,
  GarageDoor,
};

enum class AlarmAction {
  None,
  Arm,
  Disarm,
};

enum class AlarmLifecycleState {
  Unknown,
  Disarmed,
  Arming,
  ArmedHome,
  ArmedAway,
  ArmedNight,
  ArmedVacation,
  ArmedCustomBypass,
  Pending,
  Triggered,
  Open,
  Closed,
  Opening,
  Closing,
};

enum class ArmMode {
  Home,
  Away,
  Night,
  Vacation,
};
