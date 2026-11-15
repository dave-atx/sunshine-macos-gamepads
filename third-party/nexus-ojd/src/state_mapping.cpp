#include "nexus/ojd/state_mapping.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace nexus::ojd {
namespace {

struct ButtonPair {
  std::uint32_t moonlight_bit;
  Button ojd_button;
};

/// Moonlight bit -> OJD control. Face buttons are positional: on an Xbox pad
/// Moonlight's A is the South button.
///
/// D-pad directions are absent on purpose: OJD takes them in a separate `dpad`
/// field and ignores them inside `buttons`.
constexpr std::array<ButtonPair, 11> kButtonMap{{
    {moonlight::a, Button::south},
    {moonlight::b, Button::east},
    {moonlight::x, Button::west},
    {moonlight::y, Button::north},
    {moonlight::left_button, Button::left_shoulder},
    {moonlight::right_button, Button::right_shoulder},
    {moonlight::back, Button::back},
    {moonlight::start, Button::start},
    {moonlight::home, Button::guide},
    {moonlight::left_stick, Button::left_stick_click},
    {moonlight::right_stick, Button::right_stick_click},
}};

/// Moonlight D-pad bit -> OJD D-pad control, emitted in the dedicated field.
constexpr std::array<ButtonPair, 4> kDpadMap{{
    {moonlight::dpad_up, Button::dpad_up},
    {moonlight::dpad_down, Button::dpad_down},
    {moonlight::dpad_left, Button::dpad_left},
    {moonlight::dpad_right, Button::dpad_right},
}};

/// Controls that Sunshine can report but that have no dedicated OJD button in
/// this profile. They are accepted rather than dropped silently so a caller can
/// see the intent: touchpad and misc map onto the closest OJD control.
constexpr std::array<ButtonPair, 2> kApproximateButtonMap{{
    {moonlight::touchpad_button, Button::touchpad_click},
    {moonlight::misc_button, Button::capture},
}};

/// Collect the OJD buttons whose Moonlight bit is set in `flags`.
std::vector<Button> buttons_for(std::uint32_t flags) {
  std::vector<Button> buttons;
  for (const auto &pair : kButtonMap) {
    if ((flags & pair.moonlight_bit) != 0) {
      buttons.push_back(pair.ojd_button);
    }
  }
  for (const auto &pair : kApproximateButtonMap) {
    if ((flags & pair.moonlight_bit) != 0) {
      buttons.push_back(pair.ojd_button);
    }
  }
  return buttons;
}

/// Collect the OJD D-pad directions whose Moonlight bit is set.
std::vector<Button> dpad_for(std::uint32_t flags) {
  std::vector<Button> dpad;
  for (const auto &pair : kDpadMap) {
    if ((flags & pair.moonlight_bit) != 0) {
      dpad.push_back(pair.ojd_button);
    }
  }
  return dpad;
}

double trigger_to_unit(std::uint8_t value, const MapOptions &options) {
  if (!options.triggers_are_bytes) {
    return static_cast<double>(value);
  }
  return static_cast<double>(value) / 255.0;
}

bool nearly_equal(double left, double right) noexcept {
  // Stick quantisation is 1/32768, so this tolerance only ignores true no-ops.
  return std::fabs(left - right) < 0.0005;
}

/// OJD's stick Y is inverted relative to Moonlight's: Moonlight says negative
/// for up, OJD produces HID Y = positive for up. Negate so "stick up" stays up.
double y_axis(std::int16_t value) noexcept {
  return -detail::stick_to_unit(value);
}

}  // namespace

namespace detail {

double stick_to_unit(std::int16_t value) noexcept {
  // Moonlight sends negative Y for stick-up (SDL convention). OJD's
  // LeftStickY/RightStickY are the opposite: measured on a real endpoint,
  // sending -1.0 produced HID Y = +32766, which a HID consumer reads as DOWN.
  // The Y negation lives in full_frame()/diff_frame() so this stays a pure
  // scale of the raw value.
  if (value < 0) {
    return static_cast<double>(value) / 32768.0;
  }
  return static_cast<double>(value) / 32767.0;
}

}  // namespace detail

Frame full_frame(const GamepadState &state, const MapOptions &options) {
  Frame frame;
  frame.buttons = buttons_for(state.button_flags);
  frame.dpad = dpad_for(state.button_flags);
  frame.axes = {
      {Axis::left_stick_x, detail::stick_to_unit(state.lx)},
      {Axis::left_stick_y, y_axis(state.ly)},
      {Axis::right_stick_x, detail::stick_to_unit(state.rx)},
      {Axis::right_stick_y, y_axis(state.ry)},
      {Axis::left_trigger, trigger_to_unit(state.lt, options)},
      {Axis::right_trigger, trigger_to_unit(state.rt, options)},
  };
  return frame;
}

Frame diff_frame(const GamepadState &previous, const GamepadState &current,
                 const MapOptions &options) {
  Frame frame;

  // OJD frames are absolute: omitted controls are neutral, not "unchanged". So
  // a diff must still list held buttons, or they would drop on this frame.
  frame.buttons = buttons_for(current.button_flags);
  frame.dpad = dpad_for(current.button_flags);

  const auto push_axis = [&frame, &options](Axis axis, double before, double after) {
    if (options.full_frames || !nearly_equal(before, after)) {
      frame.axes.push_back({axis, after});
    }
  };

  push_axis(Axis::left_stick_x, detail::stick_to_unit(previous.lx),
            detail::stick_to_unit(current.lx));
  push_axis(Axis::left_stick_y, y_axis(previous.ly), y_axis(current.ly));
  push_axis(Axis::right_stick_x, detail::stick_to_unit(previous.rx),
            detail::stick_to_unit(current.rx));
  push_axis(Axis::right_stick_y, y_axis(previous.ry), y_axis(current.ry));
  push_axis(Axis::left_trigger, trigger_to_unit(previous.lt, options),
            trigger_to_unit(current.lt, options));
  push_axis(Axis::right_trigger, trigger_to_unit(previous.rt, options),
            trigger_to_unit(current.rt, options));

  return frame;
}

}  // namespace nexus::ojd
