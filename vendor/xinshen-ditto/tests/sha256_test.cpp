#include "avatar_runtime.h"
#include <fstream>
#include <iostream>

int main() {
  const auto check = [](const std::string& actual, const std::string& expected) {
    if (actual != expected) throw std::runtime_error("SHA256 known vector mismatch");
  };
  check(avatar::sha256(nullptr, 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  check(avatar::sha256("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::string million(1000000, 'a');
  const auto expected = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
  check(avatar::sha256(million.data(), million.size()), expected);
  const auto path = std::filesystem::temp_directory_path() / "xinshen-sha256-test.bin";
  { std::ofstream out(path, std::ios::binary); out.write(million.data(), million.size()); }
  try {
    check(avatar::sha256(path), expected);
    { std::ofstream out(path, std::ios::binary | std::ios::app); out.put('b'); }
    if (avatar::sha256(path) == expected) throw std::runtime_error("Changed file was not detected");
  } catch (...) { std::filesystem::remove(path); throw; }
  std::filesystem::remove(path);
  const auto jsonPath = std::filesystem::temp_directory_path() / "xinshen-replace-json-test.json";
  try {
    avatar::writeJson(jsonPath, {{"version",1}});
    avatar::writeJson(jsonPath, {{"version",2}});
    if (avatar::readJson(jsonPath).at("version") != 2)
      throw std::runtime_error("Atomic replacement did not replace existing JSON");
  } catch (...) { std::filesystem::remove(jsonPath); throw; }
  std::filesystem::remove(jsonPath);
  bool missingRejected = false;
  try { avatar::sha256(path); } catch (const std::runtime_error&) { missingRejected = true; }
  if (!missingRejected) throw std::runtime_error("Missing file accepted");
  std::cout << "SHA256 empty, abc, million-byte memory/file, mutation and missing-file checks passed\n";
}
