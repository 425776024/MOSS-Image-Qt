cmake_minimum_required(VERSION 3.24)
project(MossStudio VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Multimedia Concurrent Svg Test)
qt_standard_project_setup(REQUIRES 6.8)
qt_add_executable(MossStudio MACOSX_BUNDLE
    src/ConversationDocument.cpp src/ConversationDocument.h src/StudioConversation.cpp src/ConversationMediaStore.cpp src/ConversationMediaStore.h
    src/WorkspaceLibrary.cpp src/WorkspaceLibrary.h src/main.cpp src/ModelManager.cpp src/ModelManager.h src/Studio.cpp src/StudioTimeline.cpp src/Studio.h src/RemoteImageClient.cpp src/RemoteImageClient.h src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h src/Document.cpp src/Document.h src/SoundPlan.cpp src/SoundPlan.h)
qt_add_resources(MossStudio home_resources PREFIX "/home"
    FILES THIRD_PARTY.md assets/home/wechat_contact_qr.png)
if(APPLE)
    target_sources(MossStudio PRIVATE src/WindowAppearance.mm src/WindowAppearance.h)
    target_link_libraries(MossStudio PRIVATE "-framework AppKit")
endif()
set_source_files_properties(qml/HomeTheme.qml qml/StudioTheme.qml qml/VideoCutTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(MossStudio URI MossStudio VERSION 1.0
    QML_FILES qml/ModelDownloadDialog.qml qml/HomeSection.qml qml/HomeChoiceCard.qml qml/CanvasSizePicker.qml qml/HomeDialog.qml qml/HomeMenu.qml
    QML_FILES qml/VideoCutTheme.qml qml/Glyph.qml qml/AppButton.qml qml/IconButton.qml qml/AppSlider.qml qml/AppCheckBox.qml qml/CardSurface.qml qml/AppSelect.qml qml/AppInputSurface.qml qml/AppMenuItem.qml qml/ConversationMessage.qml qml/ConversationVideo.qml
    QML_FILES qml/ConversationView.qml qml/ConversationInspector.qml qml/ConversationImageEditor.qml qml/ConversationAudio.qml qml/ConversationTimeline.qml qml/HomeTemplates.qml
    QML_FILES qml/EditorWindow.qml qml/HomeTheme.qml qml/HomeButton.qml qml/HomeField.qml qml/HomeHeader.qml qml/HomeTabs.qml qml/HomeEmpty.qml qml/HomeWorks.qml qml/HomeAvatars.qml qml/HomeVoices.qml qml/HomeAvatarCreator.qml qml/HomeSettings.qml qml/HomeModels.qml qml/HomeRemoteGpuSettings.qml qml/HomeCombo.qml qml/HomeContact.qml
    QML_FILES qml/FrostedPanel.qml
    QML_FILES qml/RemoteGpuSettings.qml qml/CanvasObjectSettings.qml
    QML_FILES qml/Main.qml qml/StudioButton.qml qml/StudioField.qml qml/LayoutCanvas.qml qml/RepaintCanvas.qml qml/RepaintSettings.qml qml/NarrationCanvas.qml qml/NarrationSettings.qml qml/CreationPage.qml qml/TrackTimeline.qml qml/TrackPreview.qml qml/BackgroundSettings.qml qml/InputArea.qml qml/StudioCombo.qml qml/CompositionPage.qml qml/StudioTheme.qml qml/StudioIcon.qml qml/TemplateCard.qml qml/CreationPages.qml qml/StudioSlider.qml qml/StudioSpinBox.qml qml/StudioCheckBox.qml
    SOURCES src/AudioWaveform.h src/AudioWaveform.cpp src/EditDocument.h src/EditDocument.cpp src/MaskCanvas.h src/MaskCanvas.cpp src/LayerDocument.h src/LayerDocument.cpp src/LayerCanvas.h src/LayerCanvas.cpp)
target_include_directories(MossStudio PRIVATE src)
target_link_libraries(MossStudio PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Svg)
set_target_properties(MossStudio PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Moss Studio"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ai.moss.studio"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")
target_compile_definitions(MossStudio PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
qt_add_executable(workspace_library_tests tests/workspace_library_tests.cpp src/WorkspaceLibrary.cpp src/WorkspaceLibrary.h src/SoundPlan.cpp src/SoundPlan.h)
target_include_directories(workspace_library_tests PRIVATE src)
target_compile_definitions(workspace_library_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(workspace_library_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME workspace_library COMMAND workspace_library_tests)
qt_add_executable(document_tests tests/document_tests.cpp src/Document.cpp src/Document.h)
target_include_directories(document_tests PRIVATE src)
target_link_libraries(document_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME document COMMAND document_tests)
qt_add_executable(edit_tests tests/edit_tests.cpp src/EditDocument.cpp src/EditDocument.h)
target_include_directories(edit_tests PRIVATE src)
target_link_libraries(edit_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME edit COMMAND edit_tests)
qt_add_executable(layer_tests tests/layer_tests.cpp src/LayerDocument.cpp src/LayerDocument.h)
target_include_directories(layer_tests PRIVATE src)
target_link_libraries(layer_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Svg Qt6::Test)
add_test(NAME layers COMMAND layer_tests)
qt_add_executable(creation_pages_tests tests/creation_pages_tests.cpp
    src/ConversationDocument.cpp src/ConversationDocument.h src/StudioConversation.cpp src/ConversationMediaStore.cpp src/ConversationMediaStore.h
    src/ModelManager.cpp src/ModelManager.h src/Studio.cpp src/StudioTimeline.cpp src/Studio.h src/RemoteImageClient.cpp src/RemoteImageClient.h src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h src/Document.cpp src/Document.h src/SoundPlan.cpp src/SoundPlan.h
    src/EditDocument.cpp src/EditDocument.h src/LayerDocument.cpp src/LayerDocument.h)
target_include_directories(creation_pages_tests PRIVATE src)
target_compile_definitions(creation_pages_tests PRIVATE MOSS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(creation_pages_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Svg Qt6::Test)
add_test(NAME creation_pages COMMAND creation_pages_tests)
add_executable(mouth_cache_tests tests/mouth_cache_tests.cpp)
add_test(NAME mouth_cache COMMAND mouth_cache_tests)
option(MOSS_BUILD_DITTO "Build native Ditto worker" ON)
if(MOSS_BUILD_DITTO)
    add_subdirectory(vendor/xinshen-ditto)
endif()
option(MOSS_BUILD_VOICE "Build native voice synthesis worker" ON)
if(MOSS_BUILD_VOICE)
    add_subdirectory(vendor/videocut-voice)
endif()

qt_add_executable(timeline_tests tests/timeline_tests.cpp src/TimelineDocument.cpp src/TimelineDocument.h src/AudioDurationProbe.cpp src/AudioDurationProbe.h)
target_include_directories(timeline_tests PRIVATE src)
target_link_libraries(timeline_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Multimedia Qt6::Test)
add_test(NAME timeline COMMAND timeline_tests)
find_package(Python3 QUIET COMPONENTS Interpreter)

qt_add_executable(audio_waveform_tests tests/audio_waveform_tests.cpp src/AudioWaveform.cpp src/AudioWaveform.h)
target_include_directories(audio_waveform_tests PRIVATE src)
target_link_libraries(audio_waveform_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Test)
add_test(NAME audio_waveform COMMAND audio_waveform_tests)

foreach(model_target MossStudio creation_pages_tests)
    qt_add_resources(${model_target} model_resources PREFIX "/models" FILES
        config/models.json config/ditto-original/config.json config/ditto-original/schedule.json config/ditto-original/manifest.json)
endforeach()
qt_add_executable(model_manager_tests tests/model_manager_tests.cpp src/ModelManager.cpp src/ModelManager.h)
target_include_directories(model_manager_tests PRIVATE src)
target_link_libraries(model_manager_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Test)
qt_add_resources(model_manager_tests model_resources PREFIX "/models" FILES config/models.json
    config/ditto-original/config.json config/ditto-original/schedule.json config/ditto-original/manifest.json)
add_test(NAME model_manager COMMAND model_manager_tests)
if(Python3_Interpreter_FOUND)
    add_test(NAME public_release COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_release_tests.py")
    add_test(NAME macos_distribution COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_distribution_tests.py")
endif()
