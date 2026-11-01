cmake_minimum_required(VERSION 3.25)
project(NagareWindows LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_program(FXC fxc.exe REQUIRED)

set(SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glass.hlsl)
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED})

function(compile_shader entry profile header)
    add_custom_command(
        OUTPUT ${GENERATED}/${header}.h
        COMMAND ${FXC} /nologo /O3 /T ${profile} /E ${entry} /Vn g_${entry} /Fh ${GENERATED}/${header}.h ${SHADER}
        DEPENDS ${SHADER}
        VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${GENERATED}/${header}.h PARENT_SCOPE)
endfunction()

compile_shader(vsQuad vs_4_0 vs_quad)
compile_shader(psDown ps_4_0 ps_down)
compile_shader(psBlur ps_4_0 ps_blur)
compile_shader(psGlass ps_4_0 ps_glass)

# Everything the app draws lives inside the executable. A resource name is its path below assets/ with
# each punctuation mark as an underscore, because rc.exe would keep any quotes as part of the name.
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/assets assets/*.jpg assets/*.svg assets/*.ttf)
set(ASSET_RC "")
foreach(ASSET ${ASSET_FILES})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" ASSET_NAME ${ASSET})
    string(APPEND ASSET_RC "${ASSET_NAME} RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/assets/${ASSET}\"\n")
    list(APPEND ASSET_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${ASSET})
endforeach()
file(CONFIGURE OUTPUT ${GENERATED}/assets.rc CONTENT "${ASSET_RC}")
set_source_files_properties(${GENERATED}/assets.rc PROPERTIES OBJECT_DEPENDS "${ASSET_PATHS}")
set_source_files_properties(resources/nagare.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/nagare.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/nagare.manifest;${CMAKE_CURRENT_SOURCE_DIR}/resources/version.h")

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp src/ui/*.cpp src/gfx/renderer.cpp)
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp)

set(WIN_LIBS d3d11 dxgi d2d1 dwrite dxguid imm32 windowscodecs dwmapi shlwapi ole32 shell32 user32 gdi32 advapi32 uxtheme comctl32 comdlg32)
set(WIN_DEFS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
set(WIN_FLAGS /W4 /utf-8 /permissive- /EHsc)

# libtransmission and everything it needs, built once by dev/remote-engine.ps1.
set(NAGARE_ENGINE_ROOT "C:/dev" CACHE PATH "Folder holding nagare-src, engine-build and vcpkg")
set(ENGINE_SRC ${NAGARE_ENGINE_ROOT}/nagare-src)
set(ENGINE_BUILD ${NAGARE_ENGINE_ROOT}/engine-fast CACHE PATH "Folder the engine was built in")
set(ENGINE_VCPKG ${NAGARE_ENGINE_ROOT}/vcpkg/installed/x64-windows-static/lib)
set(ENGINE_THIRD ${ENGINE_BUILD}/third-party)

set(ENGINE_LIBS
    ${ENGINE_BUILD}/libtransmission/transmission.lib
    ${ENGINE_THIRD}/dht.bld/pfx/lib/dht.lib
    ${ENGINE_THIRD}/libb64.bld/src/b64.lib
    ${ENGINE_THIRD}/libdeflate.bld/pfx/lib/deflatestatic.lib
    ${ENGINE_THIRD}/libevent.bld/pfx/lib/event.lib
    ${ENGINE_THIRD}/libnatpmp.bld/pfx/lib/natpmp.lib
    ${ENGINE_THIRD}/libpsl.bld/pfx/lib/psl.lib
    ${ENGINE_THIRD}/libutp.bld/utp.lib
    ${ENGINE_THIRD}/madler-crcany/MadlerCrcany.lib
    ${ENGINE_THIRD}/miniupnp/miniupnpc.bld/pfx/lib/libminiupnpc.lib
    ${ENGINE_THIRD}/wildmat/wildmat.lib
    ${ENGINE_VCPKG}/libcurl.lib
    ${ENGINE_VCPKG}/mbedtls.lib
    ${ENGINE_VCPKG}/mbedx509.lib
    ${ENGINE_VCPKG}/mbedcrypto.lib
    ${ENGINE_VCPKG}/everest.lib
    ${ENGINE_VCPKG}/p256m.lib
    ${ENGINE_VCPKG}/zs.lib
    ws2_32 iphlpapi crypt32 bcrypt secur32 wldap32 winhttp delayimp)

# The engine compiles apart so that libtransmission's headers and flags stay out of the UI.
# These definitions must match the library's own build, or its inline functions would differ.
add_library(nagare_engine OBJECT ${ENGINE_SOURCES})
target_include_directories(nagare_engine PRIVATE src ${ENGINE_SRC} ${ENGINE_BUILD})
target_include_directories(nagare_engine SYSTEM PRIVATE
    ${ENGINE_SRC}/third-party/fmt/include
    ${ENGINE_SRC}/third-party/small/include
    ${ENGINE_SRC}/third-party/rapidjson/include
    ${ENGINE_SRC}/third-party/utfcpp/source
    ${ENGINE_SRC}/third-party/fast_float/include
    ${ENGINE_SRC}/third-party/wide-integer
    ${ENGINE_THIRD}/libevent.bld/pfx/include
    ${NAGARE_ENGINE_ROOT}/vcpkg/installed/x64-windows-static/include)
target_compile_definitions(nagare_engine PRIVATE ${WIN_DEFS}
    FMT_HEADER_ONLY=1 FMT_USE_EXCEPTIONS=0 SMALL_DISABLE_EXCEPTIONS=1 RAPIDJSON_HAS_STDSTRING=1 WITH_MBEDTLS WITH_UTP)
target_compile_options(nagare_engine PRIVATE ${WIN_FLAGS})

# A console engine with no UI, for comparing engine settings and build flags. Built only on request.
add_executable(nagare_enginebench EXCLUDE_FROM_ALL dev/enginebench/main.cpp)
target_include_directories(nagare_enginebench PRIVATE ${ENGINE_SRC} ${ENGINE_BUILD})
target_include_directories(nagare_enginebench SYSTEM PRIVATE
    ${ENGINE_SRC}/third-party/fmt/include
    ${ENGINE_SRC}/third-party/small/include
    ${ENGINE_SRC}/third-party/rapidjson/include
    ${ENGINE_SRC}/third-party/utfcpp/source
    ${ENGINE_SRC}/third-party/fast_float/include
    ${ENGINE_SRC}/third-party/wide-integer
    ${ENGINE_THIRD}/libevent.bld/pfx/include)
target_compile_definitions(nagare_enginebench PRIVATE ${WIN_DEFS}
    FMT_HEADER_ONLY=1 FMT_USE_EXCEPTIONS=0 SMALL_DISABLE_EXCEPTIONS=1 RAPIDJSON_HAS_STDSTRING=1 WITH_MBEDTLS WITH_UTP)
target_compile_options(nagare_enginebench PRIVATE ${WIN_FLAGS})
target_link_libraries(nagare_enginebench PRIVATE ${ENGINE_LIBS} shell32 ole32 advapi32 user32)

add_executable(nagare WIN32 ${APP_SOURCES} ${SHADER_HEADERS} resources/nagare.rc ${GENERATED}/assets.rc $<TARGET_OBJECTS:nagare_engine>)
target_include_directories(nagare PRIVATE ${GENERATED} src resources)
target_compile_definitions(nagare PRIVATE ${WIN_DEFS})
target_compile_options(nagare PRIVATE ${WIN_FLAGS})
# Search is the only thing that uses WinHTTP, so it doesn't load until someone searches.
# resources/nagare.rc carries the manifest, so the linker must not write one of its own.
target_link_options(nagare PRIVATE /OPT:REF /OPT:ICF /DELAYLOAD:winhttp.dll /MANIFEST:NO)
target_link_libraries(nagare PRIVATE ${WIN_LIBS} ${ENGINE_LIBS})
