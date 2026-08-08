cmake_minimum_required(VERSION 3.24)

project(SappKit VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SAPPKIT_BUILD_PLUGIN "Build JUCE plugin + standalone targets" ON)
option(SAPPKIT_BUILD_TESTS  "Build unit tests"                       ON)
option(SAPPKIT_BUILD_CLI    "Build the sappkit agent CLI"            ON)

include(cmake/CompilerWarnings.cmake)
include(FetchContent)

# ------------------------------------------------------------- SappSounds ---
# Local development uses the sibling checkout; CI or isolated builds may set
# SAPPSOUNDS_DIR or fall back to FetchContent from the public repo.
set(SAPPSOUNDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sappsounds" CACHE PATH
    "Path to a local SappSounds checkout (sibling directory by default)")

if(EXISTS "${SAPPSOUNDS_DIR}/CMakeLists.txt")
  add_subdirectory("${SAPPSOUNDS_DIR}" "${CMAKE_BINARY_DIR}/SappSounds")
else()
  FetchContent_Declare(SappSounds
    GIT_REPOSITORY https://github.com/localteampartners/sappsounds.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SappSounds)
endif()

# ------------------------------------------------------------------- core ---
# Kit policy on top of Sapp::Sounds. Framework-independent (no JUCE):
# consumed by the plugin, the CLI, and the tests.
add_library(sappkit_core STATIC
  src/core/KitModel.cpp
  src/core/KitMix.cpp
  src/core/KitEngine.cpp
  src/core/KitRender.cpp
  src/core/SappLinkCCMap.cpp
  src/core/DiagnosticKit.cpp
)
target_include_directories(sappkit_core PUBLIC src)
target_link_libraries(sappkit_core PUBLIC Sapp::Sounds)
sappkit_set_warnings(sappkit_core)

# -------------------------------------------------------------------- cli ---
if(SAPPKIT_BUILD_CLI)
  add_executable(sappkit-cli src/cli/main.cpp)
  set_target_properties(sappkit-cli PROPERTIES OUTPUT_NAME sappkit)
  target_link_libraries(sappkit-cli PRIVATE sappkit_core)
  sappkit_set_warnings(sappkit-cli)
endif()

# ------------------------------------------------------------------ tests ---
if(SAPPKIT_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(SappKitTests
    tests/unit/test_kit_model.cpp
    tests/unit/test_kit_mix.cpp
    tests/unit/test_kit_engine.cpp
    tests/unit/test_render.cpp
    tests/unit/test_sapplink.cpp
  )
  target_link_libraries(SappKitTests PRIVATE sappkit_core Catch2::Catch2WithMain)
  target_compile_definitions(SappKitTests PRIVATE
    SAPPKIT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  sappkit_set_warnings(SappKitTests)

  enable_testing()
  add_test(NAME unit COMMAND SappKitTests)
endif()

# ----------------------------------------------------------------- plugin ---
if(SAPPKIT_BUILD_PLUGIN)
  # Same pinned JUCE tag as sappsynth/sapporchestra (see DECISIONS.md there).
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(JUCE)

  juce_add_plugin(SappKitPlugin
    PRODUCT_NAME                "SappKit"
    COMPANY_NAME                "LocalTeamPartners"
    BUNDLE_ID                   com.localteampartners.sappkit
    PLUGIN_MANUFACTURER_CODE    Ltpr
    PLUGIN_CODE                 Skit
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    FORMATS                     Standalone VST3 AU
  )
  if(APPLE)
    juce_enable_copy_plugin_step(SappKitPlugin)
  endif()

  target_sources(SappKitPlugin PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/SoundsPanel.cpp
  )
  target_link_libraries(SappKitPlugin PRIVATE
    sappkit_core
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
  )
  target_compile_definitions(SappKitPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
  )

  if(APPLE)
    # Offscreen editor snapshot tool (UI verification without screen capture).
    juce_add_gui_app(SappKitUiShot PRODUCT_NAME "SappKitUiShot")
    target_sources(SappKitUiShot PRIVATE
      tools/uishot/main.cpp
      src/plugin/PluginProcessor.cpp
      src/plugin/PluginEditor.cpp
      src/plugin/SoundsPanel.cpp
    )
    target_include_directories(SappKitUiShot PRIVATE src/plugin)
    target_link_libraries(SappKitUiShot PRIVATE
      sappkit_core
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_recommended_config_flags
    )
    target_compile_definitions(SappKitUiShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  endif()
endif()
