#include "videocut/ai/AiTypes.h"

namespace videocut::ai {

const char *AiTaskName(const AiTask value) noexcept {
  switch (value) {
  case AiTask::Vad:
    return "vad";
  case AiTask::Asr:
    return "asr";
  case AiTask::Tts:
    return "tts";
  case AiTask::AudioEnhancement:
    return "audio_enhancement";
  case AiTask::AudioSeparation:
    return "audio_separation";
  case AiTask::FaceDetection:
    return "face_detection";
  case AiTask::FaceEmbedding:
    return "face_embedding";
  case AiTask::FaceLandmarks:
    return "face_landmarks";
  case AiTask::VisualUnderstanding:
    return "visual_understanding";
  case AiTask::PersonSegmentation:
    return "person_segmentation";
  case AiTask::SemanticSegmentation:
    return "semantic_segmentation";
  case AiTask::PersonDetection:
    return "person_detection";
  case AiTask::BodyPose:
    return "body_pose";
  case AiTask::PalmDetection:
    return "palm_detection";
  case AiTask::HandPose:
    return "hand_pose";
  case AiTask::DepthEstimation:
    return "depth_estimation";
  }
  return "unknown";
}

const char *ComputePreferenceName(const ComputePreference value) noexcept {
  switch (value) {
  case ComputePreference::AcceleratedPreferred:
    return "accelerated_preferred";
  case ComputePreference::CpuOnly:
    return "cpu_only";
  case ComputePreference::AcceleratedOnly:
    return "accelerated_only";
  case ComputePreference::ModelPreferred:
    return "model_preferred";
  }
  return "unknown";
}

const char *ComputeBackendName(const ComputeBackend value) noexcept {
  switch (value) {
  case ComputeBackend::Cpu:
    return "cpu";
  case ComputeBackend::CoreMl:
    return "coreml";
  case ComputeBackend::Metal:
    return "metal";
  case ComputeBackend::Cuda:
    return "cuda";
  case ComputeBackend::Vulkan:
    return "vulkan";
  }
  return "unknown";
}

const char *SessionResidencyName(const SessionResidency value) noexcept {
  switch (value) {
  case SessionResidency::OnDemand:
    return "on_demand";
  case SessionResidency::KeepWarm:
    return "keep_warm";
  case SessionResidency::ProcessResident:
    return "process_resident";
  }
  return "unknown";
}

const char *AiErrorCodeName(const AiErrorCode value) noexcept {
  switch (value) {
  case AiErrorCode::None:
    return "none";
  case AiErrorCode::InvalidArgument:
    return "invalid_argument";
  case AiErrorCode::CatalogInvalid:
    return "catalog_invalid";
  case AiErrorCode::ModelNotFound:
    return "model_not_found";
  case AiErrorCode::ModelFileMissing:
    return "model_file_missing";
  case AiErrorCode::ModelFileChanged:
    return "model_file_changed";
  case AiErrorCode::ModelFileCorrupt:
    return "model_file_corrupt";
  case AiErrorCode::ModelPathEscapesRoot:
    return "model_path_escapes_root";
  case AiErrorCode::BackendUnavailable:
    return "backend_unavailable";
  case AiErrorCode::SessionCreationFailed:
    return "session_creation_failed";
  case AiErrorCode::BatchTooLarge:
    return "batch_too_large";
  case AiErrorCode::TensorInvalid:
    return "tensor_invalid";
  case AiErrorCode::InferenceFailed:
    return "inference_failed";
  case AiErrorCode::RuntimeShuttingDown:
    return "runtime_shutting_down";
  case AiErrorCode::AlreadyInstalled:
    return "already_installed";
  case AiErrorCode::Canceled:
    return "canceled";
  }
  return "unknown";
}

bool IsAcceleratedBackend(const ComputeBackend value) noexcept {
  return value != ComputeBackend::Cpu;
}

} // namespace videocut::ai
