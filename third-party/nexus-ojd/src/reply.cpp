#include "nexus/ojd/reply.hpp"

#include <cstddef>

namespace nexus::ojd {
namespace {

/// A small cursor over a JSON document. Only what this client needs is
/// implemented; anything unexpected fails rather than being skipped silently.
class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  bool parse_object(Reply &reply) {
    skip_space();
    if (!consume('{')) {
      return false;
    }
    skip_space();
    if (consume('}')) {
      return true;
    }

    while (true) {
      skip_space();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_space();
      if (!consume(':')) {
        return false;
      }
      skip_space();
      if (!parse_value(key, reply)) {
        return false;
      }
      skip_space();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        return true;
      }
      return false;
    }
  }

  [[nodiscard]] bool at_end() {
    skip_space();
    return position_ >= text_.size();
  }

 private:
  void skip_space() {
    while (position_ < text_.size()) {
      const char character = text_[position_];
      if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
        ++position_;
      } else {
        break;
      }
    }
  }

  bool consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  bool parse_string(std::string &out) {
    if (!consume('"')) {
      return false;
    }
    out.clear();
    while (position_ < text_.size()) {
      const char character = text_[position_++];
      if (character == '"') {
        return true;
      }
      if (character == '\\') {
        if (position_ >= text_.size()) {
          return false;
        }
        const char escape = text_[position_++];
        switch (escape) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            // Escapes only matter for log text here. Consume the code point and
            // substitute a placeholder rather than decoding non-BMP sequences.
            if (position_ + 4 > text_.size()) {
              return false;
            }
            position_ += 4;
            out.push_back('?');
            break;
          }
          default:
            return false;
        }
        continue;
      }
      out.push_back(character);
    }
    return false;
  }

  bool parse_string_array(std::vector<std::string> &out) {
    if (!consume('[')) {
      return false;
    }
    out.clear();
    skip_space();
    if (consume(']')) {
      return true;
    }
    while (true) {
      skip_space();
      std::string item;
      if (!parse_string(item)) {
        return false;
      }
      out.push_back(std::move(item));
      skip_space();
      if (consume(',')) {
        continue;
      }
      if (consume(']')) {
        return true;
      }
      return false;
    }
  }

  bool parse_value(const std::string &key, Reply &reply) {
    if (key == "kind") {
      return parse_string(reply.kind);
    }
    if (key == "version") {
      return parse_string(reply.version);
    }
    if (key == "nonce") {
      return parse_string(reply.nonce);
    }
    if (key == "code") {
      return parse_string(reply.code);
    }
    if (key == "message") {
      return parse_string(reply.message);
    }
    if (key == "scopes") {
      return parse_string_array(reply.scopes);
    }
    if (key == "details") {
      return parse_details(reply);
    }
    if (key == "type") {
      return parse_string(reply.rumble_type);
    }
    if (key == "durationMilliseconds") {
      return parse_integer(reply.duration_milliseconds);
    }
    if (key == "intensities") {
      return parse_intensities(reply);
    }
    // Any other field is skipped structurally: it carries nothing this client
    // acts on, and rejecting it would break on additive upstream changes.
    return skip_value();
  }

  /// Parse a JSON integer, rejecting non-numeric text.
  bool parse_integer(std::int64_t &out) {
    bool negative = false;
    if (position_ < text_.size() && text_[position_] == '-') {
      negative = true;
      ++position_;
    }
    const std::size_t start = position_;
    std::int64_t value = 0;
    while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
      value = value * 10 + (text_[position_] - '0');
      ++position_;
    }
    if (position_ == start) {
      return false;
    }
    out = negative ? -value : value;
    return true;
  }

  /// Parse the RumbleCommand `intensities` object.
  bool parse_intensities(Reply &reply) {
    skip_space();
    if (!consume('{')) {
      return false;
    }
    reply.has_intensities = true;
    skip_space();
    if (consume('}')) {
      return true;
    }
    while (true) {
      skip_space();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_space();
      if (!consume(':')) {
        return false;
      }
      skip_space();
      if (key == "lowFrequency") {
        if (!parse_integer(reply.low_frequency)) return false;
      } else if (key == "highFrequency") {
        if (!parse_integer(reply.high_frequency)) return false;
      } else if (key == "leftTrigger") {
        if (!parse_integer(reply.left_trigger)) return false;
      } else if (key == "rightTrigger") {
        if (!parse_integer(reply.right_trigger)) return false;
      } else if (!skip_value()) {
        return false;
      }
      skip_space();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        return true;
      }
      return false;
    }
  }

  bool parse_details(Reply &reply) {
    skip_space();
    if (!consume('{')) {
      return false;
    }
    skip_space();
    if (consume('}')) {
      return true;
    }
    while (true) {
      skip_space();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_space();
      if (!consume(':')) {
        return false;
      }
      skip_space();
      if (key == "supportedAPIVersions") {
        if (!parse_string_array(reply.supported_versions)) {
          return false;
        }
      } else {
        if (!skip_value()) {
          return false;
        }
      }
      skip_space();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        return true;
      }
      return false;
    }
  }

  bool skip_value() {
    if (position_ >= text_.size()) {
      return false;
    }
    const char character = text_[position_];

    if (character == '"') {
      std::string discard;
      return parse_string(discard);
    }
    if (character == '{' || character == '[') {
      const char close = character == '{' ? '}' : ']';
      const char open = character;
      int depth = 0;
      bool in_string = false;
      while (position_ < text_.size()) {
        const char current = text_[position_];
        if (in_string) {
          if (current == '\\') {
            position_ += 2;
            continue;
          }
          if (current == '"') {
            in_string = false;
          }
          ++position_;
          continue;
        }
        if (current == '"') {
          in_string = true;
          ++position_;
          continue;
        }
        if (current == open) {
          ++depth;
        } else if (current == close) {
          --depth;
          if (depth == 0) {
            ++position_;
            return true;
          }
        }
        ++position_;
      }
      return false;
    }

    // Literal: true, false, null, or a number.
    const std::size_t start = position_;
    while (position_ < text_.size()) {
      const char current = text_[position_];
      if (current == ',' || current == '}' || current == ']' ||
          current == ' ' || current == '\t' || current == '\r' || current == '\n') {
        break;
      }
      ++position_;
    }
    return position_ > start;
  }

  std::string_view text_;
  std::size_t position_ = 0;
};

}  // namespace

bool Reply::has_scope(std::string_view scope) const {
  // OJD reports scopes in mixed case: requests use "Control", status output
  // uses "control".
  for (const auto &value : scopes) {
    if (value.size() != scope.size()) {
      continue;
    }
    bool same = true;
    for (std::size_t index = 0; index < value.size(); ++index) {
      const char left = value[index] >= 'A' && value[index] <= 'Z'
                            ? static_cast<char>(value[index] - 'A' + 'a')
                            : value[index];
      const char right = scope[index] >= 'A' && scope[index] <= 'Z'
                             ? static_cast<char>(scope[index] - 'A' + 'a')
                             : scope[index];
      if (left != right) {
        same = false;
        break;
      }
    }
    if (same) {
      return true;
    }
  }
  return false;
}

std::optional<Reply> parse_reply(std::string_view line) {
  if (line.empty()) {
    return std::nullopt;
  }
  Parser parser(line);
  Reply reply;
  if (!parser.parse_object(reply)) {
    return std::nullopt;
  }
  if (!parser.at_end()) {
    return std::nullopt;
  }
  if (reply.kind.empty()) {
    return std::nullopt;
  }
  return reply;
}

}  // namespace nexus::ojd
