#include "nexus/ojd/feed_client.hpp"

#include "nexus/ojd/reply.hpp"

#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nexus::ojd {
namespace {

/// Set a send/recv timeout on the socket so a stalled peer cannot hang the
/// caller. OJD is local, so a short timeout is enough.
void apply_timeout(int socket, std::chrono::milliseconds timeout) {
  timeval value{};
  value.tv_sec = static_cast<time_t>(timeout.count() / 1000);
  value.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
  ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
  ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value));
}

/// OJD caps a line at 64 KiB; a longer line means a protocol error.
constexpr std::size_t kMaxLineBytes = 64 * 1024;

}  // namespace

std::string_view to_string(ConnectError error) noexcept {
  switch (error) {
    case ConnectError::none:
      return "ok";
    case ConnectError::socket_failed:
      return "cannot connect to the OpenJoystickDriver endpoint socket";
    case ConnectError::handshake_failed:
      return "handshake failed";
    case ConnectError::not_granted:
      return "the endpoint token has no Control scope";
    case ConnectError::endpoint_disabled:
      return "the OpenJoystickDriver endpoint is disabled";
    case ConnectError::feed_refused:
      return "the virtual gamepad was refused";
    case ConnectError::unsupported_api:
      return "the endpoint does not accept this apiVersion";
    case ConnectError::io_failed:
      return "the socket failed mid-conversation";
  }
  return "unknown error";
}

FeedClient::FeedClient() = default;

FeedClient::~FeedClient() {
  disconnect();
}

void FeedClient::close_socket() {
  if (socket_ >= 0) {
    ::close(socket_);
    socket_ = -1;
  }
  connected_ = false;
  read_buffer_.clear();
}

