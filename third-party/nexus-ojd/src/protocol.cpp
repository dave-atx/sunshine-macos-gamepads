#include "nexus/ojd/protocol.hpp"

#include "nexus/ojd/reply.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nexus::ojd {
namespace {

void append_key(std::string &out, std::string_view key) {
  out.push_back('"');
  out.append(key);
  out.append("\":");
}

void append_api_and_kind(std::string &out, std::string_view kind) {
  append_key(out, "apiVersion");
  detail::append_json_string(out, kApiVersion);
  out.push_back(',');
  append_key(out, "kind");
  detail::append_json_string(out, kind);
}

}  // namespace

std::string_view to_string(Persona persona) noexcept {
  switch (persona) {
    case Persona::generic:
      return "Generic";
    case Persona::sdl_compatible:
      return "SDLCompatible";
  }
  return "Generic";
}

std::string_view to_string(Button button) noexcept {
  switch (button) {
    case Button::south:
      return "South";
    case Button::east:
      return "East";
    case Button::west:
      return "West";
    case Button::north:
      return "North";
    case Button::left_shoulder:
      return "LeftShoulder";
    case Button::right_shoulder:
      return "RightShoulder";
    case Button::back:
      return "Back";
    case Button::start:
      return "Start";
    case Button::left_stick_click:
      return "LeftStickClick";
    case Button::right_stick_click:
      return "RightStickClick";
    case Button::dpad_up:
      return "DpadUp";
    case Button::dpad_down:
      return "DpadDown";
    case Button::dpad_left:
      return "DpadLeft";
    case Button::dpad_right:
      return "DpadRight";
    case Button::guide:
      return "Guide";
    case Button::capture:
      return "Capture";
    case Button::microphone:
      return "Microphone";
    case Button::touchpad_click:
      return "TouchpadClick";
  }
  return "South";
}

std::string_view to_string(Axis axis) noexcept {
  switch (axis) {
    case Axis::left_stick_x:
      return "LeftStickX";
    case Axis::left_stick_y:
      return "LeftStickY";
    case Axis::right_stick_x:
      return "RightStickX";
    case Axis::right_stick_y:
      return "RightStickY";
    case Axis::left_trigger:
      return "LeftTrigger";
    case Axis::right_trigger:
      return "RightTrigger";
  }
  return "LeftStickX";
}

namespace detail {

std::string render_number(double value) {
  // Reject non-finite input rather than emitting invalid JSON.
  if (!std::isfinite(value)) {
    return "0";
  }

  // Six decimals is finer than any 8-bit HID axis can express, and avoids
  // producing a long tail of digits for a value like -0.7500000001.
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);

  std::string text{buffer};

  // Trim trailing zeros, then a trailing decimal point.
  const auto dot = text.find('.');
  if (dot != std::string::npos) {
    std::size_t last = text.find_last_not_of('0');
    if (last == dot) {
      --last;
    }
    text.erase(last + 1);
  }

  // "-0" is valid JSON but noisy; normalise it.
  if (text == "-0") {
    text = "0";
  }
  return text;
}

void append_json_string(std::string &out, std::string_view text) {
  out.push_back('"');
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    switch (character) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (byte < 0x20) {
          char escape[7];
          std::snprintf(escape, sizeof(escape), "\\u%04x", byte);
          out.append(escape);
        } else {
          out.push_back(character);
        }
        break;
    }
  }
  out.push_back('"');
}

}  // namespace detail

std::string build_token_proof(std::string_view token, std::string_view nonce) {
  // OJD: join four lines, each ending with a newline, then HMAC-SHA256 keyed
  // with the SHA-256 of the token. The token itself is never transmitted.
  std::string message;
  message.append("OpenJoystickDriver endpoint hello 1\n");
  message.append(nonce);
  message.append("\n");
  message.append("\n");  // origin: empty on the socket transport
  message.append("\n");  // WebSocket port: empty on the socket transport

  const auto key = sha256(token);
  const auto digest = hmac_sha256(
      key.data(), key.size(),
      reinterpret_cast<const std::uint8_t *>(message.data()), message.size());
  return to_hex(digest);
}

