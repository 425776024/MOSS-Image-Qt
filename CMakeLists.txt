cmake_minimum_required(VERSION 3.24)
project(MossStudio VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /bigobj)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
option(MOSS_APP_STORE "Build with container-only storage for the Mac App Store" OFF)
if(MOSS_APP_STORE)
    # Apple-hosted model downloads are part of the Store edition's baseline.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "26.0")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()
set(MOSS_MINIMUM_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(NOT MOSS_MINIMUM_MACOS)
    set(MOSS_MINIMUM_MACOS "15.0")
endif()
set(MOSS_BUILD_NUMBER "1" CACHE STRING "Monotonically increasing Apple bundle build number")
if(NOT MOSS_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "MOSS_BUILD_NUMBER must be a positive integer")
endif()
execute_process(COMMAND git log -1 --format=%H WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MOSS_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git log -1 --format=%cI WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MOSS_BUILD_TIMESTAMP OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Multimedia Concurrent Svg)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.8)
qt_add_executable(MossStudio MACOSX_BUNDLE
    src/AppUpdate.cpp src/AppUpdate.h
    src/DiagnosticLog.cpp src/DiagnosticLog.h src/InferenceDiagnostics.cpp src/InferenceDiagnostics.h
    src/ConversationDocument.cpp src/ConversationDocument.h src/StudioConversation.cpp src/ConversationMediaStore.cpp src/ConversationMediaStore.h
    src/WorkspaceLibrary.cpp src/WorkspaceLibrary.h src/main.cpp src/ModelManager.cpp src/ModelManager.h src/Studio.cpp src/StudioTimeline.cpp src/Studio.h src/RemoteImageClient.cpp src/RemoteImageClient.h src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h src/Document.cpp src/Document.h src/SoundPlan.cpp src/SoundPlan.h)
qt_add_resources(MossStudio home_resources PREFIX "/home"
    FILES THIRD_PARTY.md assets/app-icon/xinshen-1024.png assets/home/wechat_contact_qr.png assets/home/wechat_official_account_qr.png)
if(NOT MOSS_APP_STORE)
    qt_add_resources(MossStudio update_helpers PREFIX "/updates"
        FILES scripts/update_macos.sh scripts/update_windows.ps1)
endif()
target_compile_definitions(MossStudio PRIVATE MOSS_APP_VERSION="${PROJECT_VERSION}"
    MOSS_BUILD_NUMBER="${MOSS_BUILD_NUMBER}" MOSS_BUILD_COMMIT="${MOSS_BUILD_COMMIT}"
    MOSS_BUILD_TIMESTAMP="${MOSS_BUILD_TIMESTAMP}")
if(WIN32)
    target_sources(MossStudio PRIVATE config/windows-app.rc)
    target_include_directories(MossStudio PRIVATE assets/app-icon)
    set_target_properties(MossStudio PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME XAvatar)
