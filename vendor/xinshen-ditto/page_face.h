#pragma once
#include <filesystem>
// Returns the number of faces. Detection errors are surfaced, never treated
// as proof that the image has no portrait.
int pageFaceCount(const std::filesystem::path &image, const std::filesystem::path &models);
