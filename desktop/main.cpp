#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include "app_state.h"
#include "alarmo_events.h"

bool timeConfigured = true;
char perimeterAlarmState[32] = "armed_night";
char interiorAlarmState[32] = "armed_night";
char garageAlarmState[32] = "armed_night";
char garageDoorState[32] = "closed";
char weatherText[32] = "54° CLOUDY";
uint8_t currentBrightness = BRIGHTNESS_FULL;
DisplayState displayState = DisplayState::Active;
uint32_t displayTimeoutSec = 0;
uint32_t lastTouchActivityMs = 0, displayWakeIgnoreTouchUntilMs = 0, lastLvglTickMs = 0;
namespace {
constexpr int W = 480, H = 320, scale = 2;
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
std::array<uint32_t, W * H> framebuffer;
bool online = true, rejectPin = false, delayAck = false, partial = false;
unsigned doorCommands = 0;
bool mouseDown = false, ignoreUntilRelease = false;
int mouseX = 0, mouseY = 0;
uint32_t advance = 0;
lv_obj_t *simStatus;
struct Pending { bool active = false; uint32_t at = 0; std::string state; bool rejected = false; };
Pending pending[4];
char *stateBuffer(unsigned target) {
  char *states[] = {perimeterAlarmState, interiorAlarmState, garageAlarmState, garageDoorState};
  return states[target];
}
void setState(unsigned target, const char *state) {
  std::snprintf(stateBuffer(target), 32, "%s", state);
  updateAlarmLabels(); checkTriggeredState();
}
void refreshStatus() {
  char text[200];
  std::snprintf(text, sizeof(text), "MQTT / %s\nREJECT / %s\nDELAY / %s\nPARTIAL / %s", online ? "ONLINE" : "OFFLINE",
                rejectPin ? "YES" : "NO", delayAck ? "YES" : "NO", partial ? "YES" : "NO");
  lv_label_set_text(simStatus, text); updateMqttStatusLabel(online);
}
void scenario(lv_event_t *event) {
  int id = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  switch (id) {
    case 0:
    case 1:
      for (auto &p : pending) p.active = false;
      for (unsigned i = 0; i < 3; ++i) setState(i, id == 0 ? "armed_night" : "disarmed");
      setState(3, "closed"); break;
    case 2: online = !online; break;
    case 3: rejectPin = !rejectPin; break;
    case 4: delayAck = !delayAck; break;
    case 5: partial = !partial; break;
    case 6: setState(0, "triggered"); break;
    case 7: for (unsigned i = 0; i < 4; ++i) { pending[i].active = false; setState(i, "unknown"); } break;
    case 8: setDisplayBrightness(BRIGHTNESS_OFF); break;
    case 9: advance += 91000; break;
  }
  refreshStatus();
}
void companion() {
  auto *side = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(side); lv_obj_set_pos(side, 248, 0); lv_obj_set_size(side, 232, 320);
  lv_obj_set_style_bg_color(side, lv_color_hex(0x171717), 0); lv_obj_set_style_bg_opa(side, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(side, &panel_mono_16, 0); lv_obj_set_style_text_color(side, lv_color_white(), 0);
  lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
  auto *title = lv_label_create(side); lv_label_set_text(title, "DESKTOP / TEST LAB"); lv_obj_set_pos(title, 8, 9);
  auto *hint = lv_label_create(side); lv_label_set_text(hint, "FAKE HA / PIN 1234"); lv_obj_set_pos(hint, 8, 32);
  const char *names[] = {"NIGHT", "DISARMED", "MQTT", "REJECT PIN", "DELAY ACK", "PARTIAL", "TRIGGER", "UNKNOWN", "SLEEP", "+90 SEC"};
  for (intptr_t i = 0; i < 10; ++i) {
    auto *b = lv_btn_create(side); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, 8 + (i % 2) * 112, 64 + (i / 2) * 34); lv_obj_set_size(b, 104, 28);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x303030), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x555555), LV_STATE_PRESSED);
    auto *l = lv_label_create(b); lv_label_set_text(l, names[i]); lv_obj_center(l);
    lv_obj_add_event_cb(b, scenario, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
  }
  simStatus = lv_label_create(side); lv_obj_set_pos(simStatus, 8, 240); refreshStatus();
}
void applyPending() {
  if (!online) return;
  for (unsigned i = 0; i < 4; ++i) if (pending[i].active && int32_t(millis() - pending[i].at) >= 0) {
    pending[i].active = false;
    if (pending[i].rejected) {
      const char payload[] = "{\"event\": \"INVALID_CODE_PROVIDED\"}";
      handleAlarmoMessage(alarmoEventTopic(static_cast<AlarmTarget>(i)), payload, sizeof(payload) - 1);
    } else setState(i, pending[i].state.c_str());
  }
}
void draw() {
  SDL_UpdateTexture(texture, nullptr, framebuffer.data(), W * sizeof(uint32_t));
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  if (currentBrightness != BRIGHTNESS_FULL) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, currentBrightness == 0 ? 255 : 150);
    SDL_Rect panel{0, 0, 240 * scale, 320 * scale}; SDL_RenderFillRect(renderer, &panel);
  }
  SDL_RenderPresent(renderer);
}
void pump(int iterations = 12) { for (int i = 0; i < iterations; ++i) { applyPending(); loopLvgl(); draw(); } }
void click(int x, int y) { mouseX = x; mouseY = y; mouseDown = true; pump(); mouseDown = false; pump(); }
void checkLabels(lv_obj_t *object) {
  if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(object, &lv_label_class)) {
    lv_point_t measured;
    lv_txt_get_size(&measured, lv_label_get_text(object), lv_obj_get_style_text_font(object, 0),
                   lv_obj_get_style_text_letter_space(object, 0), lv_obj_get_style_text_line_space(object, 0),
                   lv_obj_get_content_width(object), LV_TEXT_FLAG_NONE);
    if (measured.y > lv_obj_get_content_height(object)) {
      std::fprintf(stderr, "Clipped label: %s (%d > %d)\n", lv_label_get_text(object), measured.y, lv_obj_get_content_height(object));
      std::exit(1);
    }
  }
  for (unsigned i = 0; i < lv_obj_get_child_cnt(object); ++i) checkLabels(lv_obj_get_child(object, i));
}
void capture(const std::string &path) {
  lv_obj_update_layout(lv_scr_act());
  checkLabels(lv_scr_act());
  auto *surface = SDL_CreateRGBSurfaceWithFormatFrom(framebuffer.data(), W, H, 32, W * 4, SDL_PIXELFORMAT_ARGB8888);
  if (!surface || SDL_SaveBMP(surface, path.c_str()) != 0) { std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError()); std::exit(1); }
  SDL_FreeSurface(surface);
}
void require(bool ok, const char *message) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void enterPin() { for (char c : std::string("1234")) handleSimInputChar(c); handleSimInputChar('\r'); }
void smoke(const std::string &prefix) {
  pump();
  lv_font_glyph_dsc_t degree {};
  require(lv_font_get_glyph_dsc(&panel_mono_16, &degree, 0x00B0, 0) &&
          !degree.is_placeholder && degree.box_w > 0 && lv_font_get_glyph_bitmap(&panel_mono_16, 0x00B0),
          "screensaver font contains the actual degree glyph");
  capture(prefix + "-home.bmp");
  click(100, 65); capture(prefix + "-pin.bmp");
  // Exercise the real LVGL keypad: 1, 2, 3, 4, OK.
  click(40, 134); click(120, 134); click(200, 134); click(40, 182); click(200, 278);
  pump(180);
  require(std::strcmp(perimeterAlarmState, "disarmed") == 0 && std::strcmp(interiorAlarmState, "disarmed") == 0, "one PIN disarms house");
  require(std::strcmp(garageAlarmState, "armed_night") == 0, "house leaves garage armed");
  capture(prefix + "-house-result.bmp");
  click(205, 20); click(100, 112); enterPin(); pump(450);
  require(std::strcmp(garageAlarmState, "disarmed") == 0 && std::strcmp(garageDoorState, "open") == 0, "garage disarms then opens");
  capture(prefix + "-garage-result.bmp");
  click(110, 290); capture(prefix + "-garage-close-confirm.bmp");
  require(std::strcmp(garageDoorState, "open") == 0, "result screen close waits for confirmation");
  unsigned beforeClose = doorCommands;
  click(110, 140); pump(260);
  require(std::strcmp(garageDoorState, "closed") == 0 && doorCommands == beforeClose + 1,
          "result screen can close garage with exactly one command");
  require(std::strcmp(garageAlarmState, "disarmed") == 0, "closing leaves rearming to Home Assistant");
  click(205, 20); click(100, 112); pump(260); // Already disarmed: open without another PIN.
  require(std::strcmp(garageDoorState, "open") == 0, "disarmed garage opens directly");
  click(205, 20); capture(prefix + "-home-close-garage.bmp");
  click(100, 112); capture(prefix + "-home-close-confirm.bmp");
  require(std::strcmp(garageDoorState, "open") == 0, "home close waits for confirmation");
  beforeClose = doorCommands;
  click(110, 140); pump(260);
  require(std::strcmp(garageDoorState, "closed") == 0 && doorCommands == beforeClose + 1,
          "home shortcut changes to close garage and sends one close command");
  click(205, 20); click(65, 160); click(100, 140); pump(180);
  require(std::strcmp(perimeterAlarmState, "armed_away") == 0 && std::strcmp(interiorAlarmState, "armed_away") == 0 && std::strcmp(garageAlarmState, "armed_away") == 0, "Away arms all three");
  click(205, 20); click(175, 160); click(100, 140); pump(180);
  require(std::strcmp(perimeterAlarmState, "armed_night") == 0 && std::strcmp(interiorAlarmState, "armed_night") == 0 && std::strcmp(garageAlarmState, "armed_night") == 0, "Night arms all three");
  click(205, 20); click(415, 112); // Reject PIN toggle in lab.
  click(100, 65); enterPin();
  const char unrelated[] = "{\"event\":\"INVALID_CODE_PROVIDED\"}";
  require(!handleAlarmoMessage("alarmo/garage/event", unrelated, sizeof(unrelated) - 1), "unrelated event is ignored");
  pump(45);
  require(std::strcmp(perimeterAlarmState, "armed_night") == 0, "rejected PIN leaves alarm armed");
  capture(prefix + "-rejected.bmp");
  click(415, 112); // Clear reject toggle while result remains visible.
  click(110, 290); capture(prefix + "-retry-pin.bmp"); enterPin(); pump(180);
  require(std::strcmp(perimeterAlarmState, "disarmed") == 0 && std::strcmp(interiorAlarmState, "disarmed") == 0,
          "immediate rejection allows fresh PIN retry without returning home");
  click(205, 20); click(300, 78); // Night preset for individual-control checks.
  click(100, 205); capture(prefix + "-individual-pin.bmp"); enterPin(); pump(180);
  require(std::strcmp(perimeterAlarmState, "disarmed") == 0 && std::strcmp(interiorAlarmState, "armed_night") == 0, "individual disarm leaves other areas alone");
  click(205, 20); click(100, 205); capture(prefix + "-individual-modes.bmp");
  click(175, 190); pump(180);
  require(std::strcmp(perimeterAlarmState, "armed_vacation") == 0, "individual Vacation mode retained");
  click(205, 20); click(300, 214); // Sleep.
  require(displayState == DisplayState::Sleeping, "lab can sleep display");
  click(100, 65); require(displayState == DisplayState::Active, "first tap wakes display");
  click(100, 65); enterPin(); pump(180);
  require(std::strcmp(perimeterAlarmState, "disarmed") == 0, "next tap can open PIN entry");
  click(205, 20); showScreensaver(); pump(); capture(prefix + "-screensaver.bmp"); click(100, 65);
  std::puts("PASS: real UI clicks, keypad, house, garage, Away, Night, rejected PIN, individual controls, sleep/wake, label fit");
}
}
uint32_t millis() { return SDL_GetTicks() + advance; }
void delay(uint32_t ms) { SDL_Delay(ms); }
bool getLocalTime(struct tm *info, uint32_t) { auto now = std::time(nullptr); return localtime_r(&now, info) != nullptr; }
void feedWatchdog() {}
void saveSettings() {}
bool mqttReady() { return online; }
bool connectMqtt() { return online; }
void setDisplayBrightness(uint8_t brightness) { currentBrightness = brightness; if (!brightness) displayState = DisplayState::Sleeping; }
void resetIdleTimer() { lastTouchActivityMs = millis(); }
void wakeDisplay() {
  if (displayState == DisplayState::Active) return;
  hideScreensaver(); currentBrightness = BRIGHTNESS_FULL; displayState = DisplayState::Active;
  ignoreUntilRelease = true; resetIdleTimer();
}
void checkIdleTimeout() {
  if (displayState == DisplayState::Active && displayTimeoutSec && millis() - lastTouchActivityMs >= displayTimeoutSec * 1000) showScreensaver();
}
void displayFlush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
  for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x) {
    auto rgb = lv_color_to32(*colors++); framebuffer[y * W + x] = 0xFF000000 | (rgb & 0xFFFFFF);
  }
  lv_disp_flush_ready(driver);
}
void touchRead(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->point.x = mouseX; data->point.y = mouseY;
  if (!mouseDown) ignoreUntilRelease = false;
  if (mouseDown && mouseX < 240 && displayState != DisplayState::Active) { wakeDisplay(); ignoreUntilRelease = true; }
  data->state = mouseDown && !ignoreUntilRelease ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (mouseDown && !ignoreUntilRelease) resetIdleTimer();
}
bool executeAlarmAction(AlarmTarget target, AlarmAction action, const char *code) {
  if (!online) return false;
  unsigned i = unsigned(target);
  if (i == 3) {
    if (action == AlarmAction::Arm && !isDisarmedState(garageAlarmState)) return false;
    ++doorCommands;
    setState(i, action == AlarmAction::Arm ? "opening" : "closing");
    pending[i] = {true, millis() + 1200, action == AlarmAction::Arm ? "open" : "closed"};
  } else {
    if (action == AlarmAction::Disarm && (rejectPin || std::strcmp(code, "1234") != 0)) {
      pending[i] = {true, millis() + 150, "", true};
      return true;
    }
    if (partial && i == 1) return true;
    std::string next = action == AlarmAction::Disarm ? "disarmed" : selectedArmMode == ArmMode::Away ? "armed_away" :
        selectedArmMode == ArmMode::Night ? "armed_night" : selectedArmMode == ArmMode::Vacation ? "armed_vacation" : "armed_home";
    pending[i] = {true, millis() + (delayAck ? 15000u : 500u), next};
  }
  return true;
}
int main(int argc, char **argv) {
  bool testing = argc >= 2 && std::strcmp(argv[1], "--smoke") == 0;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  window = SDL_CreateWindow("HOME / CONTROL - Desktop simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * scale, H * scale, testing ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
  if (!window || !renderer || !texture) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  setupLvgl(); companion();
  if (testing) { smoke(argc >= 3 ? argv[2] : "/tmp/alarm-panel"); }
  else {
    bool running = true; SDL_StartTextInput();
    while (running) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = false;
        if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
          int x, y; SDL_GetMouseState(&x, &y); mouseX = x / scale; mouseY = y / scale;
          if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) mouseDown = true;
          if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) mouseDown = false;
        }
        if (e.type == SDL_TEXTINPUT && displayState == DisplayState::Active) for (char *c = e.text.text; *c; ++c) handleSimInputChar(*c);
        if (e.type == SDL_KEYDOWN) {
          if (e.key.keysym.sym == SDLK_RETURN) handleSimInputChar('\r');
          if (e.key.keysym.sym == SDLK_BACKSPACE) handleSimInputChar('\b');
          if (e.key.keysym.sym == SDLK_ESCAPE) handleSimInputChar(27);
        }
      }
      applyPending(); loopLvgl(); draw();
    }
  }
  SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
}
