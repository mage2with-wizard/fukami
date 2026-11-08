set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(${CMAKE_SOURCE_DIR}/cmake/RrvToolchainFlags.cmake)
set(RRV_PS2RECOMP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/ps2recomp)
set(RRV_RT ${RRV_PS2RECOMP_SOURCE_DIR}/ps2xRuntime)
set(RRV_GENERATED_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/generated/rr5)
set(PS2X_RRV_EXTENSION_SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(PS2X_RRV_FIELD_ONLY ON)
set(X ${CMAKE_SOURCE_DIR}/jp-experiment)
# Local header/library locations (extracted -dev packages, the installed Fukami's SDL2,
# and a sparse checkout of PCSX2 fd9d310c 3rdparty/libchdr + lzma). Override per machine.
set(RRV_JP_DEV_PREFIX "/tmp/fukami-jp-dev" CACHE PATH "Prefix holding extracted -dev packages (usr/include, usr/lib)")
set(RRV_JP_SDL2_LIBRARY "$ENV{HOME}/Applications/Fukami/lib/libSDL2-2.0.so.0" CACHE FILEPATH "SDL2 shared library to link")
set(RRV_JP_CHDR_SOURCE "${RRV_JP_DEV_PREFIX}/pcsx2-sparse/3rdparty" CACHE PATH "PCSX2 2.8.2 3rdparty dir with libchdr and lzma")
add_compile_options(-march=x86-64-v3 -ffunction-sections -fdata-sections)
add_compile_definitions(PS2X_RRV_FIELD_ONLY=1 RRV_PRODUCT_HOST_SDL=1 RRV_GS_FIELD_ONLY=1)
include_directories(${X} ${RRV_RT}/include ${RRV_RT}/src/lib/Kernel ${RRV_RT}/src/lib/Kernel/Syscalls
 ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/guest-time ${CMAKE_SOURCE_DIR}/src/gs-backend
 ${CMAKE_SOURCE_DIR}/src/gs-record ${CMAKE_SOURCE_DIR}/src/snapshot ${CMAKE_SOURCE_DIR}/src/diag
 ${CMAKE_SOURCE_DIR}/src/host ${CMAKE_SOURCE_DIR}/src/vu-aot/compat ${CMAKE_SOURCE_DIR}/src/hle
 ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/generated/rr5/output
 ${RRV_JP_DEV_PREFIX}/usr/include/SDL2 ${RRV_JP_DEV_PREFIX}/usr/include ${RRV_JP_DEV_PREFIX}/usr/include/x86_64-linux-gnu)
add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION ${RRV_JP_SDL2_LIBRARY})
add_library(rrv_sdl_product_audio STATIC src/host/rrv_sdl_audio.cpp src/host/rrv_thread_cpu_log.cpp)
target_link_libraries(rrv_sdl_product_audio PUBLIC SDL2::SDL2)
add_subdirectory(cmake/product-runtime runtime)
file(GLOB HLE src/hle/*.cpp)
file(GLOB SNAP src/snapshot/*.cpp)
file(GLOB GAME generated/rr5/output/sub_*.cpp)
add_library(game STATIC ${GAME} generated/rr5/output/register_functions.cpp)
set_target_properties(game PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 30)
target_link_libraries(game PUBLIC ps2_runtime)
add_library(support STATIC ${HLE} ${SNAP} src/gs-record/rrv_gs_record.cpp
 src/gs-backend/rrv_gs_backend.cpp src/host/rrv_resource_package.cpp ${X}/binding.cpp
 src/host/rrv_sdl_presentation.cpp src/app/fukami_menu.cpp src/app/fukami_settings.cpp
 ${X}/app_linux_jp.cpp src/app/fukami_app_common.cpp)
# First-launch disc extraction (CHD / CUE / BIN) for the normal-play launcher.
enable_language(C)
add_library(jp-disc-chdr STATIC
 ${RRV_JP_CHDR_SOURCE}/libchdr/src/libchdr_bitstream.c ${RRV_JP_CHDR_SOURCE}/libchdr/src/libchdr_cdrom.c
 ${RRV_JP_CHDR_SOURCE}/libchdr/src/libchdr_chd.c ${RRV_JP_CHDR_SOURCE}/libchdr/src/libchdr_flac.c
 ${RRV_JP_CHDR_SOURCE}/libchdr/src/libchdr_huffman.c ${RRV_JP_CHDR_SOURCE}/lzma/src/LzmaDec.c
 ${RRV_JP_CHDR_SOURCE}/lzma/src/Alloc.c ${RRV_JP_CHDR_SOURCE}/lzma/src/CpuArch.c)
target_compile_definitions(jp-disc-chdr PRIVATE _7ZIP_ST)
target_compile_options(jp-disc-chdr PRIVATE -w)
target_include_directories(jp-disc-chdr PUBLIC ${RRV_JP_CHDR_SOURCE}/libchdr/include)
target_include_directories(jp-disc-chdr PRIVATE ${RRV_JP_CHDR_SOURCE}/lzma/include)
target_link_libraries(jp-disc-chdr PUBLIC ${RRV_JP_DEV_PREFIX}/usr/lib/x86_64-linux-gnu/libzstd.a
 ${RRV_JP_DEV_PREFIX}/usr/lib/x86_64-linux-gnu/libz.a)
add_library(jp-disc-extract STATIC src/app/disc_extract.cpp)
target_link_libraries(jp-disc-extract PUBLIC jp-disc-chdr)
target_link_libraries(support PUBLIC ps2_runtime SDL2::SDL2 jp-disc-extract dl pthread)
add_executable(Fukami-JP ${X}/main_jp.cpp ${X}/patches_jp.cpp)
set_target_properties(Fukami-JP PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(Fukami-JP PRIVATE RRV_GAME_ELF_SHA256="4532fb27202f6259c37d1e0d9f9d02f59f9479da29f0ac7d95e04bf5b5d759fa")
foreach(k SOURCE_COMMIT SOURCE_CLEAN PRODUCER_NAME PRODUCER_COMMIT PRODUCER_TREE PRODUCER_BASE PCSX2_COMMIT)
 target_compile_definitions(Fukami-JP PRIVATE RRV_${k}="experimental-jp")
endforeach()
target_link_libraries(Fukami-JP PRIVATE -Wl,--start-group game support ps2_runtime rrv_sdl_product_audio -Wl,--end-group)
target_link_options(Fukami-JP PRIVATE -Wl,--gc-sections)
set_target_properties(Fukami-JP PROPERTIES OUTPUT_NAME Fukami)
target_compile_definitions(Fukami-JP PRIVATE RRV_PRODUCT_DIRECT_PRESENT=1)
# JP audio experiment: use the same SPU2 core and RSPU2 host driver as v1.1.0.
set(RRV_PCSX2_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build-deps/pcsx2-2.8.2" CACHE PATH "Pinned PCSX2 SPU2 source")
set(RRV_SPU2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RRV_SPU2_TSAN_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/pcsx2-spu2 spu2)
target_sources(Fukami-JP PRIVATE src/audio/rspu2_driver.cpp src/audio/rspu2_glue.cpp)
target_compile_definitions(Fukami-JP PRIVATE RRV_GATE8_SPU2=1)
target_link_libraries(Fukami-JP PRIVATE rrv_pcsx2_spu2)
