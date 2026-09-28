#!/bin/sh
set -eu

GEN=${1:-build/gen_trace}

fail() {
    echo "test_gen_trace: $1" >&2
    exit 1
}

output=$($GEN random 16 2 1 4) || fail "valid random workload failed"
printf '%s\n' "$output" | grep -q '# 2 accesses, highest address' \
    || fail "valid random workload produced the wrong summary"

expect_failure() {
    output=$("$@" 2>/dev/null) && fail "invalid invocation succeeded: $*"
    [ -z "$output" ] || fail "invalid invocation wrote a partial trace: $*"
}

expect_failure "$GEN" random 1 1 1 4
expect_failure "$GEN" sequential 16 4 unexpected
expect_failure "$GEN" sequential -1 4
expect_failure "$GEN" strided 16 32 1 4
expect_failure "$GEN" matmul-naive 100000 8

echo "test_gen_trace: PASS"
