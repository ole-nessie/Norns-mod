# Desktop simulator (macOS / Linux). The bare-metal Pi build lives in src/pi.
cmake_minimum_required(VERSION 3.20)
project(drumbox CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
cmake_policy(SET CMP0135 NEW)

# Header-only use: SOURCE_SUBDIR points nowhere so miniaudio's own CMake project is not built.
FetchContent_Declare(miniaudio
  URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(miniaudio)

set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rtmidi
  URL https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.tar.gz)
FetchContent_MakeAvailable(rtmidi)

add_library(drumbox_core STATIC
  src/core/midi.cpp
  src/core/synth.cpp
  src/core/efm_kick.cpp
  src/core/sid_voice.cpp
  src/core/ui/canvas.cpp
  src/core/ui/param_page.cpp
)
target_include_directories(drumbox_core PUBLIC src/core third_party)
target_compile_options(drumbox_core PRIVATE -Wall -Wextra)

add_executable(drumbox
  src/desktop/main.cpp
  src/desktop/audio.cpp
  src/desktop/midi_input.cpp
  src/desktop/terminal_display.cpp
  src/desktop/keyboard.cpp
)
target_include_directories(drumbox PRIVATE src/desktop ${miniaudio_SOURCE_DIR})
target_link_libraries(drumbox PRIVATE drumbox_core rtmidi)

find_package(Threads REQUIRED)
target_link_libraries(drumbox PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(drumbox PRIVATE m)
endif()
if(APPLE)
  target_link_libraries(drumbox PRIVATE
    "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

target_compile_options(drumbox PRIVATE -Wall -Wextra)
