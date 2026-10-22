#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace avatar {
using Json = nlohmann::json;
void installCancellation();
void checkCancelled();
void progress(const std::string& stage, int64_t done, int64_t total);
Json readJson(const std::filesystem::path& path);
void writeJson(const std::filesystem::path& path, const Json& value);
void replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& destination);
std::filesystem::path member(const std::filesystem::path& root, const std::string& relative);
std::string sha256(const std::filesystem::path& path);
std::string sha256(const void* bytes, size_t size);
struct AudioPlan {
  std::vector<float> samples;
  std::vector<uint8_t> covered;
};
AudioPlan readAudioPlan(const std::filesystem::path& path);
}