endif()
if(APPLE)
    foreach(language en zh-Hans)
        set(localized_info "config/localizations/${language}.lproj/InfoPlist.strings")
        set_source_files_properties("${localized_info}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${language}.lproj")
        target_sources(MossStudio PRIVATE "${localized_info}")
    endforeach()
    set_source_files_properties(assets/app-icon/Xinshen.icns config/PrivacyInfo.xcprivacy PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(MossStudio PRIVATE src/WindowAppearance.mm src/WindowAppearance.h assets/app-icon/Xinshen.icns config/PrivacyInfo.xcprivacy)
    set_target_properties(MossStudio PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/config/macos-Info.plist.in")
    target_link_libraries(MossStudio PRIVATE "-framework AppKit")
endif()
set_source_files_properties(qml/HomeTheme.qml qml/StudioTheme.qml qml/VideoCutTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(MossStudio URI MossStudio VERSION 1.0
    QML_FILES qml/HomeUpdates.qml
    QML_FILES qml/HomeMcp.qml
    RESOURCES qml/Translations.js qml/VoiceDesignPresets.js
    QML_FILES qml/GenerationAvatar.qml qml/AvatarDesignDialog.qml qml/VoiceStyleControls.qml qml/VoiceDesignDialog.qml qml/AvatarMotionControls.qml
    QML_FILES qml/ModelDownloadDialog.qml qml/HomeSection.qml qml/HomeChoiceCard.qml qml/CanvasSizePicker.qml qml/HomeDialog.qml qml/HomeNoticeDialog.qml qml/HomeImagePreview.qml qml/HomeMenu.qml
    QML_FILES qml/VideoCutTheme.qml qml/Glyph.qml qml/AppButton.qml qml/IconButton.qml qml/AppSlider.qml qml/AppCheckBox.qml qml/CardSurface.qml qml/AppSelect.qml qml/AppInputSurface.qml qml/AppMenuItem.qml qml/ConversationMessage.qml qml/ConversationVideo.qml
    QML_FILES qml/ConversationView.qml qml/ConversationInspector.qml qml/ConversationImageEditor.qml qml/ConversationAudio.qml qml/ConversationTimeline.qml qml/HomeTemplates.qml
    QML_FILES qml/EditorWindow.qml qml/HomeTheme.qml qml/HomeButton.qml qml/HomeField.qml qml/HomeHeader.qml qml/HomeTabs.qml qml/HomeEmpty.qml qml/HomeWorks.qml qml/HomeAvatars.qml qml/HomeVoices.qml qml/HomeVoiceCreator.qml qml/HomeAvatarCreator.qml qml/HomeSettings.qml qml/HomeModels.qml qml/HomeRemoteGpuSettings.qml qml/HomeCombo.qml qml/HomeContact.qml
    QML_FILES qml/WindowDragArea.qml qml/FrostedPanel.qml
    QML_FILES qml/RemoteGpuSettings.qml qml/CanvasObjectSettings.qml
    QML_FILES qml/Main.qml qml/StudioButton.qml qml/StudioField.qml qml/LayoutCanvas.qml qml/RepaintCanvas.qml qml/RepaintSettings.qml qml/NarrationCanvas.qml qml/NarrationSettings.qml qml/CreationPage.qml qml/TrackTimeline.qml qml/TrackPreview.qml qml/BackgroundSettings.qml qml/InputArea.qml qml/StudioCombo.qml qml/CompositionPage.qml qml/StudioTheme.qml qml/StudioIcon.qml qml/TemplateCard.qml qml/CreationPages.qml qml/StudioSlider.qml qml/StudioSpinBox.qml qml/StudioCheckBox.qml
    SOURCES src/AudioWaveform.h src/AudioWaveform.cpp src/EditDocument.h src/EditDocument.cpp src/MaskCanvas.h src/MaskCanvas.cpp src/LayerDocument.h src/LayerDocument.cpp src/LayerCanvas.h src/LayerCanvas.cpp)
target_include_directories(MossStudio PRIVATE src)
set(MOSS_MCP_SOURCES src/McpService.cpp src/McpService.h src/McpHttpServer.cpp src/McpHttpServer.h src/McpCatalog.cpp src/McpCatalog.h src/McpCallModel.cpp src/McpCallModel.h src/McpMediaStore.cpp src/McpMediaStore.h)
target_sources(MossStudio PRIVATE ${MOSS_MCP_SOURCES})
if(MOSS_APP_STORE)
    if(NOT APPLE)
        message(FATAL_ERROR "MOSS_APP_STORE requires macOS")
    endif()
    target_compile_definitions(MossStudio PRIVATE MOSS_APP_STORE=1)
    target_sources(MossStudio PRIVATE src/AppleAssetDownload.mm src/AppleAssetDownload.h)
    set_source_files_properties(src/AppleAssetDownload.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_options(MossStudio PRIVATE "SHELL:-weak_framework BackgroundAssets")
    set(MOSS_BACKGROUND_ASSETS_PLIST "<key>BAAppGroupID</key><string>group.ai.ffclip.studio.assets</string><key>BAHasManagedAssetPacks</key><true/><key>BAUsesAppleHosting</key><true/>")
    find_program(MOSS_SWIFTC swiftc REQUIRED)
    configure_file(config/apple-assets/Extension-Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Assets-Info.plist")
    set(MOSS_ASSET_EXTENSION "${CMAKE_CURRENT_BINARY_DIR}/XinshenAssets.appex")
    add_custom_command(OUTPUT "${MOSS_ASSET_EXTENSION}/Contents/MacOS/XinshenAssets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MOSS_ASSET_EXTENSION}/Contents/MacOS"
        COMMAND ${MOSS_SWIFTC} -parse-as-library -application-extension -O
            -target arm64-apple-macos26.0
            -Xlinker -e -Xlinker _NSExtensionMain
            "${CMAKE_CURRENT_SOURCE_DIR}/src/apple/DownloaderExtension.swift"
            -o "${MOSS_ASSET_EXTENSION}/Contents/MacOS/XinshenAssets"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/Assets-Info.plist" "${MOSS_ASSET_EXTENSION}/Contents/Info.plist"
        DEPENDS src/apple/DownloaderExtension.swift "${CMAKE_CURRENT_BINARY_DIR}/Assets-Info.plist" VERBATIM)
    add_custom_target(xinshen_assets_extension DEPENDS "${MOSS_ASSET_EXTENSION}/Contents/MacOS/XinshenAssets")
    add_dependencies(MossStudio xinshen_assets_extension)
    # Refresh the embedded extension when its binary or metadata changes.
    set_property(TARGET MossStudio APPEND PROPERTY LINK_DEPENDS "${MOSS_ASSET_EXTENSION}/Contents/MacOS/XinshenAssets")
    add_custom_command(TARGET MossStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${MOSS_ASSET_EXTENSION}" "$<TARGET_BUNDLE_DIR:MossStudio>/Contents/Extensions/XinshenAssets.appex")
endif()
target_link_libraries(MossStudio PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Svg)
set_target_properties(MossStudio PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "新身数字人"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ai.ffclip.studio"
    MACOSX_BUNDLE_ICON_FILE "Xinshen.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MOSS_BUILD_NUMBER}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_compile_definitions(MossStudio PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
if(BUILD_TESTING)
function(moss_add_qt_test name target)
    if(WIN32)
        add_test(NAME "${name}" COMMAND "${target}"
            -o "${CMAKE_CURRENT_BINARY_DIR}/${name}-test.txt,txt" -v1)
    else()
        add_test(NAME "${name}" COMMAND "${target}")
    endif()
endfunction()
qt_add_executable(app_update_tests tests/app_update_tests.cpp src/AppUpdate.cpp src/AppUpdate.h)
target_include_directories(app_update_tests PRIVATE src)
target_link_libraries(app_update_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
moss_add_qt_test(app_update app_update_tests)
qt_add_executable(store_app_update_tests tests/app_update_tests.cpp src/AppUpdate.cpp src/AppUpdate.h)
target_include_directories(store_app_update_tests PRIVATE src)
target_compile_definitions(store_app_update_tests PRIVATE MOSS_APP_STORE=1)
target_link_libraries(store_app_update_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
moss_add_qt_test(store_app_update store_app_update_tests)
qt_add_executable(user_file_export_tests tests/user_file_export_tests.cpp)
target_include_directories(user_file_export_tests PRIVATE src)
target_link_libraries(user_file_export_tests PRIVATE Qt6::Core Qt6::Test)
moss_add_qt_test(user_file_export user_file_export_tests)
qt_add_executable(workspace_library_tests tests/workspace_library_tests.cpp src/WorkspaceLibrary.cpp src/WorkspaceLibrary.h src/SoundPlan.cpp src/SoundPlan.h)
target_include_directories(workspace_library_tests PRIVATE src)
target_compile_definitions(workspace_library_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(workspace_library_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
moss_add_qt_test(workspace_library workspace_library_tests)
qt_add_executable(document_tests tests/document_tests.cpp src/Document.cpp src/Document.h)
target_include_directories(document_tests PRIVATE src)
target_link_libraries(document_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
moss_add_qt_test(document document_tests)
qt_add_executable(edit_tests tests/edit_tests.cpp src/EditDocument.cpp src/EditDocument.h)
target_include_directories(edit_tests PRIVATE src)
target_link_libraries(edit_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
moss_add_qt_test(edit edit_tests)
qt_add_executable(layer_tests tests/layer_tests.cpp src/LayerDocument.cpp src/LayerDocument.h)
target_include_directories(layer_tests PRIVATE src)
target_link_libraries(layer_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Svg Qt6::Test)
moss_add_qt_test(layers layer_tests)
qt_add_executable(creation_pages_tests tests/creation_pages_tests.cpp
    src/DiagnosticLog.cpp src/DiagnosticLog.h src/InferenceDiagnostics.cpp src/InferenceDiagnostics.h
    src/ConversationDocument.cpp src/ConversationDocument.h src/StudioConversation.cpp src/ConversationMediaStore.cpp src/ConversationMediaStore.h
    src/ModelManager.cpp src/ModelManager.h src/Studio.cpp src/StudioTimeline.cpp src/Studio.h src/RemoteImageClient.cpp src/RemoteImageClient.h src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h src/Document.cpp src/Document.h src/SoundPlan.cpp src/SoundPlan.h
    src/EditDocument.cpp src/EditDocument.h src/LayerDocument.cpp src/LayerDocument.h)
target_include_directories(creation_pages_tests PRIVATE src)
target_compile_definitions(creation_pages_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(creation_pages_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Svg Qt6::Test)
moss_add_qt_test(creation_pages creation_pages_tests)
qt_add_executable(diagnostic_log_tests tests/diagnostic_log_tests.cpp src/DiagnosticLog.cpp src/InferenceDiagnostics.cpp)
target_include_directories(diagnostic_log_tests PRIVATE src)
target_link_libraries(diagnostic_log_tests PRIVATE Qt6::Core Qt6::Test)
moss_add_qt_test(diagnostic_log diagnostic_log_tests)
add_executable(mouth_cache_tests tests/mouth_cache_tests.cpp)
add_test(NAME mouth_cache COMMAND mouth_cache_tests)
endif()
option(MOSS_BUILD_DITTO "Build native Ditto worker" ON)
if(MOSS_BUILD_DITTO)
    add_subdirectory(vendor/xinshen-ditto)
endif()
option(MOSS_BUILD_VOICE "Build native voice synthesis worker" ON)
if(MOSS_BUILD_VOICE)
    add_subdirectory(vendor/videocut-voice)
endif()

if(BUILD_TESTING)
qt_add_executable(timeline_tests tests/timeline_tests.cpp src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h)
target_include_directories(timeline_tests PRIVATE src)
target_link_libraries(timeline_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Multimedia Qt6::Test)
moss_add_qt_test(timeline timeline_tests)
find_package(Python3 QUIET COMPONENTS Interpreter)

qt_add_executable(audio_waveform_tests tests/audio_waveform_tests.cpp src/AudioWaveform.cpp src/AudioWaveform.h)
target_include_directories(audio_waveform_tests PRIVATE src)
target_link_libraries(audio_waveform_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Test)
moss_add_qt_test(audio_waveform audio_waveform_tests)
endif()

foreach(model_target MossStudio creation_pages_tests)
    if(TARGET ${model_target})
    qt_add_resources(${model_target} model_resources PREFIX "/models" FILES
        config/models.json config/ditto-original/config.json config/ditto-original/schedule.json config/ditto-original/manifest.json)
    qt_add_resources(${model_target} ${model_target}_layout_templates PREFIX "/layouts" FILES config/layout-templates.json)
    endif()
endforeach()
if(BUILD_TESTING)
qt_add_executable(model_manager_tests tests/model_manager_tests.cpp src/ModelManager.cpp src/ModelManager.h)
target_include_directories(model_manager_tests PRIVATE src)
target_link_libraries(model_manager_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Test)
qt_add_resources(model_manager_tests model_resources PREFIX "/models" FILES config/models.json
    config/ditto-original/config.json config/ditto-original/schedule.json config/ditto-original/manifest.json)
moss_add_qt_test(model_manager model_manager_tests)
if(Python3_Interpreter_FOUND)
    add_test(NAME public_release COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_release_tests.py")
    add_test(NAME macos_distribution COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_distribution_tests.py")
endif()

endif()

if(MOSS_APP_STORE)
    if(BUILD_TESTING)
    qt_add_executable(store_model_channel_tests tests/store_model_channel_tests.cpp
        src/ModelManager.cpp src/ModelManager.h src/AppleAssetDownload.mm src/AppleAssetDownload.h)
    target_include_directories(store_model_channel_tests PRIVATE src)
    target_compile_definitions(store_model_channel_tests PRIVATE MOSS_APP_STORE=1)
    qt_add_resources(store_model_channel_tests model_resources PREFIX "/models" FILES
        config/models.json config/ditto-original/config.json config/ditto-original/schedule.json config/ditto-original/manifest.json)
    target_link_libraries(store_model_channel_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Test)
    target_link_options(store_model_channel_tests PRIVATE "SHELL:-weak_framework BackgroundAssets" "SHELL:-framework Foundation")
    moss_add_qt_test(store_model_channel store_model_channel_tests)
    endif()
    foreach(store_target MossStudio store_model_channel_tests)
        if(TARGET ${store_target})
        qt_add_resources(${store_target} store_model_resources PREFIX "/models" FILES
            config/models-store.json config/ditto-store/config.json config/ditto-store/manifest.json)
        endif()
    endforeach()

endif()

qt_add_resources(MossStudio sound_model_terms PREFIX "/model-terms" FILES docs/model-terms.md)

foreach(download_target MossStudio creation_pages_tests model_manager_tests)
    if(TARGET ${download_target})
    target_sources(${download_target} PRIVATE src/ModelHttpDownload.cpp src/ModelHttpDownload.h)
    endif()
endforeach()
foreach(runtime_target MossStudio creation_pages_tests)
    if(TARGET ${runtime_target})
        target_sources(${runtime_target} PRIVATE src/RuntimeManager.cpp src/RuntimeManager.h)
        qt_add_resources(${runtime_target} ${runtime_target}_runtime_helpers PREFIX "/runtime" FILES scripts/install_runtime.ps1)
    endif()
endforeach()
if(TARGET store_model_channel_tests)
    target_sources(store_model_channel_tests PRIVATE src/ModelHttpDownload.cpp src/ModelHttpDownload.h)
endif()
if(BUILD_TESTING)
qt_add_executable(model_http_download_tests tests/model_http_download_tests.cpp src/ModelHttpDownload.cpp src/ModelHttpDownload.h)
target_include_directories(model_http_download_tests PRIVATE src)
target_link_libraries(model_http_download_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME model_http_download COMMAND model_http_download_tests)
if(Python3_Interpreter_FOUND)
qt_add_executable(runtime_manager_tests tests/runtime_manager_tests.cpp src/RuntimeManager.cpp src/RuntimeManager.h src/ModelHttpDownload.cpp src/ModelHttpDownload.h)
target_include_directories(runtime_manager_tests PRIVATE src)
target_link_libraries(runtime_manager_tests PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Test)
target_compile_definitions(runtime_manager_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}" MOSS_TEST_PYTHON="${Python3_EXECUTABLE}")
qt_add_resources(runtime_manager_tests runtime_test_helpers PREFIX "/runtime" FILES scripts/install_runtime.ps1)
moss_add_qt_test(runtime_manager runtime_manager_tests)
endif()

# Exercise the MCP boundary against the real application services in an isolated root.
if(NOT MOSS_APP_STORE) # Tests rely on output-root overrides unavailable in sandbox builds.
get_target_property(MOSS_MCP_TEST_CORE creation_pages_tests SOURCES)
list(FILTER MOSS_MCP_TEST_CORE EXCLUDE REGEX "tests/creation_pages_tests\\.cpp$")
qt_add_executable(mcp_tests tests/mcp_tests.cpp ${MOSS_MCP_TEST_CORE} ${MOSS_MCP_SOURCES} src/WorkspaceLibrary.cpp src/WorkspaceLibrary.h)
target_include_directories(mcp_tests PRIVATE src)
target_compile_definitions(mcp_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mcp_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Svg Qt6::Test)
qt_add_resources(mcp_tests mcp_model_terms PREFIX "/model-terms" FILES docs/model-terms.md)
moss_add_qt_test(mcp mcp_tests)
endif()
endif()
