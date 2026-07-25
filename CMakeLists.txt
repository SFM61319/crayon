# Copyright (c) 2026 Avinash Maddikonda
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28...4.4)

project(crayon
  VERSION 0.1.0
  DESCRIPTION "A data parallelism library for C++, inspired by Rayon for Rust"
  HOMEPAGE_URL "https://github.com/SFM61319/crayon"
  LANGUAGES CXX
)

# Guard against accidental in-source builds
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreventInSourceBuilds.cmake")
include(CMakeDependentOption)

# Public Build Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Dependent Options (Only exposed in GUI/Cache if Crayon is the top-level project)
cmake_dependent_option(
  CRAYON_BUILD_TESTS "Build test suite" ON
  "PROJECT_IS_TOP_LEVEL" OFF
)
cmake_dependent_option(
  CRAYON_BUILD_EXAMPLES "Build example binaries" ON
  "PROJECT_IS_TOP_LEVEL" OFF
)

# ==============================================================================
# TARGET DEFINITION
# ==============================================================================

find_package(Threads REQUIRED)

add_library(crayon)
add_library(crayon::crayon ALIAS crayon)

target_sources(crayon
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/crayon.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/include/crayon/crayon.hpp"
)

# FILE_SET handles public include paths; PRIVATE handles internal src headers
target_include_directories(crayon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Set C++23 standard and propagate it to consumers
target_compile_features(crayon PUBLIC cxx_std_23)

# Enforce strict standard requirements locally
set_target_properties(crayon PROPERTIES
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_link_libraries(crayon PUBLIC Threads::Threads)

# Compiler Warning Flags
if(MSVC)
  target_compile_options(crayon PRIVATE /W4 /permissive-)
else()
  target_compile_options(crayon PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Enable warning-as-error when building Crayon as the main project
if(PROJECT_IS_TOP_LEVEL)
  set_target_properties(crayon PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endif()

# ==============================================================================
# INSTALLATION & EXPORT
# ==============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS crayon
  EXPORT crayonTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT crayonTargets
  FILE crayonTargets.cmake
  NAMESPACE crayon::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/crayon"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/crayonConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/crayonConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/crayonConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/crayon"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/crayonConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/crayonConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/crayon"
)

# Export targets to binary directory for build-tree usage
export(EXPORT crayonTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/crayonTargets.cmake"
  NAMESPACE crayon::
)

# ==============================================================================
# SUBDIRECTORIES
# ==============================================================================

if(CRAYON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CRAYON_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
