#include "alarmo_events.h"

#include <cstring>
#include <initializer_list>

namespace {
constexpr const char *topics[] = {"alarmo/perimeter/event", "alarmo/interior/event", "alarmo/garage/event"};

// A bounded JSON reader for Alarmo's small event objects. Only top-level event
// and command strings are extracted; nested sensor metadata is skipped.
class Reader {
 public:
  Reader(const char *data, size_t length) : p(data), end(data + length) {}
  void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
  bool take(char c) { ws(); if (p == end || *p != c) return false; ++p; return true; }
  bool done() { ws(); return p == end; }
  bool string(char *out = nullptr, size_t capacity = 0) {
    if (!take('"')) return false;
    size_t size = 0;
    while (p < end) {
      unsigned char c = *p++;
      if (c == '"') {
        if (out) out[size < capacity ? size : capacity - 1] = '\0';
        return !out || size < capacity;
      }
      if (c < 32) return false;
      if (c == '\\') {
        if (p == end) return false;
        c = *p++;
        if (c == 'u') {
          unsigned value = 0;
          for (int i = 0; i < 4; ++i) {
            if (p == end) return false;
            char h = *p++;
            unsigned digit = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 16;
            if (digit == 16) return false;
            value = value * 16 + digit;
          }
          // Non-ASCII metadata is valid but cannot name an event or JSON key.
          c = value > 0 && value < 128 ? value : 127;
        } else if (c == 'b') c = '\b';
        else if (c == 'f') c = '\f';
        else if (c == 'n') c = '\n';
        else if (c == 'r') c = '\r';
        else if (c == 't') c = '\t';
        else if (c != '"' && c != '\\' && c != '/') return false;
      }
      if (out && size + 1 < capacity) out[size] = char(c);
      ++size;
    }
    return false;
  }
  bool skip(unsigned depth = 0) {
    ws();
    if (p == end || depth > 8) return false;
    if (*p == '"') return string();
    if (*p == '{' || *p == '[') {
      bool object = *p++ == '{';
      char close = object ? '}' : ']';
      if (take(close)) return true;
      do {
        if (object && (!string() || !take(':'))) return false;
        if (!skip(depth + 1)) return false;
        if (take(close)) return true;
      } while (take(','));
      return false;
    }
    for (const char *literal : {"true", "false", "null"}) {
      size_t length = std::strlen(literal);
      if (size_t(end - p) >= length && std::memcmp(p, literal, length) == 0) { p += length; return true; }
    }
    if (*p == '-') ++p;
    if (p == end || *p < '0' || *p > '9') return false;
    if (*p++ != '0') while (p < end && *p >= '0' && *p <= '9') ++p;
    if (p < end && *p == '.') { ++p; if (!digits()) return false; }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p; if (p < end && (*p == '+' || *p == '-')) ++p;
      if (!digits()) return false;
    }
    return true;
  }
 private:
  bool digits() {
    const char *start = p;
    while (p < end && *p >= '0' && *p <= '9') ++p;
    return p != start;
  }
  const char *p;
  const char *end;
};
}

const char *alarmoEventTopic(AlarmTarget target) {
  return unsigned(target) < 3 ? topics[unsigned(target)] : nullptr;
}

bool alarmoEventTarget(const char *topic, AlarmTarget &target) {
  if (!topic) return false;
  for (unsigned i = 0; i < 3; ++i) if (std::strcmp(topic, topics[i]) == 0) {
    target = static_cast<AlarmTarget>(i); return true;
  }
  return false;
}

bool parseAlarmoEvent(const char *payload, size_t length, AlarmoEvent &event) {
  event = AlarmoEvent{};
  if (!payload || length == 0 || length > 1024) return false;
  Reader reader(payload, length);
  char name[40] = "";
  bool found = false, commandFound = false;
  if (!reader.take('{')) return false;
  if (reader.take('}')) return false;
  do {
    char key[64];
    if (!reader.string(key, sizeof(key)) || !reader.take(':')) return false;
    if (std::strcmp(key, "event") == 0) {
      if (found || !reader.string(name, sizeof(name))) return false;
      found = true;
    } else if (std::strcmp(key, "command") == 0) {
      if (commandFound || !reader.string(event.command, sizeof(event.command))) return false;
      commandFound = true;
    } else if (!reader.skip()) return false;
    if (reader.take('}')) {
      if (!found || !reader.done()) return false;
      if (std::strcmp(name, "INVALID_CODE_PROVIDED") == 0) event.failure = AlarmoFailure::InvalidCode;
      else if (std::strcmp(name, "NO_CODE_PROVIDED") == 0) event.failure = AlarmoFailure::MissingCode;
      else if (std::strcmp(name, "COMMAND_NOT_ALLOWED") == 0) event.failure = AlarmoFailure::NotAllowed;
      else if (std::strcmp(name, "FAILED_TO_ARM") == 0) event.failure = AlarmoFailure::OpenSensors;
      return true;
    }
  } while (reader.take(','));
  return false;
}
