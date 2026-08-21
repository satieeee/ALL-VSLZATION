cmake_minimum_required(VERSION 3.15)
project(AudioVizGPU LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(AUDIO_SOURCES AudioCapture/AudioCapture.cpp)
set(AUDIO_HEADERS AudioCapture/AudioCapture.h AudioCapture/FFTHelper.h)

# CPU 效果源码（本地 VisualEffects 目录）
set(CPU_EFFECT_SOURCES
    VisualEffects/Psychedelic/Psychedelic.cpp
    VisualEffects/GlitchForge/GlitchForge.cpp
    VisualEffects/Verdigris/Verdigris.cpp
    VisualEffects/Azurite/Azurite.cpp
    VisualEffects/Azurite_c/Azurite_c.cpp
    VisualEffects/MTH/MTH.cpp
    VisualEffects/MTH_C/MTH_C.cpp
)

add_library(BPMDetector STATIC BPMdetector/BPMDetector.cpp BPMdetector/BPMDetector.h ${AUDIO_HEADERS})
target_include_directories(BPMDetector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(BPMDetector PUBLIC ole32 uuid)

add_executable(MAIN WIN32 main.cpp ${AUDIO_SOURCES} ${AUDIO_HEADERS} ${CPU_EFFECT_SOURCES})
target_link_libraries(MAIN BPMDetector ole32 uuid gdi32 user32 comctl32 opengl32)
target_include_directories(MAIN PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/VisualEffects)
target_compile_definitions(MAIN PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(MAIN PROPERTIES OUTPUT_NAME "MAIN")
