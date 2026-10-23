# Loaded after project() by CMAKE_PROJECT_INCLUDE. Let CMake merge manifests
# through its native source-file handling instead of overriding linker flags.
get_property(moss_manifest_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if(NOT MSVC OR moss_manifest_try_compile)
    return()
endif()
include_guard(GLOBAL)
set(MOSS_WINDOWS_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/windows.manifest")

function(moss_attach_windows_manifests directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind STREQUAL "EXECUTABLE")
            target_sources("${target}" PRIVATE "${MOSS_WINDOWS_MANIFEST}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        moss_attach_windows_manifests("${child}")
    endforeach()
endfunction()

# Workers and upstream executables may be declared in nested directories.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL moss_attach_windows_manifests "${CMAKE_SOURCE_DIR}")
