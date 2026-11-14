#include "nexus/ojd/sunshine_bridge.hpp"

#include <mutex>

namespace nexus::ojd::sunshine {
namespace {

/// Matches the OJD documented limit of 4 concurrent virtual gamepads.
constexpr int kMaxPads = 4;

}  // namespace

std::string_view to_string(Availability availability) noexcept {
  switch (availability) {
    case Availability::ready:
      return "ready";
    case Availability::no_socket:
      return "the OpenJoystickDriver endpoint socket path is not configured";
    case Availability::no_token:
      return "the OpenJoystickDriver endpoint token is not configured";
  }
  return "unknown";
}

GamepadBridge::GamepadBridge() = default;

GamepadBridge::~GamepadBridge() {
  release_all();
}

void GamepadBridge::configure(std::string socket_path, std::string token,
                              std::string token_name) {
  std::lock_guard lock(mutex_);
  if (socket_path == socket_path_ && token == token_ && token_name == token_name_) {
    return;
  }

  // A different endpoint means every live pad belongs to the old one. Destroy the
  // pool outside this lock: teardown joins session threads.
  std::unique_ptr<PadPool> previous = std::move(pool_);

  socket_path_ = std::move(socket_path);
  token_ = std::move(token);
  token_name_ = std::move(token_name);
  last_error_ = ConnectError::none;

  // `previous` is released when this function returns, after the lock is gone.
  static_cast<void>(previous);
}

Availability GamepadBridge::availability() const {
  std::lock_guard lock(mutex_);
  if (socket_path_.empty()) {
    return Availability::no_socket;
  }
  if (token_.empty()) {
    return Availability::no_token;
  }
  return Availability::ready;
}

void GamepadBridge::ensure_pool_locked() {
  if (pool_) {
    return;
  }
  SessionConfig config;
  config.socket_path = socket_path_;
  config.token = token_;
  config.token_name = token_name_;
  config.persona = Persona::sdl_compatible;
  // OJD plays a frame for at least 8 ms, so publishing faster gains nothing.
  config.max_fps = 120;
  config.keepalive_after = std::chrono::milliseconds(500);
  config.max_pads = kMaxPads;
  // A restarted OJD must not require a Sunshine restart: sessions reconnect with
  // backoff, and the frame thread republishes absolute state once the feed is
  // back, so a momentary OJD outage costs a short input gap rather than the pad.
  config.reconnect = true;
  config.reconnect_initial = std::chrono::milliseconds(250);
  config.reconnect_max = std::chrono::milliseconds(5000);

  // Dispatch with the slot the session was created for. The handler is copied so
  // it stays valid even if the bridge is reconfigured mid-session.
  const RumbleHandler handler = rumble_handler_;
  config.on_rumble = [handler](int slot, const RumbleCommand &command) {
    if (handler) {
      handler(slot, command);
    }
  };
  pool_ = std::make_unique<PadPool>(config);
}

void GamepadBridge::set_rumble_handler(RumbleHandler handler) {
  std::lock_guard lock(mutex_);
  rumble_handler_ = std::move(handler);
  // A live pool captured the previous handler, so rebuild it to pick this one up.
  if (pool_) {
    pool_.reset();
  }
}

bool GamepadBridge::update(int slot, std::uint32_t button_flags, std::uint8_t lt,
                           std::uint8_t rt, std::int16_t lx, std::int16_t ly,
                           std::int16_t rx, std::int16_t ry) {
  {
    std::lock_guard lock(mutex_);
    if (socket_path_.empty() || token_.empty()) {
      return false;
    }
    ensure_pool_locked();
  }

  GamepadState state;
  state.button_flags = button_flags;
  state.lt = lt;
  state.rt = rt;
  state.lx = lx;
  state.ly = ly;
  state.rx = rx;
  state.ry = ry;

  // The pool serialises its own access; holding this lock across a blocking
  // handshake would stall Sunshine's input thread, so copy the pointer out.
  PadPool *pool = nullptr;
  {
    std::lock_guard lock(mutex_);
    pool = pool_.get();
  }
  if (!pool) {
    return false;
  }

  const bool ok = pool->update(slot, state);
  if (!ok) {
    std::lock_guard lock(mutex_);
    last_error_ = pool->last_error();
  }
  return ok;
}

void GamepadBridge::release(int slot) {
  PadPool *pool = nullptr;
  {
    std::lock_guard lock(mutex_);
    pool = pool_.get();
  }
  if (pool) {
    pool->release(slot);
  }
}

void GamepadBridge::release_all() {
  std::unique_ptr<PadPool> pool;
  {
    std::lock_guard lock(mutex_);
    pool = std::move(pool_);
  }
  // Destroyed outside the lock: teardown waits for session threads to join.
  pool.reset();
}

ConnectError GamepadBridge::last_error() const {
  std::lock_guard lock(mutex_);
  return last_error_;
}

GamepadBridge &global_bridge() {
  static GamepadBridge bridge;
  return bridge;
}

}  // namespace nexus::ojd::sunshine
