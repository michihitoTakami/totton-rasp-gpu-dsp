cmake_minimum_required(VERSION 3.20)
project(totton_rasp_gpu_dsp LANGUAGES CXX)

option(ENABLE_VULKAN "Enable Vulkan upsampler" ON)
option(USE_VKFFT "Use VkFFT backend when Vulkan is enabled" ON)
option(ENABLE_ALSA "Enable ALSA streaming app" ON)
option(ENABLE_ZMQ "Enable ZeroMQ control server" ON)
option(ENABLE_OPT "Enable compiler optimizations" ON)
option(ENABLE_TESTS "Build test binaries" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ENABLE_OPT)
    # Disable optimization when running under emulation to avoid GCC ICE.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-O0>)
endif()

add_library(vulkan_upsampler
    src/vulkan/vulkan_streaming_upsampler.cpp
)

target_include_directories(vulkan_upsampler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vulkan_upsampler PUBLIC cxx_std_17)

add_library(audio_eq
    src/audio/eq_parser.cpp
    src/audio/eq_to_fir.cpp
)
target_include_directories(audio_eq
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(audio_eq PUBLIC cxx_std_17)

if(ENABLE_TESTS)
    enable_testing()
endif()

if(ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(vulkan_upsampler PRIVATE ENABLE_VULKAN=1)
    target_link_libraries(vulkan_upsampler PRIVATE Vulkan::Vulkan)

    if(USE_VKFFT)
        include(FetchContent)
        FetchContent_Declare(
            vkfft
            GIT_REPOSITORY https://github.com/DTolm/VkFFT.git
            GIT_TAG v1.3.1
        )
        FetchContent_GetProperties(vkfft)
        if(NOT vkfft_POPULATED)
            FetchContent_Populate(vkfft)
        endif()
        target_compile_definitions(vulkan_upsampler PRIVATE USE_VKFFT=1 VKFFT_BACKEND=0)
        target_include_directories(vulkan_upsampler PRIVATE ${vkfft_SOURCE_DIR})
    endif()
endif()

if(ENABLE_TESTS)
    add_executable(upsampler_smoke
        tests/cpp/test_vulkan_upsampler.cpp
    )
    target_link_libraries(upsampler_smoke PRIVATE vulkan_upsampler)
    add_test(NAME upsampler_smoke COMMAND upsampler_smoke)

    add_executable(eq_parser_smoke
        tests/cpp/test_eq_parser_smoke.cpp
    )
    target_link_libraries(eq_parser_smoke PRIVATE audio_eq)
    add_test(NAME eq_parser_smoke COMMAND eq_parser_smoke)

    add_executable(eq_to_fir_smoke
        tests/cpp/test_eq_to_fir_smoke.cpp
    )
    target_link_libraries(eq_to_fir_smoke PRIVATE audio_eq)
    add_test(NAME eq_to_fir_smoke COMMAND eq_to_fir_smoke)

    if(ENABLE_VULKAN)
        target_compile_definitions(upsampler_smoke PRIVATE ENABLE_VULKAN=1)
        target_link_libraries(upsampler_smoke PRIVATE Vulkan::Vulkan)
    endif()
endif()

if(ENABLE_ALSA)
    find_package(ALSA REQUIRED)

    # Auto-negotiation library (Issue #12)
    add_library(auto_negotiation
        src/audio/auto_negotiation.cpp
        src/io/dac_capability.cpp
    )
    target_include_directories(auto_negotiation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(auto_negotiation PUBLIC ALSA::ALSA)
    target_compile_features(auto_negotiation PUBLIC cxx_std_17)

    add_library(alsa_utils
        src/alsa/alsa_common.cpp
        src/alsa/alsa_filter_selector.cpp
    )
    target_include_directories(alsa_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(alsa_utils PUBLIC ALSA::ALSA)

    add_executable(alsa_streamer
        src/alsa/alsa_streamer_main.cpp
    )
    target_link_libraries(alsa_streamer PRIVATE vulkan_upsampler alsa_utils)

    if(ENABLE_TESTS)
        add_executable(alsa_common_smoke
            tests/cpp/test_alsa_common.cpp
        )
        target_link_libraries(alsa_common_smoke PRIVATE alsa_utils)
        add_test(NAME alsa_common_smoke COMMAND alsa_common_smoke)

        add_executable(alsa_filter_selector_smoke
            tests/cpp/test_alsa_filter_selector.cpp
        )
        target_link_libraries(alsa_filter_selector_smoke PRIVATE alsa_utils)
        add_test(NAME alsa_filter_selector_smoke COMMAND alsa_filter_selector_smoke)

        add_executable(alsa_streamer_e2e
            tests/cpp/test_alsa_streamer_e2e.cpp
        )
        add_test(NAME alsa_streamer_e2e COMMAND alsa_streamer_e2e)

        # Auto-negotiation tests (Issue #12)
        add_executable(auto_negotiation_smoke
            tests/cpp/audio/test_auto_negotiation.cpp
        )
        target_link_libraries(auto_negotiation_smoke PRIVATE auto_negotiation)
        add_test(NAME auto_negotiation_smoke COMMAND auto_negotiation_smoke)

        add_executable(audio_ring_buffer_smoke
            tests/cpp/audio/test_audio_ring_buffer.cpp
        )
        target_include_directories(audio_ring_buffer_smoke
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(NAME audio_ring_buffer_smoke COMMAND audio_ring_buffer_smoke)
    endif()
endif()

if(ENABLE_ZMQ)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBZMQ REQUIRED libzmq)

    add_library(zmq_command_server
        src/zmq/command_server.cpp
    )
    target_include_directories(zmq_command_server
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${LIBZMQ_INCLUDE_DIRS}
    )
    target_link_libraries(zmq_command_server PUBLIC ${LIBZMQ_LIBRARIES})

    add_executable(zmq_control_server
        src/zmq/zmq_server_main.cpp
    )
    target_link_libraries(zmq_control_server PRIVATE zmq_command_server)
    if(ENABLE_ALSA)
        target_link_libraries(zmq_control_server PRIVATE auto_negotiation)
    endif()

    if(ENABLE_TESTS)
        add_executable(zmq_server_e2e
            tests/cpp/test_zmq_server_e2e.cpp
        )
        target_include_directories(zmq_server_e2e PRIVATE ${LIBZMQ_INCLUDE_DIRS})
        target_link_libraries(zmq_server_e2e PRIVATE ${LIBZMQ_LIBRARIES})
        add_test(NAME zmq_server_e2e COMMAND zmq_server_e2e)
    endif()
endif()
