#ifndef MOSS_FLOWEDIT_HPP
#define MOSS_FLOWEDIT_HPP

#include <fstream>
#include <iterator>
#include <stdexcept>
#include "model/diffusion/model.hpp"

// Copyright (c) 2024 Vladimir Kulikov. MIT; see LICENSE in this directory.
// Inference-only port of FlowEdit (Kulikov et al., ICCV 2025).
// Algorithm reference: https://github.com/fallenshock/FlowEdit (MIT).
// No model module, weight, attention processor, or spatial mask is changed.
namespace moss_flowedit {

static std::string option(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

static bool enabled() {
    return !option("MOSS_FLOWEDIT_MODE").empty();
}

static SDCondition source_condition(StableDiffusionGGML* sd,
                                    const sd::pipeline::GenerationRequest& request) {
    if (option("MOSS_FLOWEDIT_MODE") != "flow") {
        return {};
    }
    std::ifstream stream(option("MOSS_FLOWEDIT_SOURCE_PROMPT"));
    if (!stream) {
        throw std::runtime_error("FlowEdit source prompt file is required");
    }
    ConditionerParams params;
    params.text = std::string(std::istreambuf_iterator<char>(stream), {});
    if (params.text.empty() || params.text.size() > 65536) {
        throw std::runtime_error("FlowEdit source prompt must contain 1 to 65536 bytes");
    }
    params.clip_skip = request.clip_skip;
    params.width = request.width;
    params.height = request.height;
    sd::pipeline::ConditionerRunnerEndOnExit finish{sd->cond_stage_model.get()};
    return sd->get_learned_condition(params);
}

static sd::Tensor<float> sample(StableDiffusionGGML* sd,
                                const sd::Tensor<float>& source,
                                const sd::Tensor<float>& initial_noise,
                                const SDCondition& source_cond,
                                const sd::pipeline::ImageGenerationEmbeds& embeds,
                                const std::vector<float>& sigmas,
                                float target_cfg) {
    struct Finish {
        DiffusionModelRunner* runner;
        ~Finish() { runner->runner_end(); }
    } finish{sd->diffusion_model.get()};

    const std::string mode = option("MOSS_FLOWEDIT_MODE");
    const int steps = static_cast<int>(sigmas.size()) - 1;
    const int n_max = std::stoi(option("MOSS_FLOWEDIT_NMAX", "14"));
    const int n_min = std::stoi(option("MOSS_FLOWEDIT_NMIN", "0"));
    const int n_avg = std::stoi(option("MOSS_FLOWEDIT_NAVG", "1"));
    const float source_cfg = std::stof(option("MOSS_FLOWEDIT_SOURCE_CFG", std::to_string(target_cfg)));
    if (mode != "flow" && mode != "euler" && mode != "vae") {
        throw std::runtime_error("FlowEdit mode must be flow, euler or vae");
    }
    if (sd->version != VERSION_MOSS_IMAGE || source.empty() || steps < 1 ||
        (mode == "flow" && (source_cond.empty() || n_min < 0 || n_min > n_max || n_max < 1 || n_max > steps || n_avg < 1 || n_avg > 16 ||
                           !std::isfinite(source_cfg) || source_cfg < 1.0f || source_cfg > 10.0f ||
                           !std::isfinite(target_cfg) || target_cfg < 1.0f || target_cfg > 10.0f))) {
        throw std::runtime_error("Invalid FlowEdit configuration");
    }
    LOG_INFO("MOSS FlowEdit v1 mode=%s n_max=%d n_min=%d n_avg=%d source_cfg=%.3f target_cfg=%.3f; no mask, no reference tokens", mode.c_str(), n_max, n_min, n_avg, source_cfg, target_cfg);
    if (mode == "vae") {
        return source;
    }

    const std::vector<int> negative_skip_layers{22};
    auto velocity = [&](const sd::Tensor<float>& x, float sigma,
                        const SDCondition& positive, float cfg) -> sd::Tensor<float> {
        if (sd->get_cancel_flag() == SD_CANCEL_ALL) {
            return {};
        }
        auto timestep = sd::Tensor<float>::from_vector(sd->prepare_sample_timesteps(sigma, 0));
        DiffusionParams params;
        params.x = &x;
        params.timesteps = &timestep;
        params.context = &positive.c_crossattn;
        params.extra = SkipLayerDiffusionExtra{nullptr};
        auto pred = sd->diffusion_model->compute(sd->n_threads, params);
        if (pred.empty() || std::abs(cfg - 1.0f) < 1e-5f) {
            return pred;
        }
        if (embeds.uncond.empty()) {
            throw std::runtime_error("FlowEdit CFG requires encoded negative prompt");
        }
        params.context = &embeds.uncond.c_crossattn;
        // Match the existing MOSS pipeline's native negative branch exactly.
        params.extra = SkipLayerDiffusionExtra{&negative_skip_layers};
        auto negative = sd->diffusion_model->compute(sd->n_threads, params);
        if (negative.empty()) {
            return {};
        }
        return negative + (pred - negative) * cfg;
    };

    auto edited = source;
    if (mode == "euler") {
        edited = source * (1.0f - sigmas.front()) + initial_noise * sigmas.front();
    }
    bool regular_phase = mode == "euler";
    for (int i = 0; i < steps; ++i) {
        if (mode == "flow" && steps - i > n_max) {
            continue;
        }
        const float sigma = sigmas[i];
        const float dt = sigmas[i + 1] - sigma;
        if (!(sigma > 0.0f && dt <= 0.0f)) {
            throw std::runtime_error("FlowEdit needs a decreasing positive sigma schedule");
        }
        if (mode == "flow" && steps - i > n_min) {
            auto delta = sd::Tensor<float>::zeros_like(source);
            for (int k = 0; k < n_avg; ++k) {
                auto noise = sd::randn_like<float>(source, sd->sampler_rng);
                auto x_source = source * (1.0f - sigma) + noise * sigma;
                // This ordering cancels identical source/target latents exactly.
                auto x_target = x_source + (edited - source);
                auto v_source = velocity(x_source, sigma, source_cond, source_cfg);
                auto v_target = velocity(x_target, sigma, embeds.cond, target_cfg);
                if (v_source.empty() || v_target.empty()) {
                    return {};
                }
                delta += (v_target - v_source) / static_cast<float>(n_avg);
            }
            edited += delta * dt;
        } else {
            if (!regular_phase) {
                auto noise = sd::randn_like<float>(source, sd->sampler_rng);
                edited = source * (1.0f - sigma) + noise * sigma + (edited - source);
                regular_phase = true;
            }
            auto pred = velocity(edited, sigma, embeds.cond, target_cfg);
            if (pred.empty()) {
                return {};
            }
            // Keep the original Euler arithmetic for the parity control.
            auto denoised = pred * (-sigma) + edited;
            edited += ((edited - denoised) / sigma) * dt;
        }
        double squared_delta = 0.0;
        float max_delta = 0.0f;
        for (int64_t j = 0; j < edited.numel(); ++j) {
            float difference = edited.data()[j] - source.data()[j];
            if (!std::isfinite(edited.data()[j])) {
                throw std::runtime_error("Non-finite FlowEdit latent");
            }
            squared_delta += static_cast<double>(difference) * difference;
            max_delta = std::max(max_delta, std::abs(difference));
        }
        LOG_INFO("FlowEdit %d/%d - sigma=%.6f next=%.6f latent_rms=%.8f latent_max=%.8f", i + 1, steps, sigma, sigmas[i + 1], std::sqrt(squared_delta / edited.numel()), max_delta);
    }
    return edited;
}
}

#endif
