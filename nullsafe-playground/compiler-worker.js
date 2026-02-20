// Web Worker for running Clang WASM compiler
// This worker is terminated and recreated for each compilation to avoid
// LLVM signal handler registration issues

let isReady = false;

// Handle messages from main thread
self.onmessage = function(e) {
    const { type, ...data } = e.data;

    if (type === 'load') {
        // Configure Module BEFORE loading clang.js
        // The main thread pre-loaded the WASM binary and is sharing it
        self.Module = {
            print: function(text) {
                self.postMessage({ type: 'stdout', text });
            },
            printErr: function(text) {
                self.postMessage({ type: 'stderr', text });
            },
            noInitialRun: true,
            wasmBinary: data.wasmBinary,  // Use the shared WASM binary
            postRun: [function() {
                // postRun happens AFTER runtime initialization
                const FS = self.Module.FS || self.FS || (typeof FS !== 'undefined' ? FS : null);

                if (FS) {
                    isReady = true;
                    self.postMessage({ type: 'ready' });
                } else {
                    // Emscripten might attach FS later, poll for it
                    let attempts = 0;
                    const checkFS = setInterval(() => {
                        attempts++;
                        const fsNow = self.Module.FS || self.FS || (typeof FS !== 'undefined' ? FS : null);

                        if (fsNow) {
                            clearInterval(checkFS);
                            isReady = true;
                            self.postMessage({ type: 'ready' });
                        } else if (attempts > 100) {
                            clearInterval(checkFS);
                            self.postMessage({
                                type: 'error',
                                error: 'Filesystem never became available'
                            });
                        }
                    }, 100);
                }
            }]
        };

        // Load the Emscripten-generated JavaScript
        try {
            importScripts(data.scriptUrl);
        } catch (error) {
            self.postMessage({
                type: 'error',
                error: `Failed to load compiler: ${error.message}`
            });
        }
    } else if (type === 'compile') {
        const FS = self.Module?.FS || self.FS;

        if (!isReady || !FS) {
            self.postMessage({
                type: 'error',
                error: 'Compiler not ready - FS not available'
            });
            return;
        }

        try {
            const { code, extraFlags = [] } = data;
            const inputFile = 'input.c';

            // Write source file
            FS.writeFile(inputFile, code);

            // Build arguments
            const args = [
                '-fsyntax-only',
                '--target=wasm32-unknown-emscripten',
                ...extraFlags,
                inputFile
            ];

            // Run compiler
            const exitCode = self.Module.callMain(args);

            // Clean up
            try {
                FS.unlink(inputFile);
            } catch (e) {
                // Ignore
            }

            // Send completion
            self.postMessage({
                type: 'complete',
                exitCode
            });

        } catch (error) {
            self.postMessage({
                type: 'error',
                error: error.message || String(error)
            });
        }
    }
};
