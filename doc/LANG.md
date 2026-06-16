# C++ Projects — Coding Style & Conventions

Applies to all C++ projects under this directory.

## Naming

- If not specified, all data structures are in `forge` namespace.
  - **Class methods**: CamelCase style with `verb_noun`, - e.g., `ScheduleWork`.
  - **Free functions** (no implicit "self"): `verb_noun` — e.g., `ConvertHtml`, `ParseToken`.
  - **Object-like functions** (first arg is the owning struct): `noun_verb` — e.g., `ErrInit`, `ErrEmit`.
- **Internal / static** symbols: `snake_case`, no prefix.
- Constants `kConstant`.
- Macros: `UPPER_SNAKE_CASE`.

## Code Style

- Never use raw new/delete or malloc/free
- Prefer raw pointers for function inputs if the lifetime at callsite is longer
  than the function.
- Prefer std::vector, std::string over raw arrays
- Use std::unique_ptr for ownership if needed. Or prefer value in lexical scope.
- Avoid std::shared_ptr unless necessary
- Use RAII for all resources
- Keep code simple and readable (no heavy templates)

## Makefile

- Common tasks are `make run`, `make compile`, `make clean`, `make test`
- Standard: **C++17** (`-std=c++17`).
- Compiler is `${CXX}`.
- All c++ files are `.cc` suffix.
- Compiler flags: `-Wall -Werror -pedantic -Wextra -Wfatal-errors -Wconversion`.
- In Makefile, if `RELEASE` is defined, compiler flags should have
  `-O2 -DNDEBUG -march=native -flto -ffast-math` as well.
- Each logical concern gets its own function. No monolithic functions.
- No third-party libraries — stdlib only unless the project explicitly states otherwise.
- One blank line between top-level definitions. Section banners for logical groups:
  ```c++
  // === --- Section name ----------------------------------------------- ===
  //
  ```
- All produced assets must be in dir `.build`.

## Constants and Magic Numbers

All constants should be defined at top level to avoid magic numbers. And for
fixed size array, it is better to check the input length to report error.

## Error Handling

Every function that can fail takes a trailing `std::string *err_msg`
out-parameter and reports failures through it:

```c++
bool Foo( ..., std::string *err_msg );                    // false = error
std::optional<Bar> MakeBar( ..., std::string *err_msg );  // nullopt = error
```

On failure, write a human-readable description (include `strerror(errno)` where
relevant) to `*err_msg` and return the error sentinel (`false` or
`std::nullopt`). On success, leave `*err_msg` untouched.

A fallible function must **never** do either of the following:

- **Print the error itself** — no `std::cerr` / `fprintf(stderr, ...)`.
  Surfacing the message is the caller's decision, not the callee's.
- **Fail silently** — every error path must set `*err_msg`; never just return a
  sentinel with no explanation.

Only the top-level entry point (`cmd/main*.cc`) prints these messages, typically
to `stderr`. Plain predicates that answer a yes/no question (e.g. an existence
check returning `bool`) are not "fallible" and do not take `err_msg`.

The sole exception is a child process after `fork`: once `exec` fails there is
no channel back to the parent except the exit code, so the child may write to
`stderr` before `_exit`.

## Project Layout

- **Entry points** live in `cmd/`:
  - The primary executable is `cmd/main.cc`.
  - Additional executables are `cmd/main_<name>.cc` (e.g. `cmd/main_bench.cc`).
- **Tests** live in `cmd/` as `cmd/test_<name>.cc` (e.g. `cmd/test_cache.cc`).
- **All other source** — every library `.cc` and every header (`.h`) — lives in
  `src/`. Entry points and tests `#include` headers from `src/`.

```
project/
  cmd/
    main.cc            # primary entry point
    main_xx.cc         # additional entry points
    test_yyy.cc        # tests
  src/
    foo.h  foo.cc      # library code + headers
  doc/
  Makefile
```

## Entry Point Documentation

Every entry point in `cmd/` (`cmd/main.cc` and each `cmd/main_<name>.cc`) must
begin with a header comment describing how to use the resulting binary. At a
minimum it states the usage line, the arguments, and what the program does.

```c++
// xrun — a go-run-style launcher for single-file C++ programs.
//
// Usage:
//   xrun <file.cc> [args...]
//
//   <file.cc>   C++ source to compile (cached) and run.
//   [args...]   Forwarded verbatim to the compiled program.
//
// The program's exit code is propagated as xrun's exit code.
```
