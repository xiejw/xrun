#!/usr/bin/env bash
# End-to-end smoke test for xrun.
set -uo pipefail

BIN=.build/xrun
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
check() { # desc expected actual
  if [ "$2" = "$3" ]; then
    echo "ok:   $1"
    pass=$((pass + 1))
  else
    echo "FAIL: $1 (expected '$2', got '$3')"
    fail=$((fail + 1))
  fi
}

# Compute the cache key the same way xrun does: abspath + mtime -> sha256.
key_for() {
  printf '%s\n%s\n' "$(realpath "$1")" "$(stat -c %Y "$1")" |
    sha256sum | cut -d' ' -f1
}

# --- A program that echoes its args and exits 7 ----------------------
SRC="$TMP/hello.cc"
cat >"$SRC" <<'EOF'
#include <cstdio>
int main(int argc, char** argv) {
  std::printf("hello");
  for (int i = 1; i < argc; ++i) std::printf(" %s", argv[i]);
  std::printf("\n");
  return 7;
}
EOF

# 1. First run: compiles, forwards args, propagates exit code.
out=$("$BIN" "$SRC" one two)
rc=$?
check "args forwarded"        "hello one two" "$out"
check "exit code propagated"  "7"             "$rc"

cache="$HOME/.cache/xrun/$(key_for "$SRC")"
check "cache file created"    "yes"           "$([ -f "$cache" ] && echo yes || echo no)"

# 2. Second run is a cache hit: the cached binary is not rebuilt.
before=$(stat -c %Y "$cache")
"$BIN" "$SRC" >/dev/null
after=$(stat -c %Y "$cache")
check "cache hit (no rebuild)" "$before" "$after"

# 3. Touching the source changes mtime -> new key -> rebuild.
key1=$(key_for "$SRC")
touch -d '+1 second' "$SRC"
"$BIN" "$SRC" >/dev/null
key2=$(key_for "$SRC")
check "new key after edit"     "yes" "$([ "$key1" != "$key2" ] && echo yes || echo no)"
check "rebuilt under new key"  "yes" "$([ -f "$HOME/.cache/xrun/$key2" ] && echo yes || echo no)"

# 4. Compile error: nonzero exit, nothing cached.
BAD="$TMP/bad.cc"
echo 'int main() { return frobnicate; }' >"$BAD"
"$BIN" "$BAD" >/dev/null 2>&1
erc=$?
check "compile error nonzero"  "yes" "$([ "$erc" -ne 0 ] && echo yes || echo no)"
check "compile error uncached" "no"  "$([ -f "$HOME/.cache/xrun/$(key_for "$BAD")" ] && echo yes || echo no)"

# 5. Missing file is a clean error.
"$BIN" "$TMP/nope.cc" >/dev/null 2>&1
mrc=$?
check "missing file nonzero"   "yes" "$([ "$mrc" -ne 0 ] && echo yes || echo no)"

echo "----"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
