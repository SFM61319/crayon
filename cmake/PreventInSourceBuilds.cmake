# Copyright (c) 2026 Avinash Maddikonda
# SPDX-License-Identifier: Apache-2.0

# Prevents in-source builds (e.g., running `cmake .` from the repository root).
# If an in-source build is detected, it stops configuration immediately and
# instructs the user how to properly configure an out-of-source build.
function(prevent_in_source_builds)
  # Get real physical paths to handle symlinks correctly across platforms
  get_filename_component(src_dir "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
  get_filename_component(bin_dir "${CMAKE_CURRENT_BINARY_DIR}" REALPATH)

  # Check if the source directory and binary directory are the same
  if(src_dir STREQUAL bin_dir)
    message(FATAL_ERROR "
==============================================================================
ERROR: In-source builds are strictly disabled for [Crayon]!
==============================================================================
You ran CMake directly inside the source directory:
  ${CMAKE_CURRENT_SOURCE_DIR}

In-source builds clutter the repository and can cause hard-to-debug cache errors.

To fix this:
  1. Remove generated CMake artifacts:
     Linux/macOS : rm -rf CMakeCache.txt CMakeFiles/
     Windows (PS): Remove-Item -Recurse -Force CMakeCache.txt, CMakeFiles

  2. Configure out-of-source:
     cmake -B build
     cmake --build build

  Or use standard Presets:
     cmake --preset dev
==============================================================================
")
  endif()
endfunction()

prevent_in_source_builds()
