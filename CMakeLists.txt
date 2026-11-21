cmake_minimum_required(VERSION 3.15)
project(AudioVizGPU LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(AUDIO_SOURCES AudioCapture/AudioCapture.cpp AudioCapture/AudioAnalyzer.cpp)
set(AUDIO_HEADERS AudioCapture/AudioCapture.h AudioCapture/FFTHelper.h AudioCapture/AudioAnalyzer.h)

# GPU 效果源码：从 main.cpp 搬出来的各个效果的渲染器。
# 【为什么要单独一组】效果以前全堆在 main.cpp 里（8000 行），改一个效果要在里面找半天。
# 搬出来一个就往这里加一行 —— 渲染器是各自独立的模块，只有 main.cpp 的调用点还连着。
set(GPU_EFFECT_SOURCES
    VisualEffects/TN/TNRender.cpp      # Filigree（隧道 + 金属细条 + 内壁剪纸）
    VisualEffects/XS/XSRender.cpp      # Shardstorm（碎片风暴）
    # B376 的 CPU 侧场解码/位移与 Metalheart1 的 GPU 网格共用（main.cpp 每帧调 SetFlowAmp）
    VisualEffects/B376/B376.cpp
    # 跨效果过渡（引擎侧统一处理；效果 shader 里不再各写一份）
    VisualEffects/Transition/TransitionFx.cpp
)

add_library(BPMDetector STATIC BPMdetector/BPMDetector.cpp BPMdetector/BPMDetector.h ${AUDIO_HEADERS})
target_include_directories(BPMDetector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(BPMDetector PUBLIC ole32 uuid)

add_executable(MAIN WIN32 main.cpp ${AUDIO_SOURCES} ${AUDIO_HEADERS} ${GPU_EFFECT_SOURCES})
target_link_libraries(MAIN BPMDetector ole32 uuid gdi32 gdiplus user32 comctl32 opengl32)
target_include_directories(MAIN PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/VisualEffects)
target_compile_definitions(MAIN PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(MAIN PROPERTIES OUTPUT_NAME "MAIN")
