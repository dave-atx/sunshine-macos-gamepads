// Minimal reader for OJD's single-line reply objects.
//
// OJD sends one JSON object per line. This parses only what the client needs:
// the `kind`, an optional `nonce`, an optional `code`, and the string array
// `scopes`. It is deliberately strict: unknown structure is a parse failure
// rather than a guess, because a wrong parse here could mean driving the wrong
// pad or misreading a refusal as success.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::ojd {

/// The reply shapes this client acts on.
struct Reply {
  std::string kind;                 ///< "Challenge", "Welcome", "FeedSession", "Status", ...
  std::string version;              ///< OJD instance version, set on Welcome.
  std::string nonce;                ///< Set on Challenge.
  std::string code;                 ///< Set on a Status failure, e.g. "E1002".
  std::string message;              ///< Human-readable Status text, for logs only.
  std::string rumble_type;          ///< Set on RumbleCommand: "SetRumble" or "StopRumble".
  std::vector<std::string> scopes;  ///< Set on Welcome.
  std::vector<std::string> supported_versions;  ///< Set on E1009/E1010/E1011.

  /// Rumble intensities, set on RumbleCommand. OJD's range is 0..65535.
  std::int64_t low_frequency = 0;
  std::int64_t high_frequency = 0;
  std::int64_t left_trigger = 0;
  std::int64_t right_trigger = 0;
  std::int64_t duration_milliseconds = -1;
  bool has_intensities = false;

  [[nodiscard]] bool is_failure() const { return kind == "Status"; }
  [[nodiscard]] bool has_scope(std::string_view scope) const;
};

/// Parse one reply line. Returns nullopt when the line is not a well-formed
/// object with a string `kind`.
[[nodiscard]] std::optional<Reply> parse_reply(std::string_view line);

}  // namespace nexus::ojd
