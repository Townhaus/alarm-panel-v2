#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "app_state.h"
#include "pin_config.h"
#include "workflow.h"

ArmMode selectedArmMode = ArmMode::Home;

namespace {
constexpr int width = 240;
lv_disp_draw_buf_t drawBuffer;
lv_color_t pixels[width * 20];
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_obj_t *root, *modal, *modalTitle, *prompt, *pinLabel, *keys, *modes, *confirmButton;
lv_obj_t *retryButton, *garageShortcut, *garageCloseButton;
lv_obj_t *progress, *clockLabel, *weatherLabel, *connectionLabel;
lv_obj_t *stateLabels[4]{};
lv_obj_t *screensaver, *saverClock, *toast, *triggered, *triggerSource;
lv_timer_t *toastTimer = nullptr;
bool dismissedTrigger = false;
bool wasTriggered = false;
bool showingProgress = false;
bool enteringPin = false;
char pin[16] = "";
QuickAction selectedQuick = QuickAction::Individual;
AlarmTarget selectedTarget = AlarmTarget::Perimeter;
AlarmAction selectedAction = AlarmAction::Disarm;
uint32_t lastClock = 0;

AlarmLifecycleState stateFor(AlarmTarget target) {
  return parseAlarmLifecycleState(targetState(target));
}
bool publishFor(AlarmTarget target, AlarmAction action, ArmMode mode, const char *code) {
  selectedArmMode = mode;
  return executeAlarmAction(target, action, code);
}
PanelWorkflow workflow({stateFor, mqttReady, publishFor});

void base(lv_obj_t *obj, int x, int y, int w, int h) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(obj, &panel_mono_16, 0);
  lv_obj_set_style_text_color(obj, lv_color_white(), 0);
}
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h) {
  auto *obj = lv_obj_create(parent);
  base(obj, x, y, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}
lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int h = 20) {
  auto *obj = lv_label_create(parent);
  base(obj, x, y, w, h);
  lv_label_set_text(obj, text);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  return obj;
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                 lv_event_cb_t cb, intptr_t data = 0) {
  auto *obj = lv_btn_create(parent);
  base(obj, x, y, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_white(), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_bg_color(obj, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_STATE_PRESSED);
  lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
  auto *textObj = lv_label_create(obj);
  lv_obj_remove_style_all(textObj);
  lv_label_set_text(textObj, text);
  lv_obj_center(textObj);
  lv_obj_clear_flag(textObj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}
void visible(lv_obj_t *obj, bool show) {
  if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
const char *shortState(const char *state) {
  switch (parseAlarmLifecycleState(state)) {
    case AlarmLifecycleState::Disarmed: return "DISARMED";
    case AlarmLifecycleState::Arming: return "ARMING";
    case AlarmLifecycleState::ArmedHome: return "ARMED HOME";
    case AlarmLifecycleState::ArmedAway: return "ARMED AWAY";
    case AlarmLifecycleState::ArmedNight: return "ARMED NIGHT";
    case AlarmLifecycleState::ArmedVacation: return "VACATION";
    case AlarmLifecycleState::ArmedCustomBypass: return "BYPASS";
    case AlarmLifecycleState::Pending: return "ENTRY DELAY";
    case AlarmLifecycleState::Triggered: return "TRIGGERED";
    case AlarmLifecycleState::Open: return "OPEN";
    case AlarmLifecycleState::Closed: return "CLOSED";
    case AlarmLifecycleState::Opening: return "OPENING";
    case AlarmLifecycleState::Closing: return "CLOSING";
    default: return "UNKNOWN";
  }
}
void clearPin() { std::memset(pin, 0, sizeof(pin)); }
void updatePin() {
  char masked[32] = "_ _ _ _";
  if (pin[0]) {
    size_t n = std::strlen(pin);
    for (size_t i = 0; i < n; ++i) masked[i] = '*';
    masked[n] = '\0';
  }
  lv_label_set_text(pinLabel, masked);
}
void addDigit(char c) {
  if (!enteringPin || showingProgress || workflow.busy()) return;
  size_t n = std::strlen(pin);
  if (n < sizeof(pin) - 1) { pin[n] = c; pin[n + 1] = '\0'; updatePin(); }
}
void backspace() {
  if (!enteringPin || showingProgress) return;
  auto n = std::strlen(pin);
  if (n) pin[n - 1] = '\0';
  updatePin();
}
void renderProgress() {
  if (!showingProgress) return;
  char text[300];
  std::snprintf(text, sizeof(text), "%s\n\nPERIMETER %s\nINTERIOR  %s\nGARAGE    %s\nDOOR      %s", workflow.message(),
                shortState(perimeterAlarmState), shortState(interiorAlarmState),
                shortState(garageAlarmState), shortState(garageDoorState));
  lv_label_set_text(progress, text);
  visible(retryButton, workflow.canRetryPin());
  const bool openedGarage = selectedQuick == QuickAction::OpenGarage ||
      (selectedQuick == QuickAction::Individual && selectedTarget == AlarmTarget::GarageDoor && selectedAction == AlarmAction::Arm);
  visible(garageCloseButton, openedGarage && workflow.phase() == WorkflowPhase::Complete &&
                            stateFor(AlarmTarget::GarageDoor) == AlarmLifecycleState::Open);
  lv_label_set_text(prompt, workflow.busy() ? "WAIT FOR CONFIRMATION" :
                    workflow.phase() == WorkflowPhase::Complete ? "ACTION COMPLETE" : "CHECK RESULTS");
}
void submit() {
  if (showingProgress || workflow.busy()) return;
  if (enteringPin && !pin[0]) { showToast("ENTER PIN FIRST"); return; }
  workflow.start(selectedQuick, pin, millis(), selectedTarget, selectedAction, selectedArmMode);
  clearPin();
  enteringPin = false;
  showingProgress = true;
  visible(keys, false); visible(pinLabel, false); visible(modes, false); visible(confirmButton, false);
  visible(progress, true);
  renderProgress();
}
void onSubmit(lv_event_t *) { submit(); }
void onRetry(lv_event_t *) {
  if (!workflow.canRetryPin()) return;
  clearPin(); updatePin(); showingProgress = false; enteringPin = true;
  visible(progress, false); visible(retryButton, false); visible(keys, true); visible(pinLabel, true);
  lv_label_set_text(prompt, "ENTER PIN AGAIN"); resetIdleTimer();
}
void onBack(lv_event_t *) {
  clearPin(); enteringPin = false;
  if (workflow.busy()) { workflow.cancel(); renderProgress(); return; }
  visible(modal, false);
  showingProgress = false;
}
void onKey(lv_event_t *event) {
  intptr_t c = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  if (c == '<') backspace();
  else if (c == '>') submit();
  else addDigit(char(c));
}
void onMode(lv_event_t *event) {
  selectedArmMode = static_cast<ArmMode>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  submit();
}
void openModal(const char *title, const char *description, bool needsPin, bool chooseMode) {
  if (workflow.busy()) { showToast("ACTION IN PROGRESS"); return; }
  wakeDisplay(); resetIdleTimer(); clearPin();
  enteringPin = needsPin; showingProgress = false;
  lv_label_set_text(modalTitle, title);
  lv_label_set_text(prompt, description);
  updatePin();
  visible(pinLabel, needsPin); visible(keys, needsPin); visible(modes, chooseMode);
  visible(confirmButton, !needsPin && !chooseMode); visible(progress, false); visible(retryButton, false); visible(garageCloseButton, false); visible(modal, true);
  lv_obj_move_foreground(modal);
}
void onCloseGarage(lv_event_t *) {
  if (workflow.busy()) return;
  if (stateFor(AlarmTarget::GarageDoor) != AlarmLifecycleState::Open) {
    showToast("DOOR STATE CHANGED"); return;
  }
  selectedQuick = QuickAction::Individual;
  selectedTarget = AlarmTarget::GarageDoor;
  selectedAction = AlarmAction::Disarm;
  openModal("CLOSE / GARAGE", "CLOSE GARAGE DOOR?", false, false);
}
void openQuick(QuickAction quick) {
  if (workflow.busy()) { showToast("ACTION IN PROGRESS"); return; }
  selectedQuick = quick;
  switch (selectedQuick) {
    case QuickAction::DisarmHouse:
      openModal("DISARM / HOUSE", "PERIMETER + INTERIOR\nENTER PIN", true, false); break;
    case QuickAction::OpenGarage:
      if (stateFor(AlarmTarget::GarageDoor) == AlarmLifecycleState::Open) { onCloseGarage(nullptr); break; }
      if (stateFor(AlarmTarget::GarageDoor) != AlarmLifecycleState::Closed) {
        showToast("DOOR MOVING OR UNKNOWN"); break;
      }
      openModal("OPEN / GARAGE", isDisarmedState(garageAlarmState) ? "OPEN GARAGE DOOR?" :
                "DISARM + OPEN\nENTER PIN", !isDisarmedState(garageAlarmState), false);
      // When already disarmed, opening is the action the home-screen tap requested.
      if (isDisarmedState(garageAlarmState)) submit();
      break;
    case QuickAction::ArmAway:
      openModal("ARM / AWAY", "ALL THREE AREAS\nARM AWAY?", false, false); break;
    case QuickAction::GoodNight:
      openModal("ARM / NIGHT", "ALL THREE AREAS\nARM NIGHT?", false, false); break;
    default: break;
  }
}
void onQuick(lv_event_t *event) {
  openQuick(static_cast<QuickAction>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}
void openArea(AlarmTarget target) {
  if (workflow.busy()) { showToast("ACTION IN PROGRESS"); return; }
  selectedTarget = target;
  selectedQuick = QuickAction::Individual;
  auto state = stateFor(selectedTarget);
  if (state == AlarmLifecycleState::Unknown) { showToast("STATE UNKNOWN"); return; }
  selectedAction = nextActionForTargetState(selectedTarget, state);
  if (selectedTarget == AlarmTarget::GarageDoor && state != AlarmLifecycleState::Open && state != AlarmLifecycleState::Closed) {
    showToast("DOOR IS MOVING"); return;
  }
  const bool door = selectedTarget == AlarmTarget::GarageDoor;
  const bool needsPin = door ? (selectedAction == AlarmAction::Arm && !isDisarmedState(garageAlarmState)) : selectedAction == AlarmAction::Disarm;
  char title[32];
  std::snprintf(title, sizeof(title), "%s / %s", door ? "DOOR" : selectedAction == AlarmAction::Arm ? "ARM" : "DISARM", targetName(selectedTarget));
  openModal(title, door ? (needsPin ? "DISARM + OPEN\nENTER PIN" : selectedAction == AlarmAction::Arm ? "OPEN DOOR?" : "CLOSE DOOR?") :
            needsPin ? "ENTER PIN" : "CHOOSE ARM MODE", needsPin, !door && !needsPin);
}
void onArea(lv_event_t *event) {
  openArea(static_cast<AlarmTarget>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}
void dismissToast(lv_timer_t *) { visible(toast, false); toastTimer = nullptr; }
void dismissTrigger(lv_event_t *) { dismissedTrigger = true; visible(triggered, false); }
void setupUi() {
  root = box(lv_scr_act(), 0, 0, width, 320);
  label(root, "HOME /", 8, 9, 64);
  connectionLabel = label(root, "OFFLINE", 78, 9, 76);
  lv_obj_set_style_text_color(connectionLabel, lv_color_hex(0xAAAAAA), 0);
  clockLabel = label(root, "--:--", 182, 9, 50);
  auto *rule = box(root, 8, 33, 224, 1);
  lv_obj_set_style_bg_color(rule, lv_color_white(), 0);
  button(root, "DISARM HOUSE", 8, 43, 224, 42, onQuick, intptr_t(QuickAction::DisarmHouse));
  garageShortcut = button(root, "OPEN GARAGE", 8, 91, 224, 42, onQuick, intptr_t(QuickAction::OpenGarage));
  button(root, "ARM AWAY", 8, 139, 108, 42, onQuick, intptr_t(QuickAction::ArmAway));
  button(root, "GOOD NIGHT", 124, 139, 108, 42, onQuick, intptr_t(QuickAction::GoodNight));
  const char *names[] = {"PERIMETER", "INTERIOR", "GARAGE", "DOOR"};
  for (unsigned i = 0; i < 4; ++i) {
    auto *row = button(root, "", 8, 190 + int(i) * 30, 224, 30, onArea, i);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x444444), 0);
    auto *name = label(row, names[i], 0, 5, 90);
    lv_obj_set_style_text_color(name, lv_color_hex(0xAAAAAA), 0);
    stateLabels[i] = label(row, "UNKNOWN", 94, 5, 130);
    lv_obj_set_style_text_align(stateLabels[i], LV_TEXT_ALIGN_RIGHT, 0);
  }
  modal = box(root, 0, 0, width, 320);
  modalTitle = label(modal, "", 8, 4, 168, 40);
  button(modal, "BACK", 184, 4, 48, 36, onBack);
  prompt = label(modal, "", 8, 45, 224, 40);
  lv_obj_set_style_text_color(prompt, lv_color_hex(0xAAAAAA), 0);
  pinLabel = label(modal, "_ _ _ _", 8, 85, 224);
  lv_obj_set_style_text_align(pinLabel, LV_TEXT_ALIGN_CENTER, 0);
  keys = box(modal, 8, 112, 224, 192);
  const char *digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "OK"};
  const char chars[] = "123456789<0>";
  for (unsigned i = 0; i < 12; ++i)
    button(keys, digits[i], (i % 3) * 76, (i / 3) * 48, 72, 44, onKey, chars[i]);
  modes = box(modal, 8, 112, 224, 120);
  const char *modeNames[] = {"HOME", "AWAY", "NIGHT", "VACATION"};
  for (unsigned i = 0; i < 4; ++i)
    button(modes, modeNames[i], (i % 2) * 116, (i / 2) * 56, 108, 48, onMode, i);
  confirmButton = button(modal, "CONFIRM", 8, 120, 224, 48, onSubmit);
  progress = label(modal, "", 8, 94, 224, 222);
  retryButton = button(modal, "RETRY PIN", 8, 268, 224, 44, onRetry);
  visible(retryButton, false);
  garageCloseButton = button(modal, "CLOSE GARAGE", 8, 268, 224, 44, onCloseGarage);
  visible(garageCloseButton, false);
  visible(modal, false);
  screensaver = box(root, 0, 0, width, 320);
  label(screensaver, "HOME / IDLE", 8, 12, 224);
  saverClock = label(screensaver, "--:--", 40, 125, 160, 60);
  lv_obj_set_style_text_font(saverClock, &panel_mono_40, 0);
  weatherLabel = label(screensaver, "", 8, 210, 224, 36);
  lv_obj_set_style_text_align(weatherLabel, LV_TEXT_ALIGN_CENTER, 0);
  label(screensaver, "TOUCH TO WAKE", 8, 280, 224);
  visible(screensaver, false);
  triggered = box(root, 0, 0, width, 320);
  lv_obj_set_style_bg_color(triggered, lv_color_hex(0x9B2226), 0);
  label(triggered, "! ALARM TRIGGERED", 16, 85, 208);
  triggerSource = label(triggered, "", 16, 120, 208);
  button(triggered, "SHOW CONTROLS", 16, 220, 208, 48, dismissTrigger);
  visible(triggered, false);
  toast = box(root, 8, 258, 224, 54);
  lv_obj_set_style_border_width(toast, 1, 0);
  lv_obj_set_style_border_color(toast, lv_color_white(), 0);
  label(toast, "", 6, 6, 212, 42);
  visible(toast, false);
  updateAlarmLabels(); updateWeatherDisplay(); updateClockDisplay();
}
}

const char *targetName(AlarmTarget target) {
  switch (target) {
    case AlarmTarget::Perimeter: return "PERIMETER";
    case AlarmTarget::Interior: return "INTERIOR";
    case AlarmTarget::Garage: return "GARAGE";
    case AlarmTarget::GarageDoor: return "DOOR";
  }
  return "UNKNOWN";
}
const char *targetState(AlarmTarget target) {
  switch (target) {
    case AlarmTarget::Perimeter: return perimeterAlarmState;
    case AlarmTarget::Interior: return interiorAlarmState;
    case AlarmTarget::Garage: return garageAlarmState;
    case AlarmTarget::GarageDoor: return garageDoorState;
  }
  return "unknown";
}
const char *armModeCommand(ArmMode mode) {
  switch (mode) {
    case ArmMode::Home: return "arm_home";
    case ArmMode::Away: return "arm_away";
    case ArmMode::Night: return "arm_night";
    case ArmMode::Vacation: return "arm_vacation";
  }
  return "arm_home";
}
bool handleAlarmoMessage(const char *topic, const char *payload, size_t length) {
  AlarmTarget target;
  AlarmoEvent event;
  if (!alarmoEventTarget(topic, target) || !parseAlarmoEvent(payload, length, event) ||
      !workflow.handleFailure(target, event)) return false;
  clearPin(); enteringPin = false; renderProgress(); resetIdleTimer();
  return true;
}
void setStatusLabelText(const char *text, lv_color_t) {
  if (connectionLabel) lv_label_set_text(connectionLabel, std::strcmp(text, "connected") == 0 ? "ONLINE" : "OFFLINE");
}
void updateMqttStatusLabel(bool connected) { setStatusLabelText(connected ? "connected" : "disconnected", lv_color_white()); }
void updateAlarmLabels() {
  if (garageShortcut) {
    const auto door = stateFor(AlarmTarget::GarageDoor);
    const char *text = door == AlarmLifecycleState::Open ? "CLOSE GARAGE" :
        door == AlarmLifecycleState::Closed ? "OPEN GARAGE" :
        door == AlarmLifecycleState::Opening ? "OPENING GARAGE..." :
        door == AlarmLifecycleState::Closing ? "CLOSING GARAGE..." : "GARAGE / UNKNOWN";
    lv_label_set_text(lv_obj_get_child(garageShortcut, 0), text);
    if (door == AlarmLifecycleState::Open || door == AlarmLifecycleState::Closed)
      lv_obj_clear_state(garageShortcut, LV_STATE_DISABLED);
    else lv_obj_add_state(garageShortcut, LV_STATE_DISABLED);
  }
  for (unsigned i = 0; i < 4; ++i) {
    if (!stateLabels[i]) continue;
    const char *state = targetState(static_cast<AlarmTarget>(i));
    lv_label_set_text(stateLabels[i], shortState(state));
    lv_obj_set_style_text_color(stateLabels[i], std::strcmp(state, "triggered") == 0 ? lv_color_hex(0xFF5555) : lv_color_white(), 0);
  }
  renderProgress();
}
void updateClockDisplay() {
  if (!clockLabel || (lastClock != 0 && millis() - lastClock < 1000)) return;
  lastClock = millis();
  struct tm info {};
  char text[16] = "--:--";
  if (timeConfigured && getLocalTime(&info, 1)) std::strftime(text, sizeof(text), "%H:%M", &info);
  lv_label_set_text(clockLabel, text); lv_label_set_text(saverClock, text);
}
void updateWeatherDisplay() { if (weatherLabel) lv_label_set_text(weatherLabel, weatherText); }
void showToast(const char *text) {
  if (!toast) return;
  if (toastTimer) { lv_timer_del(toastTimer); toastTimer = nullptr; }
  lv_label_set_text(lv_obj_get_child(toast, 0), text);
  lv_obj_move_foreground(toast); visible(toast, true);
  toastTimer = lv_timer_create(dismissToast, 2200, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}
void showScreensaver() {
  // Keep a pending action visible and stop abandoned PIN entry being retained.
  if (workflow.busy() || wasTriggered) { resetIdleTimer(); return; }
  clearPin(); enteringPin = false; showingProgress = false;
  visible(modal, false); visible(screensaver, true); lv_obj_move_foreground(screensaver);
  setDisplayBrightness(BRIGHTNESS_DIM); displayState = DisplayState::Screensaver;
}
void hideScreensaver() { visible(screensaver, false); resetIdleTimer(); }
void checkTriggeredState() {
  const bool active = isTriggeredState(stateFor(AlarmTarget::Perimeter)) ||
                      isTriggeredState(stateFor(AlarmTarget::Interior)) || isTriggeredState(stateFor(AlarmTarget::Garage));
  if (!active) { dismissedTrigger = false; visible(triggered, false); }
  if (active && !dismissedTrigger) {
    // Interrupt a pending door-open when an alarm triggers elsewhere.
    workflow.cancel(); clearPin(); enteringPin = false;
    wakeDisplay();
    for (unsigned i = 0; i < 3; ++i) if (isTriggeredState(stateFor(static_cast<AlarmTarget>(i)))) {
      lv_label_set_text(triggerSource, targetName(static_cast<AlarmTarget>(i))); break;
    }
    visible(triggered, true); lv_obj_move_foreground(triggered);
  }
  wasTriggered = active;
}
void setupLvgl() {
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, width * 20);
  lv_disp_drv_init(&displayDriver);
#if defined(DESKTOP_SIM)
  displayDriver.hor_res = 480;
#else
  displayDriver.hor_res = width;
#endif
  displayDriver.ver_res = 320; displayDriver.flush_cb = displayFlush; displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);
  lv_indev_drv_init(&inputDriver); inputDriver.type = LV_INDEV_TYPE_POINTER; inputDriver.read_cb = touchRead;
  lv_indev_drv_register(&inputDriver);
  setupUi();
}
void loopLvgl() {
  feedWatchdog(); updateClockDisplay(); checkIdleTimeout();
  workflow.tick(millis()); renderProgress();
  uint32_t now = millis(); lv_tick_inc(now - lastLvglTickMs); lastLvglTickMs = now;
  lv_timer_handler(); delay(5);
}
#if defined(WOKWI_SIM) || defined(DESKTOP_SIM)
void handleSimInputChar(char c) {
  if (c >= '0' && c <= '9') addDigit(c);
  else if (c == '\r' || c == '\n') { if (modal && !lv_obj_has_flag(modal, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(modes, LV_OBJ_FLAG_HIDDEN)) submit(); }
  else if (c == '\b' || c == 127) backspace();
  else if (c == 27) onBack(nullptr);
#if defined(WOKWI_SIM)
  // Retain serial-only fallback controls for the existing Wokwi setup.
  else if (c == 's' || c == 'a') submit();
  else if (c == 'p') openArea(AlarmTarget::Perimeter);
  else if (c == 'i') openArea(AlarmTarget::Interior);
  else if (c == 'g') openArea(AlarmTarget::Garage);
  else if (c == 'd') openArea(AlarmTarget::GarageDoor);
  else if (c == 'u') openQuick(QuickAction::DisarmHouse);
  else if (c == 'o') openQuick(QuickAction::OpenGarage);
  else if (c == 'w') openQuick(QuickAction::ArmAway);
  else if (c == 'n') openQuick(QuickAction::GoodNight);
  else if ((c == 'h' || c == 'v') && !lv_obj_has_flag(modal, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(modes, LV_OBJ_FLAG_HIDDEN)) {
    selectedArmMode = c == 'h' ? ArmMode::Home : ArmMode::Vacation; submit();
  }
  else if (c == 'x') backspace();
  else if (c == 'c') onBack(nullptr);
#endif
}
#endif
