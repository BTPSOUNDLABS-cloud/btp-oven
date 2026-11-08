cmake_minimum_required(VERSION 3.22)

project(BtPOven VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# JUCE location (override with -DJUCE_DIR=... if yours lives somewhere else)
# ------------------------------------------------------------------------------
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to the JUCE framework")
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE was not found at '${JUCE_DIR}'. Pass -DJUCE_DIR=<path-to-JUCE>.")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

# ------------------------------------------------------------------------------
# Plugin target
# ------------------------------------------------------------------------------
juce_add_plugin(BtPOven
    COMPANY_NAME              "BtP"
    BUNDLE_ID                 "com.btp.oven"
    PLUGIN_MANUFACTURER_CODE  Btp1
    PLUGIN_CODE               Ovn1
    PRODUCT_NAME              "BtP Oven"
    DESCRIPTION               "Dual-loop granular trap melody mutation engine"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           Fx
    FORMATS                   VST3 Standalone
)

target_sources(BtPOven PRIVATE
    Source/Params.h
    Source/AiEngine.h
    Source/AiEngine.cpp
    Source/AiPage.h
    Source/AiPage.cpp
    Source/LoopEngine.h
    Source/LoopEngine.cpp
    Source/FxRecorder.h
    Source/FxRecorder.cpp
    Source/TrapFX.h
    Source/TrapFX.cpp
    Source/RackFX.h
    Source/RackFX.cpp
    Source/AutoTune.h
    Source/AutoTune.cpp
    Source/StemEngine.h
    Source/StemEngine.cpp
    Source/StemPage.h
    Source/StemPage.cpp
    Source/OneShotPage.h
    Source/OneShotPage.cpp
    Source/RvcEngine.h
    Source/RvcEngine.cpp
    Source/RvcPage.h
    Source/RvcPage.cpp
    Source/RaveEngine.h
    Source/RaveEngine.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/OvenLookAndFeel.h
    Source/OvenLookAndFeel.cpp
    Source/OvenWindow.h
    Source/OvenWindow.cpp
    Source/RackPage.h
    Source/RackPage.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_link_libraries(BtPOven PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
)

juce_generate_juce_header(BtPOven)

# ------------------------------------------------------------------------------
# In-process ACE-Step AI engine (ggml + acestep-core, no Python, no server)
# ------------------------------------------------------------------------------
set(ACESTEP_DIR "D:/Dev/ACE-Step-1.5/acestep.vst3" CACHE PATH
    "ACE-Step C++ engine sources (ggml + acestep-core)")
option(BTP_ENABLE_AI "Compile the built-in ACE-Step generator into the plugin" ON)

if(BTP_ENABLE_AI AND EXISTS "${ACESTEP_DIR}/src/pipeline-synth.cpp")
    message(STATUS "BtP Oven: building in-process ACE-Step engine from ${ACESTEP_DIR}")

    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(BTP_AI_CUDA ON)
        set(CMAKE_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA architectures" FORCE)
        message(STATUS "BtP Oven: CUDA backend enabled (native / RTX Blackwell)")
    else()
        set(BTP_AI_CUDA OFF)
        message(STATUS "BtP Oven: no CUDA toolkit found, AI runs on CPU")
    endif()

    set(GGML_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
    set(GGML_CUDA            ${BTP_AI_CUDA} CACHE BOOL "" FORCE)
    set(GGML_BACKEND_DL      OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)

    add_compile_definitions(GGML_MAX_NAME=128)
    add_subdirectory("${ACESTEP_DIR}/ggml" "${CMAKE_BINARY_DIR}/acestep-ggml")

    add_library(btp-yyjson STATIC "${ACESTEP_DIR}/vendor/yyjson/yyjson.c")
    target_include_directories(btp-yyjson PUBLIC "${ACESTEP_DIR}/vendor/yyjson")
    if(MSVC)
        target_compile_options(btp-yyjson PRIVATE /W0)
    endif()

    add_library(btp-acestep STATIC
        "${ACESTEP_DIR}/src/request.cpp"
        "${ACESTEP_DIR}/src/pipeline-lm.cpp"
        "${ACESTEP_DIR}/src/pipeline-synth.cpp"
    )
    target_include_directories(btp-acestep PUBLIC "${ACESTEP_DIR}/src")
    target_include_directories(btp-acestep SYSTEM PUBLIC "${ACESTEP_DIR}/ggml/include")
    if(MSVC)
        target_compile_options(btp-acestep PRIVATE /W0 /wd4100 /wd4505)
    endif()
    target_link_libraries(btp-acestep PUBLIC btp-yyjson ggml ggml-base)

    foreach(backend cpu cuda)
        if(TARGET ggml-${backend})
            target_link_libraries(btp-acestep PUBLIC ggml-${backend})
        endif()
    endforeach()

    target_link_libraries(BtPOven PRIVATE btp-acestep)
    target_compile_definitions(BtPOven PRIVATE BTP_HAS_AI=1)

    if(BTP_AI_CUDA)
        add_executable(btp-ai-smoke Test/AiSmoke.cpp)
        target_link_libraries(btp-ai-smoke PRIVATE btp-acestep)
        if(MSVC)
            target_compile_options(btp-ai-smoke PRIVATE /W0)
        endif()

        add_executable(btp-vocal-smoke Test/VocalSmoke.cpp)
        target_link_libraries(btp-vocal-smoke PRIVATE btp-acestep)
        if(MSVC)
            target_compile_options(btp-vocal-smoke PRIVATE /W0)
        endif()
    endif()
else()
    message(STATUS "BtP Oven: AI engine disabled (ACESTEP_DIR not found)")
endif()

# ------------------------------------------------------------------------------
# In-process demucs.cpp stem separation (C++17 + Eigen, no Python, no server)
# ------------------------------------------------------------------------------
set(DEMUCS_DIR "D:/Dev/demucs.cpp" CACHE PATH
    "demucs.cpp stem-separation sources (Demucs v4 htdemucs)")
option(BTP_ENABLE_STEM "Compile the built-in demucs.cpp stem separator" ON)

if(BTP_ENABLE_STEM AND EXISTS "${DEMUCS_DIR}/src/model.hpp")
    message(STATUS "BtP Oven: building in-process demucs.cpp stem separator")

    find_package(OpenMP QUIET)

    add_library(btp-demucs STATIC
        "${DEMUCS_DIR}/src/crosstransformer.cpp"
        "${DEMUCS_DIR}/src/dsp.cpp"
        "${DEMUCS_DIR}/src/encdec.cpp"
        "${DEMUCS_DIR}/src/layers.cpp"
        "${DEMUCS_DIR}/src/lstm.cpp"
        "${DEMUCS_DIR}/src/model_apply.cpp"
        "${DEMUCS_DIR}/src/model_inference.cpp"
        "${DEMUCS_DIR}/src/model_load.cpp"
    )
    target_include_directories(btp-demucs PUBLIC "${DEMUCS_DIR}/src")
    target_include_directories(btp-demucs SYSTEM PUBLIC "${DEMUCS_DIR}/vendor/eigen")
    if(MSVC)
        target_compile_options(btp-demucs PRIVATE /W0 /bigobj)
        target_compile_definitions(btp-demucs PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
    if(OpenMP_CXX_FOUND)
        target_link_libraries(btp-demucs PUBLIC OpenMP::OpenMP_CXX)
    endif()

    target_link_libraries(BtPOven PRIVATE btp-demucs)
    target_compile_definitions(BtPOven PRIVATE BTP_HAS_STEM=1)

    add_executable(btp-stem-smoke Test/StemSmoke.cpp)
    target_link_libraries(btp-stem-smoke PRIVATE btp-demucs)
    if(MSVC)
        target_compile_options(btp-stem-smoke PRIVATE /W0 /bigobj)
    endif()
else()
    message(STATUS "BtP Oven: stem separator disabled (DEMUCS_DIR not found)")
endif()

# ------------------------------------------------------------------------------
# In-process RVC voice conversion (onnxruntime, no Python at runtime)
# ------------------------------------------------------------------------------
set(ONNX_DIR "D:/Dev/onnxruntime/onnxruntime-win-x64-1.31.1" CACHE PATH
    "onnxruntime C++ package root (include/ + lib/)")
set(RVC_DIR "D:/Dev/rvc-models" CACHE PATH "RVC ONNX model folder")
option(BTP_ENABLE_RVC "Compile the built-in RVC voice converter" ON)

# The RAVE variant engine and its voice_multi weights are CC-BY-NC 4.0.
# Keep it ON for personal / free builds. Build with -DBTP_ENABLE_RAVE=OFF
# to ship a commercial build that contains no non-commercial code or models.
option(BTP_ENABLE_RAVE "Compile the built-in RAVE variant engine (CC-BY-NC models)" ON)

if(BTP_ENABLE_RVC AND EXISTS "${ONNX_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "BtP Oven: building in-process RVC (onnxruntime)")

    add_executable(btp-onnx-inspect Test/OnnxInspect.cpp)
    target_include_directories(btp-onnx-inspect PRIVATE "${ONNX_DIR}/include")
    target_link_libraries(btp-onnx-inspect PRIVATE "${ONNX_DIR}/lib/onnxruntime.lib")
    if(MSVC)
        target_compile_options(btp-onnx-inspect PRIVATE /W0 /bigobj)
    endif()
    add_custom_command(TARGET btp-onnx-inspect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_DIR}/lib/onnxruntime.dll"
            "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            $<TARGET_FILE_DIR:btp-onnx-inspect>)

    # --- plugin: link onnxruntime + RVC engine ---------------------------------
    target_include_directories(BtPOven PRIVATE "${ONNX_DIR}/include")
    target_link_libraries(BtPOven PRIVATE "${ONNX_DIR}/lib/onnxruntime.lib")
    target_compile_definitions(BtPOven PRIVATE BTP_HAS_RVC=1)

    if(BTP_ENABLE_RAVE)
        target_compile_definitions(BtPOven PRIVATE BTP_HAS_RAVE=1)
        message(STATUS "BtP Oven: RAVE variant engine ENABLED (CC-BY-NC models, non-commercial)")
    else()
        target_compile_definitions(BtPOven PRIVATE BTP_HAS_RAVE=0)
        message(STATUS "BtP Oven: RAVE variant engine DISABLED (commercial build)")
    endif()

    add_custom_command(TARGET BtPOven_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_DIR}/lib/onnxruntime.dll"
            "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:BtPOven_VST3>")
    add_custom_command(TARGET BtPOven_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_DIR}/lib/onnxruntime.dll"
            "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:BtPOven_Standalone>")

    # --- RVC smoke test --------------------------------------------------------
    add_executable(btp-rvc-smoke
        Test/RvcSmoke.cpp
        Source/RvcEngine.cpp
    )
    target_include_directories(btp-rvc-smoke PRIVATE
        "${ONNX_DIR}/include"
        "Source"
        "${CMAKE_BINARY_DIR}/BtPOven_artefacts/JuceLibraryCode"
    )
    target_link_libraries(btp-rvc-smoke PRIVATE
        "${ONNX_DIR}/lib/onnxruntime.lib"
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_graphics
        juce::juce_data_structures
        juce::juce_events
        juce::juce_core
    )
    target_compile_definitions(btp-rvc-smoke PRIVATE BTP_HAS_RVC=1)
    if(MSVC)
        target_compile_options(btp-rvc-smoke PRIVATE /W0 /bigobj)
    endif()
    add_custom_command(TARGET btp-rvc-smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_DIR}/lib/onnxruntime.dll"
            "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            $<TARGET_FILE_DIR:btp-rvc-smoke>)

    # --- RAVE smoke test ------------------------------------------------------
    if(BTP_ENABLE_RAVE)
    add_executable(btp-rave-smoke
        Test/RaveSmoke.cpp
        Source/RaveEngine.cpp
    )
    target_include_directories(btp-rave-smoke PRIVATE
        "${ONNX_DIR}/include"
        "Source"
        "${CMAKE_BINARY_DIR}/BtPOven_artefacts/JuceLibraryCode"
    )
    target_link_libraries(btp-rave-smoke PRIVATE
        "${ONNX_DIR}/lib/onnxruntime.lib"
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_graphics
        juce::juce_data_structures
        juce::juce_events
        juce::juce_core
    )
    target_compile_definitions(btp-rave-smoke PRIVATE BTP_HAS_RAVE=1)
    if(MSVC)
        target_compile_options(btp-rave-smoke PRIVATE /W0 /bigobj)
    endif()
    add_custom_command(TARGET btp-rave-smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_DIR}/lib/onnxruntime.dll"
            "${ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            $<TARGET_FILE_DIR:btp-rave-smoke>)
    endif()
else()
    message(STATUS "BtP Oven: RVC disabled (ONNX_DIR not found)")
    target_compile_definitions(BtPOven PRIVATE BTP_HAS_RVC=0 BTP_HAS_RAVE=0)
endif()

target_compile_definitions(BtPOven PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

# Keep build output tidy
set_target_properties(BtPOven PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
