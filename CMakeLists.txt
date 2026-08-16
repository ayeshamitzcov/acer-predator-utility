cmake_minimum_required(VERSION 3.20)
project(PredatorUtility VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

set(PREDATOR_COMPILE_DEFS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

set(PREDATOR_CORE_SOURCES
    src/common/Log.cpp
    src/common/Settings.cpp
    src/acer/WmiSession.cpp
    src/acer/GamingWmi.cpp
    src/acer/BatteryHealth.cpp
    src/acer/AcerService.cpp
    src/acer/RgbKb.cpp
    src/acer/KillPredatorSense.cpp
    src/sensors/NvmlGpu.cpp
    src/sensors/NvapiOc.cpp
    src/sensors/PawnIo.cpp
    src/sensors/Sensors.cpp
    src/power/PowerPlans.cpp
    src/power/DiscreteGpu.cpp
    src/display/DisplayMode.cpp
    src/probe/Probe.cpp
)

add_library(predator_core STATIC ${PREDATOR_CORE_SOURCES})
target_include_directories(predator_core PUBLIC src)
target_compile_definitions(predator_core PUBLIC ${PREDATOR_COMPILE_DEFS})
target_link_libraries(predator_core PUBLIC
    ole32
    oleaut32
    wbemuuid
    pdh
    powrprof
    setupapi
    cfgmgr32
    ws2_32
    iphlpapi
    dxgi
    advapi32
)

add_executable(predator-probe src/probe/main.cpp resources/app.rc)
target_link_libraries(predator-probe PRIVATE predator_core)
target_compile_definitions(predator-probe PRIVATE ${PREDATOR_COMPILE_DEFS})

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(PredatorUtility WIN32
    src/main.cpp
    src/ui/App.cpp
    src/ui/Overlay.cpp
    src/ui/Tray.cpp
    src/ui/Hotkeys.cpp
    resources/app.rc
    ${IMGUI_SOURCES}
)
target_include_directories(PredatorUtility PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(PredatorUtility PRIVATE
    predator_core
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    comctl32
    gdi32
    user32
    shell32
    advapi32
)
target_compile_definitions(PredatorUtility PRIVATE ${PREDATOR_COMPILE_DEFS})

if(MSVC)
    target_compile_options(predator_core PRIVATE /utf-8 /W3)
    target_compile_options(predator-probe PRIVATE /utf-8 /W3)
    target_compile_options(PredatorUtility PRIVATE /utf-8 /W3)
    target_link_options(PredatorUtility PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
    target_link_options(predator-probe PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
endif()

set(PAWNIO_DIR "${CMAKE_SOURCE_DIR}/third_party/pawnio")
foreach(tgt PredatorUtility predator-probe)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAWNIO_DIR}/PawnIOLib.dll"
            "$<TARGET_FILE_DIR:${tgt}>/PawnIOLib.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAWNIO_DIR}/IntelMSR.bin"
            "$<TARGET_FILE_DIR:${tgt}>/IntelMSR.bin"
    )
endforeach()
