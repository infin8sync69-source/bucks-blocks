#!/usr/bin/env bash
# One entry point for every CI job; runs the same way on a developer's Mac or Linux box and in
# GitHub Actions (.github/workflows/ci.yml just calls this).
#
#   tools/ci.sh build      optimized build, all tests
#   tools/ci.sh asan       AddressSanitizer + UndefinedBehaviorSanitizer, all tests
#   tools/ci.sh tsan       ThreadSanitizer, all tests (network, miner and server run on threads)
#   tools/ci.sh fuzz       fuzz targets: replay the seed corpus, then fuzz for BUCKS_FUZZ_SECONDS
#                          (default 60 per target; the nightly job uses 600). Needs a clang with the
#                          libFuzzer runtime (Linux clang, Homebrew LLVM); set CC/CXX if it is not
#                          the default compiler. Without libFuzzer it only replays the corpus.
#   tools/ci.sh all        build, asan, tsan, fuzz
#
# Build directories are build-ci-<job> next to the sources (git-ignored).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOB="${1:-}"
JOBS="${BUCKS_CI_JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}"

configure_and_build() {   # <dir> <cmake args...>
    local dir="$1"; shift
    cmake -S "$ROOT" -B "$dir" "$@"
    cmake --build "$dir" -j "$JOBS"
}

job_build() {
    local dir="$ROOT/build-ci-build"
    configure_and_build "$dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    (cd "$dir" && ctest --output-on-failure --timeout 900)
}

job_asan() {
    local dir="$ROOT/build-ci-asan"
    configure_and_build "$dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUCKS_SANITIZE=address,undefined
    (cd "$dir" && \
        ASAN_OPTIONS="abort_on_error=1:detect_stack_use_after_return=1" \
        UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
        ctest --output-on-failure --timeout 900)
}

job_tsan() {
    local dir="$ROOT/build-ci-tsan"
    configure_and_build "$dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUCKS_SANITIZE=thread
    local opts="halt_on_error=1:history_size=5"
    # The default symbolizer can hang on macOS; resolve addresses with atos instead.
    [[ "$(uname)" == "Darwin" ]] && opts="$opts:symbolize=0"
    (cd "$dir" && TSAN_OPTIONS="$opts" ctest --output-on-failure --timeout 1800)
}

job_fuzz() {
    local dir="$ROOT/build-ci-fuzz"
    local seconds="${BUCKS_FUZZ_SECONDS:-60}"
    local extra=()
    # Prefer Homebrew LLVM on macOS: Apple's clang has no libFuzzer.
    if [[ -z "${CXX:-}" && -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
        extra+=(-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++)
    fi
    configure_and_build "$dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUCKS_FUZZ=ON "${extra[@]}" \
        ${CC:+-DCMAKE_C_COMPILER="$CC"} ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"}

    # 1. Every seed and every checked-in reproducer must pass (works with or without libFuzzer).
    (cd "$dir" && ctest -L fuzz --output-on-failure)

    # 2. Fuzz. Work on a copy of the corpus; new inputs and crashes stay out of the repository.
    if grep -q "BUCKS_HAVE_LIBFUZZER:INTERNAL=1" "$dir/CMakeCache.txt"; then
        local work="$dir/fuzz-work"
        rm -rf "$work"
        local failed=0
        for t in transaction block header undo netmessage compactsize; do
            mkdir -p "$work/$t/corpus" "$work/$t/crashes"
            cp "$ROOT/tests/fuzz/corpus/$t/"* "$work/$t/corpus/"
            echo "=== fuzzing $t for ${seconds}s"
            (cd "$work/$t" && "$dir/tests/fuzz/fuzz_$t" corpus -max_total_time="$seconds" \
                -artifact_prefix=crashes/ -print_final_stats=1) || failed=1
            if compgen -G "$work/$t/crashes/*" >/dev/null; then
                echo "FINDING in $t:"; ls "$work/$t/crashes"; failed=1
            fi
        done
        [[ $failed -eq 0 ]] || { echo "fuzzing found problems; reproducers are under $work"; exit 1; }
    else
        echo "No libFuzzer runtime in this compiler: only the corpus was replayed."
    fi
}

case "$JOB" in
    build) job_build ;;
    asan)  job_asan ;;
    tsan)  job_tsan ;;
    fuzz)  job_fuzz ;;
    all)   job_build; job_asan; job_tsan; job_fuzz ;;
    *)     sed -n '2,17p' "${BASH_SOURCE[0]}"; exit 2 ;;
esac
