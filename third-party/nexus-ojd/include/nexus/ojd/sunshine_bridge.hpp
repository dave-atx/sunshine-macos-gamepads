// Sunshine bridge: route Moonlight gamepad input to OJD virtual pads.
//
// Sunshine's macOS backend has no working gamepad path because the HID
// entitlement is Apple-restricted. OJD holds that entitlement, so this adapter
// forwards Sunshine's gamepad state to OJD's local endpoint instead.
//
// This file is the only part of the component that includes Sunshine headers.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "nexus/ojd/pad_session.hpp"

namespace nexus::ojd::sunshine {

/// OJD documents a limit of 4 concurrent virtual gamepads, so this is also the
/// maximum number of slots the bridge will create.
inline constexpr int kMaxVirtualPads = 4;

/// Why the bridge is unavailable, for logging once at session start.
enum class Availability {
  ready,             ///< A socket path and token are configured.
  no_socket,         ///< Endpoint socket path is empty or missing.
  no_token,          ///< Endpoint token is not configured.
};

[[nodiscard]] std::string_view to_string(Availability availability) noexcept;

/// Owns the pad pool for one Sunshine stream.
///
/// Gamepad updates arrive on Sunshine's input task thread; this class only
/// stores the newest state per slot and lets the session threads publish at a
/// bounded rate, so it never blocks Sunshine's input path on a socket write.
class GamepadBridge {
 public:
  GamepadBridge();
  ~GamepadBridge();

  GamepadBridge(const GamepadBridge &) = delete;
  GamepadBridge &operator=(const GamepadBridge &) = delete;

  /// Configure from Sunshine's runtime settings. Safe to call more than once;
  /// changing the endpoint resets any live pads.
  void configure(std::string socket_path, std::string token, std::string token_name);

  [[nodiscard]] Availability availability() const;

  /// Forward a Sunshine gamepad state to the virtual pad in `slot`.
  /// Returns false when the bridge is not configured or the pad failed to start.
  bool update(int slot, std::uint32_t button_flags, std::uint8_t lt, std::uint8_t rt,
              std::int16_t lx, std::int16_t ly, std::int16_t rx, std::int16_t ry);

  /// Remove the virtual pad in `slot`.
  void release(int slot);

  /// Remove every virtual pad.
  void release_all();

  /// Install the handler that receives rumble for every pad. Called on a session
  /// thread; the slot is the Sunshine gamepad index. Set before the first update.
  using RumbleHandler = std::function<void(int slot, const RumbleCommand &)>;
  void set_rumble_handler(RumbleHandler handler);

  [[nodiscard]] ConnectError last_error() const;

 private:
  void ensure_pool_locked();

  mutable std::mutex mutex_;
  std::string socket_path_;
  std::string token_;
  std::string token_name_;
  std::unique_ptr<PadPool> pool_;
  ConnectError last_error_ = ConnectError::none;
  RumbleHandler rumble_handler_;
};

/// Process-wide bridge, so the input backend can reach it from the gamepad
/// entry points without threading a handle through Sunshine.
[[nodiscard]] GamepadBridge &global_bridge();

}  // namespace nexus::ojd::sunshine
