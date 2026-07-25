# Copyright (c) 2026 Avinash Maddikonda
# SPDX-License-Identifier: Apache-2.0

include(FetchContent)

# Declare GoogleTest via FetchContent (using a stable tag/URL)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  SYSTEM
  EXCLUDE_FROM_ALL
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Create your test executable
add_executable(crayon_tests
  test_crayon.cpp
)

# Link against your library and GoogleTest's main wrapper
target_link_libraries(crayon_tests
  PRIVATE
    crayon::crayon
    GTest::gtest_main
)

# Integrate with CTest for auto-discovery
include(GoogleTest)
gtest_discover_tests(crayon_tests
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
