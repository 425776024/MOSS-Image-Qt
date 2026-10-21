set(MOSS_AUDIO_DEPS "${CMAKE_SOURCE_DIR}/.local/audio-deps" CACHE PATH "Native voice dependencies")
set(_ort "${MOSS_AUDIO_DEPS}/onnxruntime")
set(_sherpa "${MOSS_AUDIO_DEPS}/sherpa")
find_package(PkgConfig REQUIRED)
pkg_check_modules(VOICE_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
find_package(OpenSSL REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_path(VOICE_JSON_INCLUDE nlohmann/json.hpp
    PATHS "${XINSHEN_SOURCE}/native/vendor" REQUIRED)
add_executable(moss-voice worker.cpp
    src/TtsBackend.cpp src/NativeTensor.cpp src/OrtRuntime.cpp src/VoiceTextFrontend.cpp
    src/TextNormalization.cpp src/AiTypes.cpp src/Inference.cpp
    src/Sha256.cpp src/Log.cpp src/MossModelAdapter.cpp
    ../xinshen-ditto/media_io.cpp ../xinshen-ditto/avatar_runtime.cpp)
target_include_directories(moss-voice PRIVATE src include "${_ort}/include" "${_sherpa}/include" "${VOICE_JSON_INCLUDE}" ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(moss-voice PRIVATE VIDEOCUT_AI_NATIVE_ORT_VERSION="1.23.2"
    VIDEOCUT_NATIVE_VISION_RECIPE_DIGEST="moss-voice-port-20261020" VIDEOCUT_AI_NATIVE_ALLOW_MODEL_CACHE=0)
target_compile_options(moss-voice PRIVATE -fno-rtti)
target_link_libraries(moss-voice PRIVATE "${_ort}/lib/libonnxruntime.dylib"
    "${_sherpa}/lib/libsherpa-onnx-kaldifst-core.a"
    "${_sherpa}/lib/libsherpa-onnx-fstfar.a" "${_sherpa}/lib/libsherpa-onnx-fst.a"
    PkgConfig::VOICE_FFMPEG OpenSSL::Crypto ${OpenCV_LIBS} "-framework CoreFoundation" "-framework Foundation")
set_target_properties(moss-voice PROPERTIES CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/workers")
