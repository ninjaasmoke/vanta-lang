#!/usr/bin/env bash
# Vanta test runner.
#
# Layers:
#   examples/         existing smoke tests (untouched)
#   tests/cmp/        c-vs-vanta side-by-side. compile each .c with cc,
#                     run each .vt with ./vanta. expect same exit code.
#   tests/showcase/   vanta-only programs. expect exit 0 unless noted.
#   tests/fail/       programs that must fail. expect non-zero.
#
# Usage:  ./tests/run.sh [--verbose]
set -u

VANTA=${VANTA:-./vanta}
CC=${CC:-cc}
verbose=0
[ "${1:-}" = "--verbose" ] && verbose=1

pass=0
fail=0
total=0

red()    { printf '\033[31m%s\033[0m' "$1"; }
green()  { printf '\033[32m%s\033[0m' "$1"; }

ok()    { green ok;   printf '    %s\n' "$1"; pass=$((pass+1)); total=$((total+1)); }
bad()   { red FAIL;   printf '  %s\n' "$1"; fail=$((fail+1)); total=$((total+1)); }
note()  { [ $verbose -eq 1 ] && printf '       %s\n' "$1" || true; }

# ----- existing examples (smoke) ---------------------------------------------

check_exit() {
    local name="$1"; shift
    local want="$1"; shift
    local out
    out=$("$@" 2>&1)
    local got=$?
    if [ "$got" -eq "$want" ]; then
        ok "$name (exit=$got)"
    else
        bad "$name: want exit=$want, got=$got"
        note "$out"
    fi
}

printf '\n--- examples (smoke) ---\n'
check_exit "hello"              5  "$VANTA" run examples/hello.vt
check_exit "fib"               55  "$VANTA" run examples/fib.vt
check_exit "gcd"                6  "$VANTA" run examples/gcd.vt
check_exit "sum"               55  "$VANTA" run examples/sum.vt
check_exit "stack debug"        0  "$VANTA" run --debug   examples/stack.vt
check_exit "stack release"      0  "$VANTA" run --release examples/stack.vt
check_exit "stack overflow dbg" 2  "$VANTA" run --debug   examples/stack_overflow.vt
check_exit "check hello"        0  "$VANTA" check examples/hello.vt
check_exit "check stack debug"  0  "$VANTA" check --debug examples/stack.vt

# ----- tests/cmp: c vs vanta -------------------------------------------------

run_cmp() {
    local stem="$1"
    local cfile="tests/cmp/${stem}.c"
    local vfile="tests/cmp/${stem}.vt"
    local bin
    bin=$(mktemp /tmp/vanta-cmp.XXXXXX)

    if ! "$CC" -O0 -std=c11 -o "$bin" "$cfile" 2>/dev/null; then
        bad "cmp/$stem: cc failed"
        rm -f "$bin"
        return
    fi
    "$bin" >/dev/null 2>&1
    local cexit=$?
    rm -f "$bin"

    "$VANTA" run --debug "$vfile" >/dev/null 2>&1
    local vdbg=$?

    "$VANTA" run --release "$vfile" >/dev/null 2>&1
    local vrel=$?

    if [ "$cexit" -eq "$vdbg" ] && [ "$cexit" -eq "$vrel" ]; then
        ok "cmp/$stem (c=$cexit  vanta-dbg=$vdbg  vanta-rel=$vrel)"
    else
        bad "cmp/$stem: c=$cexit vanta-dbg=$vdbg vanta-rel=$vrel"
    fi
}

printf '\n--- tests/cmp: c vs vanta ---\n'
for vfile in tests/cmp/*.vt; do
    stem=$(basename "$vfile" .vt)
    run_cmp "$stem"
done

# ----- tests/showcase: vanta-only --------------------------------------------

printf '\n--- tests/showcase: vanta-only features ---\n'
check_exit "showcase/old_value"           3  "$VANTA" run --debug tests/showcase/old_value.vt
check_exit "showcase/struct_invariant"   10  "$VANTA" run --debug tests/showcase/struct_invariant.vt
check_exit "showcase/break_continue"     43  "$VANTA" run tests/showcase/break_continue.vt
check_exit "showcase/bank_account dbg"   57  "$VANTA" run --debug   tests/showcase/bank_account.vt
check_exit "showcase/bank_account rel"   57  "$VANTA" run --release tests/showcase/bank_account.vt
# variant_dispatch: same exit code in both modes; debug prints, release doesn't.
check_exit "showcase/variant_dispatch dbg" 30 "$VANTA" run --debug   tests/showcase/variant_dispatch.vt
check_exit "showcase/variant_dispatch rel" 30 "$VANTA" run --release tests/showcase/variant_dispatch.vt

# ----- tests/fail: must fail -------------------------------------------------

# helper: run, check exit and that stderr contains a substring.
expect_fail() {
    local name="$1"; shift
    local want_exit="$1"; shift
    local want_msg="$1"; shift
    local out
    out=$("$@" 2>&1)
    local got=$?
    if [ "$got" -ne "$want_exit" ]; then
        bad "$name: want exit=$want_exit got=$got"
        note "$out"
        return
    fi
    if ! printf '%s' "$out" | grep -q -- "$want_msg"; then
        bad "$name: missing message '$want_msg'"
        note "$out"
        return
    fi
    ok "$name (exit=$got, matched '$want_msg')"
}

printf '\n--- tests/fail: programs that must fail ---\n'
expect_fail "fail/pre_violation"    2 "@requires failed"     "$VANTA" run --debug tests/fail/pre_violation.vt
expect_fail "fail/post_violation"   2 "@ensures failed"      "$VANTA" run --debug tests/fail/post_violation.vt
expect_fail "fail/invariant"        2 "@invariant failed"    "$VANTA" run --debug tests/fail/invariant_violation.vt
expect_fail "fail/bank_invariant"   2 "@invariant failed on BankAccount" "$VANTA" run --debug tests/fail/bank_invariant.vt
expect_fail "fail/assert_fires"     2 "assertion failed"     "$VANTA" run tests/fail/assert_fires.vt
expect_fail "fail/type_error"       1 "type error"           "$VANTA" check tests/fail/type_error.vt
expect_fail "fail/variant_ambig"    1 "ambiguous variant"    "$VANTA" check --debug tests/fail/variant_ambiguous.vt

# ----- summary --------------------------------------------------------------

printf '\n'
if [ $fail -eq 0 ]; then
    green "all $total passed"; printf '\n'
    exit 0
else
    red "$fail of $total failed"; printf '\n'
    exit 1
fi
