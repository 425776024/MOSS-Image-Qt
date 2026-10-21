#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace videocut::base {

/// Lossless, independently decodable Alpha8 frame payload. The caller keeps
/// source PTS, dimensions and the compressed-byte digest in its own manifest.
bool EncodeAlpha8Zlib(const std::vector<std::uint8_t> &alpha,
                      std::vector<std::uint8_t> &encoded,
                      std::string &error);

/// Rejects truncated streams, trailing bytes and a decoded size mismatch.
bool DecodeAlpha8Zlib(std::string_view encoded, std::size_t decodedByteLength,
                      std::string &alpha, std::string &error);

} // namespace videocut::base
