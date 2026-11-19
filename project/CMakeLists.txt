cmake_minimum_required(VERSION 3.20)
project(GOW3Recomp CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(RECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# -DGOW3_TU=ON: version con la actualizacion del titulo (ppc_tu/, GOW3TU.exe)
option(GOW3_TU "Version con la actualizacion del titulo (v0x403)" OFF)
if(GOW3_TU)
  set(PPC_DIR ${RECOMP_ROOT}/ppc_tu)
  set(GOW3_OUT GOW3TU)
else()
  set(PPC_DIR ${RECOMP_ROOT}/ppc)
  set(GOW3_OUT GOW3)
endif()
file(GLOB PPC_SOURCES CONFIGURE_DEPENDS ${PPC_DIR}/ppc_recomp.*.cpp ${PPC_DIR}/ppc_func_mapping.cpp)

# Codigo PPC traducido (enorme): libreria aparte con flags propias
add_library(GOW3RecompLib STATIC ${PPC_SOURCES})
target_include_directories(GOW3RecompLib PUBLIC ${PPC_DIR} ${RECOMP_ROOT}/XenonRecomp/thirdparty/simde)
target_compile_options(GOW3RecompLib PRIVATE /O1 -fno-strict-aliasing -msse4.1 -Wno-everything -gline-tables-only /Z7 "/FI${RECOMP_ROOT}/compat/ppc_indirect.h")

set(FFMPEG "G:/Guitar Hero Warrior of rock/Recomp/toolchain/ffmpeg")
set(RT ${RECOMP_ROOT}/runtime)
add_executable(GOW3 ${RT}/main.cpp ${RT}/kernel.cpp ${RT}/kernel_gow3.cpp ${RT}/hooks_gow3.cpp ${RT}/fibers.cpp ${RT}/import_stubs.cpp ${RT}/threads.cpp ${RT}/fs.cpp ${RT}/gpu_stub.cpp ${RT}/diag.cpp
    ${RT}/gpu_cp.cpp ${RT}/gpu_window.cpp ${RT}/atomics.cpp ${RT}/xam.cpp ${RT}/audio_wasapi.cpp ${RT}/launcher.cpp ${RT}/launcher_gears.cpp ${RT}/updater.cpp ${RT}/net_gow3.cpp ${RT}/xexp_patch.cpp ${RT}/dualsense.cpp
    ${RT}/gpu_d3d12.cpp ${RT}/gpu_shader.cpp ${RT}/gpu_shadertest.cpp ${RT}/xma.cpp ${RT}/texdirty.cpp ${RT}/port_settings.cpp ${RT}/input_mouse.cpp ${RT}/gow3.rc)
target_include_directories(GOW3 PRIVATE ${RT} ${FFMPEG}/include)
target_compile_options(GOW3 PRIVATE /O2 -fno-strict-aliasing -msse4.1 -Wno-unused-parameter /Z7)
target_compile_definitions(GOW3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN $<$<BOOL:${GOW3_TU}>:GOW3_TU>)
set_target_properties(GOW3 PROPERTIES OUTPUT_NAME ${GOW3_OUT})
target_link_libraries(GOW3 PRIVATE GOW3RecompLib user32 gdi32 d3d12 dxgi d3dcompiler winmm ole32 xinput bcrypt comdlg32 hid setupapi comctl32 ws2_32 dwmapi uxtheme shell32 windowscodecs msimg32 psapi winhttp ${FFMPEG}/lib/avcodec.lib ${FFMPEG}/lib/avutil.lib)
add_custom_command(TARGET GOW3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG}/bin/avcodec-62.dll ${FFMPEG}/bin/avutil-60.dll ${FFMPEG}/bin/swresample-6.dll $<TARGET_FILE_DIR:GOW3>)
target_link_options(GOW3 PRIVATE /DEBUG /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)   # sin ventana de consola; el registro va a gow3.log
