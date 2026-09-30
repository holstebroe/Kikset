cmake_minimum_required(VERSION 3.20)
project(Kikset VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

include(cmake/EmbedResources.cmake)
include(cmake/DeployClap.cmake)
include(CTest)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/clap/include/clap/clap.h")
    message(FATAL_ERROR "CLAP headers missing: run `git submodule update --init`")
endif()

# ---- embedded clicks
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_DIR})
kikset_embed_clicks("${GEN_DIR}/ClickData.cpp" "${CMAKE_SOURCE_DIR}/resources/clicks")

# ---- core DSP (no CLAP / GUI includes)
add_library(kikset_core STATIC
    src/core/KickPhase.cpp
    src/core/Saturator.cpp
    src/core/BeatPlan.cpp
    src/core/Voices.cpp
    src/core/ClickBank.cpp
    src/core/KiksetEngine.cpp
    ${GEN_DIR}/ClickData.cpp)
target_include_directories(kikset_core PUBLIC src/core)
if(MSVC)
    target_compile_options(kikset_core PUBLIC /W4 /fp:precise)
else()
    target_compile_options(kikset_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- GUI (portable panel; OS window code is optional)
add_library(kikset_gui STATIC src/gui/Graphics.cpp src/gui/Panel.cpp)
target_link_libraries(kikset_gui PUBLIC kikset_core)
find_package(X11)
if(X11_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(kikset_gui PRIVATE src/gui/X11Window.cpp)
    target_include_directories(kikset_gui PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(kikset_gui PUBLIC ${X11_LIBRARIES})
    target_compile_definitions(kikset_gui PUBLIC KIKSET_HAS_X11=1)
    add_executable(kikset_gui_demo src/tools/gui_demo_main.cpp)
    target_link_libraries(kikset_gui_demo PRIVATE kikset_gui)
endif()

# ---- CLAP plugin
add_library(kikset_clap MODULE src/clap/KiksetClap.cpp)
target_link_libraries(kikset_clap PRIVATE kikset_core kikset_gui)
target_include_directories(kikset_clap PRIVATE clap/include)
set_target_properties(kikset_clap PROPERTIES PREFIX "" OUTPUT_NAME "Kikset" SUFFIX ".clap")
if(NOT MSVC)
    set_target_properties(kikset_clap PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endif()
kikset_deploy_clap(kikset_clap)

# ---- tools
add_executable(kikset_render src/tools/render_main.cpp)
target_link_libraries(kikset_render PRIVATE kikset_core)

# ---- tests (plain executables registered with CTest)
foreach(T anchor_test dsp_test robustness_test perf_bench gui_test)
    add_executable(${T} src/tests/${T}_main.cpp)
    target_link_libraries(${T} PRIVATE kikset_core kikset_gui)
endforeach()
add_test(NAME anchor COMMAND anchor_test)
add_test(NAME dsp COMMAND dsp_test)
add_test(NAME robustness COMMAND robustness_test)
add_test(NAME perf COMMAND perf_bench)
add_test(NAME gui COMMAND gui_test ${CMAKE_BINARY_DIR})

# ---- plugin smoke test: dlopen the built .clap like a minimal host
add_executable(clap_smoke src/tests/clap_smoke_main.cpp)
target_include_directories(clap_smoke PRIVATE clap/include)
target_link_libraries(clap_smoke PRIVATE kikset_gui ${CMAKE_DL_LIBS})
add_dependencies(clap_smoke kikset_clap)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME clap_smoke COMMAND ${XVFB_RUN} -a $<TARGET_FILE:clap_smoke> $<TARGET_FILE:kikset_clap>)
    else()
        add_test(NAME clap_smoke COMMAND clap_smoke $<TARGET_FILE:kikset_clap>)
    endif()
endif()
