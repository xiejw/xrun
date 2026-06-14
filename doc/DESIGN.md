# xrun — Design

## Overview

`xrun` is a `go run`-style launcher for single-file C++ programs. Instead of a
manual `c++ a.cc -o a && ./a` cycle, you run:

```
xrun a.cc [args...]
```

and `xrun` compiles the file (the first time), caches the resulting executable,
and runs it. On subsequent invocations of an **unchanged** file, the cached
binary is executed directly with no recompilation — just like `go run` reuses
its build cache.

## Usage

```
xrun <file.cc> [args...]
```

- `<file.cc>` — the C++ source to compile and run.
- `[args...]` — forwarded verbatim as the program's arguments (`argv[1..]`).
- The compiled program's exit code is propagated as `xrun`'s exit code.

## Cache key

The cache key is the SHA-256 hex digest of:

```
<absolute path>\n<mtime seconds>\n
```

where the absolute path comes from `realpath(3)` and the mtime from
`stat(2)` (`st_mtime`). Hashing is done by piping that payload into the
`sha256sum` program (no in-process crypto).

Rationale: editing the file changes its mtime, which changes the key, which
forces a fresh compile under a new cache entry. An unchanged file maps to the
same key and hits the cache.

### Limitation (by design)

The key intentionally does **not** include the compiler identity or flags. If
you upgrade your toolchain and want every program rebuilt, clear the cache:

```
rm -rf ~/.cache/xrun
```

## Cache layout

```
~/.cache/xrun/<checksum>      # the executable itself
~/.cache/xrun/<checksum>.tmp.<pid>   # transient, during compile only
```

The cache entry *is* the binary — there is no metadata sidecar.

## Modules

| Module          | Files                | Responsibility                          |
|-----------------|----------------------|------------------------------------------|
| `forge::cache`  | `cache.h`/`cache.cc` | Key derivation, checksum, cache paths, existence checks, `~/.cache/xrun` creation. |
| `forge::proc`   | `proc.h`/`proc.cc`   | Subprocess helpers via `fork`/`exec` (capture, wait, exec-replace). |
| `main`          | `main.cc`            | Argument parsing, orchestration, exit-code passthrough. |

### `forge::cache`

- `CacheKeyFor(src)` → `{abs_path, mtime}` (or error if the file is missing).
- `ComputeChecksum(input)` → 64-char hex digest via `sha256sum`.
- `CacheDir()` / `CachePathFor(checksum)` → path helpers (`~/.cache/xrun/...`).
- `IsCached(path)` → is there already a regular file at this cache path?
- `EnsureCacheDir()` → create `~/.cache` then `~/.cache/xrun`.

### `forge::proc`

- `RunCapture(argv, stdin_data)` → run a program feeding it `stdin_data`, return
  `{exit_code, stdout}`. Used for `sha256sum`.
- `RunWait(argv)` → run to completion inheriting stdio, return exit code. Used
  for the `c++` compile so diagnostics reach the terminal directly.
- `RunExec(path, argv)` → `execv` the cached binary, replacing this process so
  its exit code propagates naturally.

## Control flow

```
xrun a.cc [args...]
  1. CacheKeyFor(a.cc)         -> {abs_path, mtime}      (error if missing)
  2. ComputeChecksum(...)      -> <checksum>  (via sha256sum subprocess)
  3. CachePathFor(checksum)    -> ~/.cache/xrun/<checksum>
  4. IsCached(path)?
        yes -> go to 6
        no  -> EnsureCacheDir()
               c++ -std=c++17 <abs_path> -o <path>.tmp.<pid>
               on success: rename(tmp, path)   (atomic publish)
               on failure: print diagnostics, exit nonzero, nothing cached
  5. (binary now present)
  6. execv(path, {path, args...})   -> child's exit code becomes ours
```

## Subprocess strategy

All process spawning uses `fork`/`execvp` directly — no `system()` / shell — so
absolute paths with spaces or shell metacharacters can never be misinterpreted
or injected.

`sha256sum` is fed via a pair of pipes (parent writes stdin, reads stdout). The
input is tiny (a path plus a number), so a single-threaded write-then-read
sequence cannot deadlock on a full pipe.

## Atomic write

Compilation targets `~/.cache/xrun/<checksum>.tmp.<pid>` and is `rename(2)`-d
into place only on a clean (exit 0) compile. A crash or interrupt mid-compile
therefore never leaves a half-written binary at the real cache path; an
incomplete temp file is unlinked.

## Error handling

- **Missing / unreadable source** → message to stderr, exit 1.
- **`sha256sum` failure** → message to stderr, exit 1.
- **Compile failure** → compiler diagnostics shown (inherited stderr), exit 1,
  nothing written to the cache.
- **`execv` failure** → message to stderr, exit 1.

## Build

`make compile` produces `.build/xrun` with the project's strict flags
(`-Wall -Werror -pedantic -Wextra -Wfatal-errors -Wconversion`, `-std=c++17`).
`make test` runs `test.sh` (end-to-end smoke test). `make clean` removes
`.build`.

## Future extensions (not implemented)

- Fold the compiler version + flags into the cache key so a toolchain change
  rebuilds automatically.
- Honor extra compile flags from an env var (e.g. `XRUN_CXXFLAGS`).
- Support multi-file programs.
