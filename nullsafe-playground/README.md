# Nullsafe Clang Playground

An interactive web-based playground for experimenting with **Nullsafe Clang**, an experimental C compiler with strict nullability checking.

## Features

- 🔍 Real-time null-safety analysis
- 💻 In-browser C compilation using WebAssembly
- 🎨 Syntax highlighting and error detection
- 📝 Example code snippets
- 🚀 No installation required - runs entirely in the browser

## Quick Start

### Running Locally

1. Build the WASM files (or copy from existing build):
   ```bash
   ./build.sh
   ```

2. Start the local server:
   ```bash
   python3 serve.py
   ```

3. Open http://localhost:9000 in your browser

### Deployment to GitHub Pages

The playground can be deployed to GitHub Pages. The WASM files are generated from the Nullsafe Clang compiler build and should not be committed to the repository due to their size (~64MB).

For deployment:
1. Build the WASM files using `build.sh`
2. Deploy the `nullsafe-playground` directory to GitHub Pages
3. The WASM files will need to be hosted separately or built as part of CI/CD

## Comparison with the Clang Static Analyzer

Nullsafe Clang and the **Clang Static Analyzer (CSA)** both find null bugs, but they
work — and cost — very differently. Both start from the same Clang CFG, but the fork
keeps *one joined fact per program point* (fast, ~linear dataflow) while CSA keeps
*one simulated program state per path* (precise, path-sensitive, exponential worst
case). See [`nullsafe-vs-csa.md`](nullsafe-vs-csa.md) for the full architecture
breakdown, and the [`examples/`](examples/) that show what each catches
(e.g. `standard-clang-gap.c`, `csa-wins-correlated.c`, `template-contract.cpp`).

**What each catches (correctness):**
- The fork catches contract violations across declaration boundaries, in template
  *definitions*, and on smart pointers — and can flag them under `-fsyntax-only` on
  every build.
- CSA finds concrete null paths in *un-annotated* code and produces full path traces,
  but goes silent behind un-inlined declarations and off-by-default checkers.

**What each costs (wall time).** Measured head-to-head — same binary, three modes —
in [`../tools/benchmarks/wall-time/`](../tools/benchmarks/wall-time/PERFORMANCE.md):

| Mode vs. baseline `-fsyntax-only` | Overhead | Notes |
|---|---|---|
| **nullsafe fork**, real clang/LLVM TUs (n=24, paired) | **+4.5%** (CI +0.3…+9%, p=0.049) | scales with pointer density, not code size |
| nullsafe fork, pointer-dense worst case | +18.9% | linear dataflow pass, never explodes |
| **static analyzer**, real clang/LLVM TUs (n=24, paired) | **1.95×** (CI 1.2–3.1×, p=0.009) | unbounded — up to **278×** on one real file |

The analyzer numbers use CSA's **default** RangeConstraintManager (this build has no
Z3/SAT solver; Z3 crosscheck would be ~5–10× slower again). Full methodology,
caveats, and a reproducible harness live in
[`tools/benchmarks/wall-time/`](../tools/benchmarks/wall-time/PERFORMANCE.md).

## Architecture

- **index.html** - Main playground interface
- **clang.wasm** - Clang compiler compiled to WebAssembly (generated, not committed)
- **clang.js** - Emscripten-generated JavaScript glue code (generated, not committed)
- **build.sh** - Script to build WASM files from Clang source
- **serve.py** - Simple HTTP server for local development

## Building WASM Files

The WASM files are generated from the Nullsafe Clang compiler using Emscripten:

```bash
# TODO: Document proper Emscripten build process
# Currently using pre-built WASM files
./build.sh
```

## Development

To modify the playground:
1. Edit `index.html`
2. Refresh your browser (no rebuild needed)
3. Changes to the compiler require rebuilding WASM files

## License

Same as LLVM Project
