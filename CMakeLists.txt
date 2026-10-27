cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

if(NOT OS_WINDOWS)
  message(FATAL_ERROR "${_name} currently supports Windows only (VST2/VST3 editors are hosted as Win32 windows).")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/vst3sdk.cmake")

# Plugin hosting core, shared by the OBS module and the out-of-process scanner. No OBS or Qt dependency.
add_library(vstex-host STATIC)
target_sources(
  vstex-host
  PRIVATE
    src/host/plugin-host.cpp
    src/host/plugin-host.h
    src/host/vst2-abi.h
    src/host/vst2-instance.cpp
    src/host/vst3-instance.cpp
)
target_include_directories(vstex-host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(vstex-host PUBLIC vst3sdk_hosting)
target_compile_definitions(vstex-host PUBLIC NOMINMAX)

# Scanning loads every plugin once; doing it in a child process means a crashing plugin can't take OBS down.
add_executable(vst-ex-scanner src/scanner/scanner-main.cpp)
target_link_libraries(vst-ex-scanner PRIVATE vstex-host)
install(TARGETS vst-ex-scanner RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
add_custom_command(
  TARGET vst-ex-scanner
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:vst-ex-scanner>" "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  VERBATIM
)

# DSP self-check: `cmake --build ... --target pitch-shifter-test` then run it (non-zero exit = failure).
add_executable(pitch-shifter-test EXCLUDE_FROM_ALL src/dsp/pitch-shifter-test.cpp)
target_include_directories(pitch-shifter-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(beat-align-test EXCLUDE_FROM_ALL src/dsp/beat-align-test.cpp)
target_include_directories(beat-align-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(${CMAKE_PROJECT_NAME} MODULE)
add_dependencies(${CMAKE_PROJECT_NAME} vst-ex-scanner)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets vstex-host winmm
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/vst-filter.cpp
    src/vst-filter.h
    src/plugin-meta.cpp
    src/plugin-meta.h
    src/plugin-scanner.cpp
    src/plugin-scanner.h
    src/pitch-filter.cpp
    src/pitch-filter.h
    src/stereo-filter.cpp
    src/stereo-filter.h
    src/dsp/pseudo-stereo.h
    src/dsp/pitch-shifter.h
    src/switch-sets.cpp
    src/switch-sets.h
    src/ui/chain-view.cpp
    src/ui/chain-view.h
    src/ui/editor-window.cpp
    src/ui/editor-window.h
    src/ui/ex-filters-dialog.cpp
    src/ui/ex-filters-dialog.h
    src/ui/filter-history.cpp
    src/ui/filter-history.h
    src/ui/menu-hook.cpp
    src/ui/fx-dock.cpp
    src/ui/fx-dock.h
    src/ui/menu-hook.h
    src/ui/plugin-picker.cpp
    src/ui/plugin-picker.h
    src/ui/properties-panel.cpp
    src/ui/properties-panel.h
    src/ui/switch-dock.cpp
    src/ui/sync-dialog.cpp
    src/ui/sync-dialog.h
    src/dsp/beat-align.h
    src/ui/switch-dock.h
    src/ui/tools-dialog.cpp
    src/ui/tools-dialog.h
    src/ui/ui-text.h
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
