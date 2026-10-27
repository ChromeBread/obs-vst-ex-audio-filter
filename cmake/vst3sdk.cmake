# Steinberg VST3 SDK (MIT licensed since 3.8) - only the interfaces and hosting helpers are needed.

include_guard(GLOBAL)
include(FetchContent)

set(VST3SDK_TAG v3.8.1_build_84)
set(VST3SDK_ROOT "${CMAKE_BINARY_DIR}/_deps/vst3sdk")

# SOURCE_SUBDIR points to a missing folder so FetchContent only downloads and never adds the SDK's own CMake project.
FetchContent_Declare(
  vst3_pluginterfaces
  URL "https://github.com/steinbergmedia/vst3_pluginterfaces/archive/refs/tags/${VST3SDK_TAG}.tar.gz"
  URL_HASH SHA256=19e88e225b6ecb4a3c429d3ff2fd511be9c6c8b8b1c7e86b05949b45c0553e2d
  SOURCE_DIR "${VST3SDK_ROOT}/pluginterfaces"
  SOURCE_SUBDIR _no_cmake
)
FetchContent_Declare(
  vst3_public_sdk
  URL "https://github.com/steinbergmedia/vst3_public_sdk/archive/refs/tags/${VST3SDK_TAG}.tar.gz"
  URL_HASH SHA256=f683db0c831dd6603a1bb8c7747a6e1dd223ac3423e288b08c5706cf30b76ae8
  SOURCE_DIR "${VST3SDK_ROOT}/public.sdk"
  SOURCE_SUBDIR _no_cmake
)
FetchContent_MakeAvailable(vst3_pluginterfaces vst3_public_sdk)

set(_pi "${VST3SDK_ROOT}/pluginterfaces")
set(_ps "${VST3SDK_ROOT}/public.sdk/source")

add_library(vst3sdk_hosting STATIC)
target_sources(
  vst3sdk_hosting
  PRIVATE
    "${_pi}/base/conststringtable.cpp"
    "${_pi}/base/coreiids.cpp"
    "${_pi}/base/funknown.cpp"
    "${_pi}/base/ustring.cpp"
    "${_ps}/common/commoniids.cpp"
    "${_ps}/common/commonstringconvert.cpp"
    "${_ps}/common/memorystream.cpp"
    "${_ps}/common/threadchecker_win32.cpp"
    "${_ps}/vst/hosting/connectionproxy.cpp"
    "${_ps}/vst/hosting/eventlist.cpp"
    "${_ps}/vst/hosting/hostclasses.cpp"
    "${_ps}/vst/hosting/module.cpp"
    "${_ps}/vst/hosting/module_win32.cpp"
    "${_ps}/vst/hosting/parameterchanges.cpp"
    "${_ps}/vst/hosting/pluginterfacesupport.cpp"
    "${_ps}/vst/hosting/plugprovider.cpp"
    "${_ps}/vst/hosting/processdata.cpp"
    "${_ps}/vst/utility/stringconvert.cpp"
    "${_ps}/vst/vstinitiids.cpp"
)
target_include_directories(vst3sdk_hosting PUBLIC "${VST3SDK_ROOT}")
target_compile_definitions(vst3sdk_hosting PUBLIC RELEASE=1)
target_compile_features(vst3sdk_hosting PUBLIC cxx_std_17)
target_link_libraries(vst3sdk_hosting PUBLIC ole32 shell32)
# Third-party code: don't let its warnings fail the CI build.
set_target_properties(vst3sdk_hosting PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
