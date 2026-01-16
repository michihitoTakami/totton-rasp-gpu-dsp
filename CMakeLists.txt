cmake_minimum_required(VERSION 3.20)
project(totton_rasp_gpu_dsp LANGUAGES CXX)

option(ENABLE_VULKAN "Enable Vulkan upsampler" ON)
option(USE_VKFFT "Use VkFFT backend when Vulkan is enabled" ON)
option(ENABLE_ALSA "Enable ALSA streaming app" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(vulkan_upsampler
    src/vulkan/vulkan_streaming_upsampler.cpp
)

target_include_directories(vulkan_upsampler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vulkan_upsampler PUBLIC cxx_std_17)

enable_testing()

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
        FetchContent_MakeAvailable(vkfft)
        target_compile_definitions(vulkan_upsampler PRIVATE USE_VKFFT=1 VKFFT_BACKEND=0)
        target_include_directories(vulkan_upsampler PRIVATE ${vkfft_SOURCE_DIR})
    endif()
endif()

add_executable(upsampler_smoke
    tests/cpp/test_vulkan_upsampler.cpp
)
target_link_libraries(upsampler_smoke PRIVATE vulkan_upsampler)
add_test(NAME upsampler_smoke COMMAND upsampler_smoke)

if(ENABLE_VULKAN)
    target_compile_definitions(upsampler_smoke PRIVATE ENABLE_VULKAN=1)
    target_link_libraries(upsampler_smoke PRIVATE Vulkan::Vulkan)
endif()

if(ENABLE_ALSA)
    find_package(ALSA REQUIRED)
    add_executable(alsa_streamer
        src/alsa/alsa_common.cpp
        src/alsa/alsa_filter_selector.cpp
        src/alsa/alsa_streamer_main.cpp
    )
    target_link_libraries(alsa_streamer PRIVATE vulkan_upsampler ALSA::ALSA)
endif()
