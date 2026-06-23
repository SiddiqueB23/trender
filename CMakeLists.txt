cmake_minimum_required(VERSION 3.16)

# Force GCC compiler
# set(CMAKE_C_COMPILER gcc)

project(trender C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_compile_definitions(RESOURCES_PATH="${CMAKE_SOURCE_DIR}/resources/")
 
# Platform-specific compiler flags
if(MSVC)
  # keep optimizations, but generate PDB and ask linker to emit debug info
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi /O2 /arch:AVX2")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /Zi /O2 /arch:AVX2")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG")
else()
  # GCC/Clang
  add_compile_options(-Wall -Wextra -Wno-unknown-pragmas -pedantic -g -O3 -march=native -mtune=native -pthread)
  
  # Native optimization only for native builds (not cross-compiling)
  if(NOT CMAKE_CROSSCOMPILING)
    add_compile_options(-march=native -mtune=native)
  endif()
endif()

# Find threading library
# find_package(Threads REQUIRED)

if(APPLE)
  set(RT_LIB "")
else()
  set(RT_LIB rt)
endif()

add_executable(tio_input_test ${CMAKE_SOURCE_DIR}/src/tio_input_test.c)
if(UNIX)
  target_link_libraries(tio_input_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(tio_input_test PRIVATE)
endif()

add_executable(tio_window_size_test ${CMAKE_SOURCE_DIR}/src/tio_window_size_test.c)
if(UNIX)
  target_link_libraries(tio_window_size_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(tio_window_size_test PRIVATE)
endif()

add_executable(trender ${CMAKE_SOURCE_DIR}/src/main.c)
if(UNIX)
  target_link_libraries(trender PRIVATE m ${RT_LIB})
else()
  target_link_libraries(trender PRIVATE)
endif()

add_executable(sixel_display_test ${CMAKE_SOURCE_DIR}/src/sixel_display_test.c)
if(UNIX)
  target_link_libraries(sixel_display_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(sixel_display_test PRIVATE)
endif()

add_executable(halfblock_display_test ${CMAKE_SOURCE_DIR}/src/halfblock_display_test.c)
if(UNIX)
  target_link_libraries(halfblock_display_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(halfblock_display_test PRIVATE)
endif()

add_executable(tio_gfx_sixel_test ${CMAKE_SOURCE_DIR}/src/tio_gfx_sixel_test.c)
if(UNIX)
  target_link_libraries(tio_gfx_sixel_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(tio_gfx_sixel_test PRIVATE)
endif()

add_executable(tio_gfx_halfblock_test ${CMAKE_SOURCE_DIR}/src/tio_gfx_halfblock_test.c)
if(UNIX)
  target_link_libraries(tio_gfx_halfblock_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(tio_gfx_halfblock_test PRIVATE)
endif()

add_executable(timer_test ${CMAKE_SOURCE_DIR}/src/timer_test.c)
if(UNIX)
  target_link_libraries(timer_test PRIVATE m ${RT_LIB})
else()
  target_link_libraries(timer_test PRIVATE)
endif()

add_executable(tio_trace ${CMAKE_SOURCE_DIR}/src/tio_trace.c)
if(UNIX)
  target_link_libraries(tio_trace PRIVATE m ${RT_LIB})
else()
  target_link_libraries(tio_trace PRIVATE)
endif()

# Default build type if not set
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
