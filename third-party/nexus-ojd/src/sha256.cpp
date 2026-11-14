#include "nexus/ojd/sha256.hpp"

#include <cstring>

namespace nexus::ojd {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t value, int count) noexcept {
  return (value >> count) | (value << (32 - count));
}

/// Streaming SHA-256 state, used for both plain hashing and the HMAC inner/outer
/// passes.
class Sha256Context {
 public:
  Sha256Context() noexcept
      : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

  void update(const std::uint8_t *data, std::size_t size) noexcept {
    total_bytes_ += size;
    while (size > 0) {
      const std::size_t take = std::min(size, kBlockSize - buffered_);
      std::memcpy(buffer_.data() + buffered_, data, take);
      buffered_ += take;
      data += take;
      size -= take;
      if (buffered_ == kBlockSize) {
        transform(buffer_.data());
        buffered_ = 0;
      }
    }
  }

  [[nodiscard]] Sha256Digest finish() noexcept {
    // Append 0x80, pad with zeros, then the 64-bit big-endian bit length.
    const std::uint64_t bit_length = static_cast<std::uint64_t>(total_bytes_) * 8;
    const std::uint8_t pad_start = 0x80;
    update(&pad_start, 1);

    const std::uint8_t zero = 0;
    while (buffered_ != kBlockSize - 8) {
      update(&zero, 1);
    }

    std::array<std::uint8_t, 8> length_bytes{};
    for (int index = 0; index < 8; ++index) {
      length_bytes[index] = static_cast<std::uint8_t>(bit_length >> (56 - index * 8));
    }
    update(length_bytes.data(), length_bytes.size());

    Sha256Digest digest{};
    for (int word = 0; word < 8; ++word) {
      digest[word * 4 + 0] = static_cast<std::uint8_t>(state_[word] >> 24);
      digest[word * 4 + 1] = static_cast<std::uint8_t>(state_[word] >> 16);
      digest[word * 4 + 2] = static_cast<std::uint8_t>(state_[word] >> 8);
      digest[word * 4 + 3] = static_cast<std::uint8_t>(state_[word]);
    }
    return digest;
  }

 private:
  static constexpr std::size_t kBlockSize = 64;

  void transform(const std::uint8_t *block) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (int index = 0; index < 16; ++index) {
      schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                        (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                        (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                        static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (int index = 16; index < 64; ++index) {
      const std::uint32_t s0 = rotr(schedule[index - 15], 7) ^
                               rotr(schedule[index - 15], 18) ^
                               (schedule[index - 15] >> 3);
      const std::uint32_t s1 = rotr(schedule[index - 2], 17) ^
                               rotr(schedule[index - 2], 19) ^
                               (schedule[index - 2] >> 10);
      schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int index = 0; index < 64; ++index) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t choice = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[index] + schedule[index];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, kBlockSize> buffer_{};
  std::size_t buffered_ = 0;
  std::size_t total_bytes_ = 0;
};

}  // namespace

Sha256Digest sha256(const std::uint8_t *data, std::size_t size) noexcept {
  Sha256Context context;
  context.update(data, size);
  return context.finish();
}

Sha256Digest sha256(std::string_view text) noexcept {
  return sha256(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

Sha256Digest hmac_sha256(const std::uint8_t *key, std::size_t key_size,
                         const std::uint8_t *data, std::size_t data_size) noexcept {
  constexpr std::size_t block_size = 64;

  // Keys longer than the block size are hashed first; shorter ones are
  // zero-padded, per RFC 2104.
  Sha256Digest key_hash{};
  std::array<std::uint8_t, block_size> key_block{};
  if (key_size > block_size) {
    key_hash = sha256(key, key_size);
    std::memcpy(key_block.data(), key_hash.data(), key_hash.size());
  } else {
    std::memcpy(key_block.data(), key, key_size);
  }

  std::array<std::uint8_t, block_size> inner_pad{};
  std::array<std::uint8_t, block_size> outer_pad{};
  for (std::size_t index = 0; index < block_size; ++index) {
    inner_pad[index] = static_cast<std::uint8_t>(key_block[index] ^ 0x36);
    outer_pad[index] = static_cast<std::uint8_t>(key_block[index] ^ 0x5c);
  }

  Sha256Context inner;
  inner.update(inner_pad.data(), inner_pad.size());
  inner.update(data, data_size);
  const Sha256Digest inner_digest = inner.finish();

  Sha256Context outer;
  outer.update(outer_pad.data(), outer_pad.size());
  outer.update(inner_digest.data(), inner_digest.size());
  return outer.finish();
}

std::string to_hex(const std::uint8_t *data, std::size_t size) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  text.reserve(size * 2);
  for (std::size_t index = 0; index < size; ++index) {
    text.push_back(kDigits[data[index] >> 4]);
    text.push_back(kDigits[data[index] & 0x0F]);
  }
  return text;
}

std::string to_hex(const Sha256Digest &digest) {
  return to_hex(digest.data(), digest.size());
}

}  // namespace nexus::ojd
