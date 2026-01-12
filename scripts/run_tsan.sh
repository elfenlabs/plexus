#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-tsan}"
DEPS_DIR="${DEPS_DIR:-${ROOT_DIR}/build/_deps}"

CMAKE_ARGS=(
  "-DCMAKE_CXX_FLAGS=-O1 -g -fsanitize=thread -fno-omit-frame-pointer"
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"
)

if [[ -d "${DEPS_DIR}/googletest-src" ]]; then
  CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_googletest=${DEPS_DIR}/googletest-src")
fi
if [[ -d "${DEPS_DIR}/googlebenchmark-src" ]]; then
  CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_googlebenchmark=${DEPS_DIR}/googlebenchmark-src")
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" -j "${JOBS:-8}"

TEST_FILTER="${TEST_FILTER:-*:-StressLoopTest.*}"
TSAN_OPTIONS="${TSAN_OPTIONS:-second_deadlock_stack=1 history_size=7}"
export TSAN_OPTIONS

if [[ -n "${TEST_ARGS:-}" ]]; then
  # shellcheck disable=SC2086
  "${BUILD_DIR}/plexusTests" --gtest_filter="${TEST_FILTER}" ${TEST_ARGS}
else
  "${BUILD_DIR}/plexusTests" --gtest_filter="${TEST_FILTER}"
fi
