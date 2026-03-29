cmake_minimum_required(VERSION 3.16)

# Force GCC compiler
set(CMAKE_C_COMPILER gcc)

project(trender C)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_compile_definitions(RESOURCES_PATH="${CMAKE_SOURCE_DIR}/resources/")

# Platform-specific compiler flags
if(MSVC)
  # keep optimizations, but generate PDB and ask linker to emit debug info
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi /O2 /Qvec-report:2")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /Zi /O2 /Qvec-report:2")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG")
else()
  # GCC/Clang
  add_compile_options(-Wall -Wextra -pedantic -O3 -g)
  
  # Native optimization only for native builds (not cross-compiling)
  if(NOT CMAKE_CROSSCOMPILING)
    add_compile_options(-march=native -mtune=native)
  endif()
endif()

# Find threading library
# find_package(Threads REQUIRED)

add_executable(tio_input_test ${CMAKE_SOURCE_DIR}/src/tio_input_test.c)
if(UNIX)
  target_link_libraries(tio_input_test PRIVATE m rt)
else()
  target_link_libraries(tio_input_test PRIVATE)
endif()

add_executable(main ${CMAKE_SOURCE_DIR}/src/main.c)
if(UNIX)
  target_link_libraries(main PRIVATE m rt)
else()
  target_link_libraries(main PRIVATE)
endif()

add_executable(sixel_display_test ${CMAKE_SOURCE_DIR}/src/sixel_display_test.c)
if(UNIX)
  target_link_libraries(sixel_display_test PRIVATE m rt)
else()
  target_link_libraries(sixel_display_test PRIVATE)
endif()

add_executable(timer_test ${CMAKE_SOURCE_DIR}/src/timer_test.c)
if(UNIX)
  target_link_libraries(timer_test PRIVATE m rt)
else()
  target_link_libraries(timer_test PRIVATE)
endif()

# Generate assembly for main.c (only on GCC/Clang, not cross-compiling)
# if(NOT MSVC AND NOT CMAKE_CROSSCOMPILING)
#   add_custom_target(asm_main
#     COMMAND ${CMAKE_C_COMPILER} -Wall -Wextra -pedantic -O3 -g -march=native -mtune=native -S -fverbose-asm -o ${CMAKE_BINARY_DIR}/main.s ${CMAKE_SOURCE_DIR}/src/main.c
#     DEPENDS ${CMAKE_SOURCE_DIR}/src/main.c
#     COMMENT "Generating assembly: ${CMAKE_BINARY_DIR}/main.s"
#   )
# endif()

# Default build type if not set
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
