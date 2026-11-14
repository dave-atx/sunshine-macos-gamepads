// SHA-256 and HMAC-SHA256.
//
// The OJD endpoint token proof is an HMAC-SHA256, so this component needs both.
// They are implemented here instead of using Sunshine's OpenSSL helpers so the
// protocol core stays dependency-free and testable on its own. Both are checked
// against published test vectors in the test suite.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nexus::ojd {

inline constexpr std::size_t kSha256Size = 32;
using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

/// Compute SHA-256 over a byte range.
[[nodiscard]] Sha256Digest sha256(const std::uint8_t *data, std::size_t size) noexcept;
[[nodiscard]] Sha256Digest sha256(std::string_view text) noexcept;

/// Compute HMAC-SHA256 with the given key over a byte range.
[[nodiscard]] Sha256Digest hmac_sha256(
    const std::uint8_t *key, std::size_t key_size,
    const std::uint8_t *data, std::size_t data_size) noexcept;

/// Lowercase hex encoding.
[[nodiscard]] std::string to_hex(const std::uint8_t *data, std::size_t size);
[[nodiscard]] std::string to_hex(const Sha256Digest &digest);

}  // namespace nexus::ojd