bool FeedClient::write_line(const std::string &line) {
  if (socket_ < 0) {
    return false;
  }
  std::string payload = line;
  payload.push_back('\n');

  std::size_t offset = 0;
  while (offset < payload.size()) {
    // MSG_NOSIGNAL: a peer that closed mid-write must surface as an error, not
    // as SIGPIPE, which would kill the host process. This matters because OJD
    // drops a feed (and closes the socket) when it refuses a line or times out.
    const auto written = ::send(
        socket_, payload.data() + offset, payload.size() - offset, MSG_NOSIGNAL);
    if (written <= 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

std::optional<Reply> FeedClient::read_reply() {
  if (socket_ < 0) {
    return std::nullopt;
  }

  while (true) {
    const auto newline = read_buffer_.find('\n');
    if (newline != std::string::npos) {
      const std::string line = read_buffer_.substr(0, newline);
      read_buffer_.erase(0, newline + 1);
      return parse_reply(line);
    }

    if (read_buffer_.size() > kMaxLineBytes) {
      return std::nullopt;
    }

    char buffer[4096];
    const auto received = ::recv(socket_, buffer, sizeof(buffer), 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::nullopt;
    }
    if (received == 0) {
      // Peer closed. A final line without a newline still counts.
      if (!read_buffer_.empty()) {
        const auto reply = parse_reply(read_buffer_);
        read_buffer_.clear();
        return reply;
      }
      return std::nullopt;
    }
    read_buffer_.append(buffer, static_cast<std::size_t>(received));
  }
}

ConnectResult FeedClient::classify_failure(const Reply &reply) const {
  ConnectResult result;
  result.detail = reply.code;
  if (!reply.message.empty()) {
    result.detail += ": ";
    result.detail += reply.message;
  }
  result.supported_api_versions = reply.supported_versions;

  if (reply.code == "E1001") {
    result.error = ConnectError::endpoint_disabled;
  } else if (reply.code == "E1002") {
    result.error = ConnectError::not_granted;
  } else if (reply.code == "E1009" || reply.code == "E1010" || reply.code == "E1011") {
    result.error = ConnectError::unsupported_api;
  } else {
    result.error = ConnectError::feed_refused;
  }
  return result;
}

ConnectResult FeedClient::connect(const FeedConfig &config) {
  disconnect();
  config_ = config;

  if (config.socket_path.empty()) {
    return {ConnectError::socket_failed, "no socket path configured", {}};
  }

  socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_ < 0) {
    return {ConnectError::socket_failed, std::strerror(errno), {}};
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (config.socket_path.size() >= sizeof(address.sun_path)) {
    close_socket();
    return {ConnectError::socket_failed, "socket path is too long", {}};
  }
  std::memcpy(address.sun_path, config.socket_path.c_str(),
              config.socket_path.size() + 1);

  if (::connect(socket_, reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) != 0) {
    const std::string detail = std::strerror(errno);
    close_socket();
    return {ConnectError::socket_failed, detail, {}};
  }

  apply_timeout(socket_, config.io_timeout);

  // 1. Challenge
  const auto challenge = read_reply();
  if (!challenge || challenge->kind != "Challenge" || challenge->nonce.empty()) {
    close_socket();
    return {ConnectError::handshake_failed, "no Challenge received", {}};
  }

  // 2. Hello
  const std::string hello = config.token.empty()
                                ? build_hello_signature_grant()
                                : build_hello_token(config.token_name, config.token,
                                                    challenge->nonce);
  if (!write_line(hello)) {
    close_socket();
    return {ConnectError::io_failed, "could not send Hello", {}};
  }

  // 3. Welcome
  const auto welcome = read_reply();
  if (!welcome) {
    close_socket();
    return {ConnectError::handshake_failed, "no Welcome received", {}};
  }
  if (welcome->is_failure()) {
    const auto result = classify_failure(*welcome);
    close_socket();
    return result;
  }
  if (welcome->kind != "Welcome") {
    close_socket();
    return {ConnectError::handshake_failed, "unexpected reply: " + welcome->kind, {}};
  }
  if (!welcome->has_scope("Control")) {
    close_socket();
    return {ConnectError::not_granted, "Control scope was not granted", {}};
  }
  service_version_ = welcome->version;

  // 4. FeedRequest
  if (!write_line(build_feed_request(config.persona))) {
    close_socket();
    return {ConnectError::io_failed, "could not send FeedRequest", {}};
  }

  // 5. FeedSession
  const auto session = read_reply();
  if (!session) {
    close_socket();
    return {ConnectError::feed_refused, "no FeedSession received", {}};
  }
  if (session->is_failure()) {
    const auto result = classify_failure(*session);
    close_socket();
    return result;
  }
  if (session->kind != "FeedSession") {
    close_socket();
    return {ConnectError::feed_refused, "unexpected reply: " + session->kind, {}};
  }

  connected_ = true;
  return {ConnectError::none, {}, {}};
}

bool FeedClient::send_frame(const Frame &frame) {
  if (!connected_) {
    return false;
  }
  if (!write_line(build_frame(frame))) {
    connected_ = false;
    return false;
  }
  return true;
}

bool FeedClient::send_keepalive() {
  if (!connected_) {
    return false;
  }
  if (!write_line(build_keepalive())) {
    connected_ = false;
    return false;
  }
  return true;
}

std::optional<RumbleCommand> FeedClient::wait_for_rumble(std::chrono::milliseconds timeout) {
  if (socket_ < 0) {
    return std::nullopt;
  }

  // Use select() rather than SO_RCVTIMEO: the send path also uses this socket,
  // and a receive timeout would apply to writes as well.
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(socket_, &readable);
  timeval value{};
  value.tv_sec = static_cast<time_t>(timeout.count() / 1000);
  value.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

  const int ready = ::select(socket_ + 1, &readable, nullptr, nullptr, &value);
  if (ready <= 0) {
    return std::nullopt;
  }

  const auto reply = read_reply();
  if (!reply) {
    return std::nullopt;
  }
  return parse_rumble_command(*reply);
}

void FeedClient::disconnect() {
  if (socket_ < 0) {
    connected_ = false;
    return;
  }

  // Ask for drained confirmation, but never block teardown on it: OJD drops
  // the pad immediately when the connection closes.
  if (connected_) {
    if (write_line(build_drain())) {
      auto reply = read_reply();
      if (reply && reply->kind == "Drained") {
        // Ideal path: every queued frame played.
      }
    }
  }

  connected_ = false;
  close_socket();
}

}  // namespace nexus::ojd
