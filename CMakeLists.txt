cmake_minimum_required(VERSION 3.15)
project(QuickGroup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # статическая среда выполнения: exe запускается без доп. библиотек
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /W3)
endif()

add_executable(QuickGroup WIN32
  src/main.cpp
  src/layout_map.cpp
)

target_link_libraries(QuickGroup PRIVATE user32 gdi32 shell32 advapi32 winmm)

if(MINGW)
  target_link_options(QuickGroup PRIVATE -static -static-libgcc -static-libstdc++)
endif()
