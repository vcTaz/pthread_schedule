# Contributing

## Scope

This repository is intentionally small. Contributions should improve correctness, API clarity, documentation quality, and predictable concurrent behavior.

## Guidelines

- Preserve the public API unless the change is explicitly intended as an API revision.
- Keep scheduling semantics consistent across the implementation, header comments, and README.
- Prefer focused, reviewable patches over broad refactors.
- Use comments to explain intent or concurrency constraints, not obvious mechanics.

## Build

Using `make`:

```bash
make
```

Using CMake:

```bash
cmake -S . -B build
cmake --build build
```

## Review Checklist

- The project builds with the selected toolchain.
- Public API changes are reflected in documentation.
- Error handling remains consistent across helper functions.
- Example code still demonstrates supported usage patterns.
