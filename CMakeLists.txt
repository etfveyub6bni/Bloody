cmake_minimum_required(VERSION 3.16)
project(CS2Prototype LANGUAGES C CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CS2P_AUDIO "Enable audio output (miniaudio)" ON)

# Static MSVC runtime: the Windows build runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

include(FetchContent)

# --- GLFW: window, input and OpenGL context only; everything else is our own engine code
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  # X11 works everywhere (XWayland included) and needs no wayland-scanner at build time.
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Build GLFW Wayland backend")
endif()
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
  URL_HASH SHA256=ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0)
FetchContent_MakeAvailable(glfw)

# --- miniaudio (single header, used only as a cross-platform audio device)
set(CS2P_MINIAUDIO_DIR ${CMAKE_BINARY_DIR}/_deps/miniaudio)
if(CS2P_AUDIO AND NOT EXISTS ${CS2P_MINIAUDIO_DIR}/miniaudio.h)
  file(DOWNLOAD https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h
       ${CS2P_MINIAUDIO_DIR}/miniaudio.h
       EXPECTED_HASH SHA256=ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89
       STATUS CS2P_MA_STATUS)
  list(GET CS2P_MA_STATUS 0 CS2P_MA_CODE)
  if(NOT CS2P_MA_CODE EQUAL 0)
    message(WARNING "miniaudio download failed - building without sound")
    file(REMOVE ${CS2P_MINIAUDIO_DIR}/miniaudio.h)
    set(CS2P_AUDIO OFF)
  endif()
endif()

# --- OpenGL 3.3 core loader (generated with glad2, vendored)
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

find_package(Threads REQUIRED)

file(GLOB_RECURSE CS2P_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(cs2proto ${CS2P_SOURCES})
target_include_directories(cs2proto PRIVATE src third_party/stb)
target_link_libraries(cs2proto PRIVATE glfw glad Threads::Threads)
target_compile_definitions(cs2proto PRIVATE CS2P_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(CS2P_AUDIO)
  target_include_directories(cs2proto PRIVATE ${CS2P_MINIAUDIO_DIR})
  target_compile_definitions(cs2proto PRIVATE CS2P_AUDIO=1)
  if(UNIX AND NOT APPLE)
    target_link_libraries(cs2proto PRIVATE ${CMAKE_DL_LIBS} m)
  endif()
endif()

if(MSVC)
  target_compile_options(cs2proto PRIVATE /W3 /utf-8 /permissive- /MP)
  target_compile_definitions(cs2proto PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT cs2proto)
  set_target_properties(cs2proto PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:cs2proto>)
else()
  target_compile_options(cs2proto PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
if(MINGW)
  target_link_options(cs2proto PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_custom_command(TARGET cs2proto POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:cs2proto>/assets)
