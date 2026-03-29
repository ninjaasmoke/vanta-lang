#!/usr/bin/env bash
# tiny smoke runner. checks examples produce the expected exit code.
# usage: ./tests/run.sh
set -u

VANTA=${VANTA:-./vanta}
fail=0

check() {
    local name="$1"; shift
    local expected="$1"; shift
    "$@" >/dev/null 2>&1
    local got=$?
    if [ "$got" -ne "$expected" ]; then
        printf 'FAIL  %-40s expected %s got %s\n' "$name" "$expected" "$got"
        fail=1
    else
        printf 'ok    %-40s exit=%s\n' "$name" "$got"
    fi
}

check "hello"             5  "$VANTA" run examples/hello.vt
check "fib"               55 "$VANTA" run examples/fib.vt
check "gcd"               6  "$VANTA" run examples/gcd.vt
check "sum"               55 "$VANTA" run examples/sum.vt
check "stack debug"       0  "$VANTA" run --attr debug   examples/stack.vt
check "stack release"     0  "$VANTA" run --attr release examples/stack.vt
check "stack overflow dbg" 2 "$VANTA" run --attr debug   examples/stack_overflow.vt
check "check hello"       0  "$VANTA" check examples/hello.vt
check "check stack debug" 0  "$VANTA" check --attr debug examples/stack.vt

exit $fail
