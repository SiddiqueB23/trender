cmake_minimum_required(VERSION 3.16)

# Force GCC compiler
# set(CMAKE_C_COMPILER gcc)

project(trender C)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_compile_definitions(RESOURCES_PATH="${CMAKE_SOURCE_DIR}/resources/")
 
# Platform-specific compiler flags
if(MSVC)
  # keep optimizations, but generate PDB and ask linker to emit debug info
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi /O2 /Qvec-report:2 /arch:AVX2 /openmp:llvm")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /Zi /O2 /Qvec-report:2 /arch:AVX2 /openmp:llvm")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /openmp:llvm")
  set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG /openmp:llvm")
else()
  # GCC/Clang
  add_compile_options(-Wall -Wextra -pedantic -g -O3 -march=native -mtune=native -pthread)
  
  # Native optimization only for native builds (not cross-compiling)
  if(NOT CMAKE_CROSSCOMPILING)
    add_compile_options(-march=native -mtune=native)
  endif()
endif()

# Prefer CMake's OpenMP handling so link flags are added correctly
if(APPLE)
  execute_process(COMMAND brew --prefix libomp OUTPUT_VARIABLE LIBOMP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
  include_directories(${LIBOMP_PREFIX}/include)
  link_directories(${LIBOMP_PREFIX}/lib)
  set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp")
  set(OpenMP_C_LIB_NAMES "omp")
  set(OpenMP_omp_LIBRARY ${LIBOMP_PREFIX}/lib/libomp.dylib)
#  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
 # add_link_options(-fsanitize=address)
endif()
find_package(OpenMP QUIET)
set(EXTRA_LIBS "")
if(OpenMP_C_FOUND)
  set(EXTRA_LIBS OpenMP::OpenMP_C)
else()
  message(STATUS "OpenMP not found by CMake; relying on -fopenmp compile flags")
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
  target_link_libraries(tio_input_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(tio_input_test PRIVATE ${EXTRA_LIBS})
endif()

add_executable(trender ${CMAKE_SOURCE_DIR}/src/main.c)
if(UNIX)
  target_link_libraries(trender PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(trender PRIVATE ${EXTRA_LIBS})
endif()

add_executable(sixel_display_test ${CMAKE_SOURCE_DIR}/src/sixel_display_test.c)
if(UNIX)
  target_link_libraries(sixel_display_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(sixel_display_test PRIVATE ${EXTRA_LIBS})
endif()

add_executable(tio_gfx_sixel_test ${CMAKE_SOURCE_DIR}/src/tio_gfx_sixel_test.c)
if(UNIX)
  target_link_libraries(tio_gfx_sixel_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(tio_gfx_sixel_test PRIVATE ${EXTRA_LIBS})
endif()

add_executable(timer_test ${CMAKE_SOURCE_DIR}/src/timer_test.c)
if(UNIX)
  target_link_libraries(timer_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(timer_test PRIVATE ${EXTRA_LIBS})
endif()

add_executable(triangle_drawing_test ${CMAKE_SOURCE_DIR}/src/triangle_drawing_test.c)
if(UNIX)
  target_link_libraries(triangle_drawing_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(triangle_drawing_test PRIVATE ${EXTRA_LIBS})
endif()

add_executable(omp_test ${CMAKE_SOURCE_DIR}/src/omp_test.c)
if(UNIX)
  target_link_libraries(omp_test PRIVATE m ${RT_LIB} ${EXTRA_LIBS})
else()
  target_link_libraries(omp_test PRIVATE ${EXTRA_LIBS})
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
