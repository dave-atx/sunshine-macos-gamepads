// Map Sunshine/Moonlight gamepad state onto OJD frames.
//
// Sunshine's platf::gamepad_state_t uses the Moonlight button mask. OJD uses
// named controls. The mapping is by position (South/East/West/North), which is
// what Xbox A/B/X/Y means; a DualSense label would swap East and South, so this
// layer is deliberately explicit rather than arithmetic.
//
// Kept free of Sunshine headers so it can be unit tested, and pinned to the real
// platf:: constants by static_asserts in the Sunshine adapter.
#pragma once

#include <cstdint>

#include "nexus/ojd/protocol.hpp"

namespace nexus::ojd {

/// Moonlight button mask bits, mirroring Sunshine's src/platform/common.h.
namespace moonlight {
inline constexpr std::uint32_t dpad_up = 0x0001;
inline constexpr std::uint32_t dpad_down = 0x0002;
inline constexpr std::uint32_t dpad_left = 0x0004;
inline constexpr std::uint32_t dpad_right = 0x0008;
inline constexpr std::uint32_t start = 0x0010;
inline constexpr std::uint32_t back = 0x0020;
inline constexpr std::uint32_t left_stick = 0x0040;
inline constexpr std::uint32_t right_stick = 0x0080;
inline constexpr std::uint32_t left_button = 0x0100;
inline constexpr std::uint32_t right_button = 0x0200;
inline constexpr std::uint32_t home = 0x0400;
inline constexpr std::uint32_t a = 0x1000;
inline constexpr std::uint32_t b = 0x2000;
inline constexpr std::uint32_t x = 0x4000;
inline constexpr std::uint32_t y = 0x8000;
inline constexpr std::uint32_t paddle1 = 0x010000;
inline constexpr std::uint32_t paddle2 = 0x020000;
inline constexpr std::uint32_t paddle3 = 0x040000;
inline constexpr std::uint32_t paddle4 = 0x080000;
inline constexpr std::uint32_t touchpad_button = 0x100000;
inline constexpr std::uint32_t misc_button = 0x200000;
}  // namespace moonlight

/// Sunshine-shaped gamepad state, in Moonlight's conventions.
struct GamepadState {
  std::uint32_t button_flags{};
  std::uint8_t lt{};
  std::uint8_t rt{};
  /// Sticks are -32768..32767 with positive Y down, which matches OJD already.
  std::int16_t lx{};
  std::int16_t ly{};
  std::int16_t rx{};
  std::int16_t ry{};

  friend constexpr bool operator==(const GamepadState &, const GamepadState &) = default;
};

struct MapOptions {
  /// Convert triggers as 0..255 -> 0..1. Moonlight triggers are already 0..255.
  bool triggers_are_bytes = true;
  /// Emit every axis on every frame (OJD treats omitted controls as neutral, so
  /// this only matters if a caller wants full-frame semantics).
  bool full_frames = false;
};

namespace detail {
/// Map an int16 stick reading onto OJD's -1..1 range.
[[nodiscard]] double stick_to_unit(std::int16_t value) noexcept;
}  // namespace detail

/// Build a frame containing only the controls that differ from `previous`.
/// Passing a default-constructed previous yields the full non-neutral state.
[[nodiscard]] Frame diff_frame(const GamepadState &previous, const GamepadState &current,
                              const MapOptions &options = {});

/// Build a frame containing every control, for the first frame after connect.
[[nodiscard]] Frame full_frame(const GamepadState &state, const MapOptions &options = {});

}  // namespace nexus::ojd
