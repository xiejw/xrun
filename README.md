Overview
========

`xrun` is a `go run`-style launcher for single-file C++ programs. Instead of a
manual `c++ a.cc -o a && ./a` cycle, you run:

```
xrun a.cc [args...]
```

and `xrun` compiles the file (the first time), caches the resulting executable,
and runs it. On subsequent invocations of an **unchanged** file, the cached
binary is executed directly with no recompilation — just like `go run` reuses
its build cache.
