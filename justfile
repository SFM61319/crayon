# Copyright (c) 2026 Avinash Maddikonda
# SPDX-License-Identifier: Apache-2.0

# https://just.systems

# Default recipe when running `just` without arguments
default: help

_require *tools:
    #!/usr/bin/env sh
    missing=0
    for tool in {{ tools }}; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            missing=1
            printf 'Missing required tool: %s\n' "${tool}" >&2
        fi
    done
    exit "${missing}"

# List all available recipes
help:
  @{{ just_executable() }} --list

# Format code using `clang-format` (parallelized across CPU cores)
format: (_require "clang-format")
  find include/ src/ tests/ examples/ \( -name '*.hpp' -o -name '*.cpp' \) -print0 | xargs -0 -P 0 -n 1 clang-format -i

# Configure the build directory using CMake presets
config preset="dev" *args: (_require "cmake")
  cmake --preset {{ preset }} {{ args }}
  ln -sf build/{{ preset }}/compile_commands.json compile_commands.json

# One-time setup: validates tools, configures CMake, and links compile_commands.json
setup preset="dev" *args: (_require "cmake" "ctest" "ninja" "clang-format" "clang-tidy" "run-clang-tidy") (config preset args)

# Run `clang-tidy` static analysis on project sources
lint preset="dev" *args: (_require "clang-tidy" "run-clang-tidy") (config preset)
  run-clang-tidy -p=build/{{ preset }} -header-filter="^$(pwd)/(include|src)/.*" "^$(pwd)/(src|tests|examples)/.*" {{ args }}

# Build the project (library, tests, examples)
build preset="dev" *args: (_require "cmake" "ninja") (config preset)
  cmake --build --preset {{ preset }} {{ args }}

# Run unit tests via CTest
test preset="dev" *args: (_require "ctest") (build preset)
  ctest --preset {{ preset }} {{ args }}

# Package project via CPack
pack preset="dev" *args: (_require "cpack") (build preset)
  cpack --preset {{ preset }} {{ args }}

# Run a complete local check (Format, Lint, Test)
check preset="dev": format (lint preset) (test preset)

# Clean build artifacts using the active preset's build target
clean-target preset="dev" *args: (build preset "--target" "clean" args)

# Clean config and build artifacts
clean:
  rm -f compile_commands.json
  rm -rf build/
