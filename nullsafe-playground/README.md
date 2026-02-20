# Null-Safe C Playground

An interactive web-based playground for experimenting with **Null-Safe C**, an experimental C compiler with strict nullability checking.

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

The playground can be deployed to GitHub Pages. The WASM files are generated from the Null-Safe C compiler build and should not be committed to the repository due to their size (~64MB).

For deployment:
1. Build the WASM files using `build.sh`
2. Deploy the `nullsafe-playground` directory to GitHub Pages
3. The WASM files will need to be hosted separately or built as part of CI/CD

## Architecture

- **index.html** - Main playground interface
- **clang.wasm** - Clang compiler compiled to WebAssembly (generated, not committed)
- **clang.js** - Emscripten-generated JavaScript glue code (generated, not committed)
- **build.sh** - Script to build WASM files from Clang source
- **serve.py** - Simple HTTP server for local development

## Building WASM Files

The WASM files are generated from the Null-Safe C compiler using Emscripten:

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
