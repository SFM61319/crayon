# Copyright (c) 2026 Avinash Maddikonda
# SPDX-License-Identifier: Apache-2.0

# Create example executable
add_executable(01_basic_usage
  01_basic_usage.cpp
)

# Link directly against your library alias
target_link_libraries(01_basic_usage
  PRIVATE
    crayon::crayon
)
