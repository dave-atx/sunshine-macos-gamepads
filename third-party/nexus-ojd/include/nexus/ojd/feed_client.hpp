// OJD endpoint transport: connect, handshake, and drive one virtual gamepad.
//
// One FeedClient owns one virtual gamepad. OJD runs at most 4 concurrently and
// closes a feed that sends no line for 2 seconds, so a caller must either send
// frames or KeepAlive within that window.
//
// SECURITY: the endpoint socket is a local IPC channel. The token grants control
// of virtual gamepads, so it is treated as a secret and never logged.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "nexus/ojd/protocol.hpp"
#include "nexus/ojd/reply.hpp"

namespace nexus::ojd {

enum class ConnectError {
  none,
  socket_failed,     ///< Could not create or connect the socket.
  handshake_failed,  ///< Challenge/Hello/Welcome did not complete.
  not_granted,       ///< E1002: the token or program has no Control scope.
  endpoint_disabled, ///< E1001: the endpoint is off.
  feed_refused,      ///< FeedRequest was refused (E1007, permission, ...).
  unsupported_api,   ///< E1009/E1010/E1011: this client speaks another version.
  io_failed,         ///< The socket failed mid-conversation.
};

/// Human-readable reason, for logs. Never includes the token.
[[nodiscard]] std::string_view to_string(ConnectError error) noexcept;

struct ConnectResult {
  ConnectError error = ConnectError::none;
  std::string detail;                   ///< Specific failure text, for logs.
  std::vector<std::string> supported_api_versions;  ///< Set on version errors.

  [[nodiscard]] bool ok() const { return error == ConnectError::none; }
};

struct FeedConfig {
  /// Endpoint socket path. Discover it with `ojd --json access status`.
  std::string socket_path;
  /// Token created with `ojd access grant --token NAME --scope control`.
  std::string token;
  /// Token name, as given to `ojd access grant`.
  std::string token_name;
  /// Which virtual pad identity to present.
  Persona persona = Persona::sdl_compatible;
  /// Per-operation socket timeout.
  std::chrono::milliseconds io_timeout{5000};
};

/// Drives one OJD virtual gamepad over a local socket.
///
/// Not thread-safe for concurrent sends: OJD requires frames in order, so
/// callers should send from one thread (or hold their own lock).
class FeedClient {
 public:
  FeedClient();
  ~FeedClient();

  FeedClient(const FeedClient &) = delete;
  FeedClient &operator=(const FeedClient &) = delete;

  /// Connect and create the virtual gamepad. Blocks for the handshake.
  [[nodiscard]] ConnectResult connect(const FeedConfig &config);

  /// Send one frame. Returns false when the feed is gone; a caller that ignores
  /// that must call disconnect() rather than keep sending.
  bool send_frame(const Frame &frame);

  /// Keep the feed alive without changing pad state (resets the 2s timeout).
  bool send_keepalive();

  /// Wait up to `timeout` for OJD to send a line, and return a rumble command if
  /// that line was one. Intended for a dedicated reader thread: it consumes the
  /// socket's read side, so a caller must not also call read_reply() directly.
  ///
  /// Returns nullopt on timeout, on end of stream, and for any non-rumble line.
  std::optional<RumbleCommand> wait_for_rumble(std::chrono::milliseconds timeout);

  /// Ask OJD to report when queued frames have played, then close.
  void disconnect();

  [[nodiscard]] bool connected() const { return connected_; }
  /// OJD instance version reported in Welcome, e.g. "0.6.0-alpha.2".
  [[nodiscard]] const std::string &service_version() const { return service_version_; }

 private:
  bool write_line(const std::string &line);
  [[nodiscard]] std::optional<Reply> read_reply();
  void close_socket();
  /// Map a Status failure onto a typed error.
  [[nodiscard]] ConnectResult classify_failure(const Reply &reply) const;

  int socket_ = -1;
  bool connected_ = false;
  FeedConfig config_;
  std::string service_version_;
  std::string read_buffer_;
};

}  // namespace nexus::ojd