std::string build_hello_token(std::string_view token_name, std::string_view token,
                              std::string_view nonce) {
  std::string out;
  out.reserve(256);
  out.push_back('{');
  append_api_and_kind(out, "Hello");
  out.push_back(',');
  append_key(out, "scopes");
  out.append("[\"Control\"]");
  out.push_back(',');
  append_key(out, "tokenName");
  detail::append_json_string(out, token_name);
  out.push_back(',');
  append_key(out, "proof");
  detail::append_json_string(out, build_token_proof(token, nonce));
  out.push_back('}');
  return out;
}

std::string build_hello_signature_grant() {
  std::string out;
  out.reserve(96);
  out.push_back('{');
  append_api_and_kind(out, "Hello");
  out.push_back(',');
  append_key(out, "scopes");
  out.append("[\"Control\"]");
  out.push_back('}');
  return out;
}

std::string build_feed_request(Persona persona) {
  std::string out;
  out.reserve(96);
  out.push_back('{');
  append_api_and_kind(out, "FeedRequest");
  out.push_back(',');
  append_key(out, "as");
  detail::append_json_string(out, to_string(persona));
  out.push_back('}');
  return out;
}

std::string build_frame(const Frame &frame) {
  std::string out;
  out.reserve(160);
  out.push_back('{');
  append_api_and_kind(out, "Frame");

  if (!frame.buttons.empty()) {
    out.push_back(',');
    append_key(out, "buttons");
    out.push_back('[');
    for (std::size_t index = 0; index < frame.buttons.size(); ++index) {
      if (index != 0) {
        out.push_back(',');
      }
      detail::append_json_string(out, to_string(frame.buttons[index]));
    }
    out.push_back(']');
  }

  // D-pad directions travel in their own field. Sending them in `buttons` is
  // silently ignored by OJD, which is why the d-pad did nothing.
  if (!frame.dpad.empty()) {
    out.push_back(',');
    append_key(out, "dpad");
    out.push_back('[');
    for (std::size_t index = 0; index < frame.dpad.size(); ++index) {
      if (index != 0) {
        out.push_back(',');
      }
      detail::append_json_string(out, to_string(frame.dpad[index]));
    }
    out.push_back(']');
  }

  if (!frame.axes.empty()) {
    out.push_back(',');
    append_key(out, "axes");
    out.push_back('{');
    for (std::size_t index = 0; index < frame.axes.size(); ++index) {
      if (index != 0) {
        out.push_back(',');
      }
      append_key(out, to_string(frame.axes[index].axis));
      out.append(detail::render_number(frame.axes[index].value));
    }
    out.push_back('}');
  }

  if (frame.hold_milliseconds >= 0) {
    out.push_back(',');
    append_key(out, "holdMilliseconds");
    out.append(std::to_string(frame.hold_milliseconds));
  }

  out.push_back('}');
  return out;
}

std::string build_keepalive() {
  std::string out;
  out.reserve(96);
  out.push_back('{');
  append_api_and_kind(out, "KeepAlive");
  out.push_back('}');
  return out;
}

std::string build_drain() {
  std::string out;
  out.reserve(96);
  out.push_back('{');
  append_api_and_kind(out, "Drain");
  out.push_back('}');
  return out;
}

std::optional<RumbleCommand> parse_rumble_command(const Reply &reply) {
  if (reply.kind != "RumbleCommand") {
    return std::nullopt;
  }
  // "StopRumble" carries no payload; treat it as an explicit zero.
  RumbleCommand command;
  command.set = reply.rumble_type != "StopRumble";
  if (reply.has_intensities) {
    // Clamp rather than reject: an out-of-range value from a future OJD build
    // should still produce sane rumble instead of dropping the command.
    const auto clamp = [](std::int64_t value) -> std::uint16_t {
      if (value <= 0) return 0;
      if (value >= 65535) return 65535;
      return static_cast<std::uint16_t>(value);
    };
    command.low_frequency = clamp(reply.low_frequency);
    command.high_frequency = clamp(reply.high_frequency);
    command.left_trigger = clamp(reply.left_trigger);
    command.right_trigger = clamp(reply.right_trigger);
  }
  if (reply.duration_milliseconds >= 0) {
    command.duration_milliseconds = static_cast<std::int32_t>(
        std::min<std::int64_t>(reply.duration_milliseconds, 60'000));
  }
  return command;
}

}  // namespace nexus::ojd
