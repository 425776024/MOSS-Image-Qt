#pragma once
#include "../avatar_runtime.h"
namespace ditto {
void validateMotion(const std::filesystem::path &,
                    const std::filesystem::path &,
                    const std::filesystem::path &);
void validateDenoiser(const std::filesystem::path &models,
                      const std::filesystem::path &fixture,
                      const std::filesystem::path &result);
avatar::Json capabilities(const std::filesystem::path &models);
void prepareDriver(const std::filesystem::path &models,
                   const std::filesystem::path &avatar,
                   const std::filesystem::path &plan,
                   const std::filesystem::path &output);
void renderTimeline(const std::filesystem::path &models,
                    const std::filesystem::path &avatar,
                    const std::filesystem::path &plan,
                    const std::filesystem::path &output);
} // namespace ditto
