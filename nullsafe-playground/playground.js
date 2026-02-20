        const editorElement = document.getElementById('editor');
        const outputNullsafe = document.getElementById('output-nullsafe');
        const outputMainline = document.getElementById('output-mainline');
        const compileBtn = document.getElementById('compileBtn');
        const status = document.getElementById('status');
        const examplesSelect = document.getElementById('examplesSelect');
        const shareBtn = document.getElementById('shareBtn');
        const loadingBar = document.getElementById('loadingBar');
        const toast = document.getElementById('toast');
        const divider = document.getElementById('divider');
        const outputDivider = document.getElementById('outputDivider');

        let editor = null; // Monaco editor instance

        // Map example numbers to names for URL parameters
        const exampleNumbers = {
            '1': 'null-check',
            '2': 'early-return',
            '3': 'pure-function',
            '4': 'function-invalidates',
            '5': 'nonnull-annotation',
            '6': 'multi-level',
            '7': 'and-pattern',
            '8': 'else-branch',
            '9': 'loop-narrowing',
            '10': 'dereference-context',
            '11': 'redis-sds',
            '12': 'cjson-escape'
        };

        // Reverse mapping from names to numbers
        const exampleNames = {};
        for (const [num, name] of Object.entries(exampleNumbers)) {
            exampleNames[name] = num;
        }

const examples = {};
const exampleFiles = {
    'null-check': 'examples/null-check.c',
    'early-return': 'examples/early-return.c',
    'pure-function': 'examples/pure-function.c',
    'function-invalidates': 'examples/function-invalidates.c',
    'nonnull-annotation': 'examples/nonnull-annotation.c',
    'multi-level': 'examples/multi-level.c',
    'and-pattern': 'examples/and-pattern.c',
    'else-branch': 'examples/else-branch.c',
    'loop-narrowing': 'examples/loop-narrowing.c',
    'dereference-context': 'examples/dereference-context.c',
    'redis-sds': 'examples/redis-sds.c',
    'cjson-escape': 'examples/cjson-escape.c'
};

// Load all examples at startup
async function loadExamples() {
    for (const [name, file] of Object.entries(exampleFiles)) {
        try {
            const response = await fetch(file);
            examples[name] = await response.text();
        } catch (err) {
            console.error('Failed to load example:', name, err);
            examples[name] = '// Failed to load example';
        }
    }
}


        // Monaco editor will be initialized async
        function initMonaco() {
            return new Promise((resolve) => {
                require(['vs/editor/editor.main'], function() {
                    editor = monaco.editor.create(editorElement, {
                        value: examples['null-check'] || '// Loading...',
                        language: 'c',
                        theme: 'vs-dark',
                        automaticLayout: true,
                        minimap: { enabled: false },
                        fontSize: 14,
                        lineNumbers: 'on',
                        scrollBeyondLastLine: false,
                        wordWrap: 'off',
                        tabSize: 4
                    });

                    // Add Ctrl+Enter shortcut
                    editor.addAction({
                        id: 'compile-code',
                        label: 'Compile Code',
                        keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
                        run: function() {
                            compile();
                        }
                    });

                    // Real-time diagnostics on content change
                    let diagnosticsTimeout;
                    editor.onDidChangeModelContent(() => {
                        clearTimeout(diagnosticsTimeout);
                        diagnosticsTimeout = setTimeout(async () => {
                            if (!scriptUrl || isCompiling) return;
                            try {
                                const code = getEditorValue();
                                const result = await compileCode(code, []);
                                const diagnostics = parseDiagnostics(result.stderr);
                                monaco.editor.setModelMarkers(editor.getModel(), 'clang', diagnostics);
                            } catch (error) {
                                // Silently fail - diagnostics are non-critical
                            }
                        }, 500);
                    });

                    resolve();
                });
            });
        }

        // Helper to get/set editor content
        function getEditorValue() {
            return editor ? editor.getValue() : '';
        }

        function setEditorValue(value) {
            if (editor) {
                editor.setValue(value);
            }
        }

        // Parse clang diagnostics into Monaco markers
        function parseDiagnostics(stderr) {
            const markers = [];
            const lines = stderr.split('\n');
            const diagnosticRegex = /^input\.c:(\d+):(\d+):\s+(error|warning|note):\s+(.+)$/;

            for (const line of lines) {
                const match = line.match(diagnosticRegex);
                if (match) {
                    const [, lineNum, colNum, severity, message] = match;
                    markers.push({
                        startLineNumber: parseInt(lineNum),
                        startColumn: parseInt(colNum),
                        endLineNumber: parseInt(lineNum),
                        endColumn: parseInt(colNum) + 1,
                        message: message,
                        severity: severity === 'error' ? monaco.MarkerSeverity.Error
                                : severity === 'warning' ? monaco.MarkerSeverity.Warning
                                : monaco.MarkerSeverity.Info
                    });
                }
            }
            return markers;
        }

        let clangVersion = '';
        let isCompiling = false;
        let isDragging = false;
        let scriptUrl = null;
        let wasmBinary = null;

        // Toast notification
        function showToast(message, duration = 2000) {
            toast.textContent = message;
            toast.classList.add('show');
            setTimeout(() => {
                toast.classList.remove('show');
            }, duration);
        }

        // Status update helper
        function setStatus(message, showSpinner = false, progressPercent = null) {
            if (showSpinner) {
                let spinner;
                if (progressPercent !== null) {
                    spinner = '<span class="loading-spinner" style="--progress: ' + progressPercent + '"></span>';
                } else {
                    spinner = '<span class="loading-spinner"></span>';
                }
                status.innerHTML = spinner + ' ' + message;
                if (progressPercent !== null) {
                    status.title = progressPercent + '% downloaded';
                    const spinnerEl = status.querySelector('.loading-spinner::after');
                    if (spinnerEl) {
                        const degrees = (progressPercent / 100) * 360;
                        spinnerEl.style.transform = 'rotate(' + degrees + 'deg)';
                    }
                } else {
                    status.title = '';
                }
            } else {
                status.textContent = message;
                status.title = '';
            }
        }

        // Update progress spinner
        function updateProgress(percent) {
            const spinner = status.querySelector('.loading-spinner');
            if (spinner) {
                const degrees = (percent / 100) * 360;
                spinner.style.setProperty('--progress', degrees + 'deg');
                status.title = Math.round(percent) + '% downloaded';
            }
        }

        // Resizable panes
        let isDraggingOutput = false;

        divider.addEventListener('mousedown', (e) => {
            isDragging = true;
            e.preventDefault();
        });

        // Output divider resizing
        outputDivider.addEventListener('mousedown', (e) => {
            isDraggingOutput = true;
            e.preventDefault();
        });

        document.addEventListener('mousemove', (e) => {
            if (isDragging) {
                const container = document.querySelector('.main-container');
                const containerRect = container.getBoundingClientRect();
                const offsetX = e.clientX - containerRect.left;
                const percentage = (offsetX / containerRect.width) * 100;

                if (percentage > 20 && percentage < 80) {
                    const panes = document.querySelectorAll('.pane');
                    panes[0].style.flex = '0 0 ' + percentage + '%';
                    panes[1].style.flex = '0 0 ' + (100 - percentage) + '%';
                }
            }

            if (isDraggingOutput) {
                const container = document.querySelector('.output-container');
                const containerRect = container.getBoundingClientRect();
                const offsetY = e.clientY - containerRect.top;
                const percentage = (offsetY / containerRect.height) * 100;

                if (percentage > 20 && percentage < 80) {
                    const sections = document.querySelectorAll('.output-section');
                    sections[0].style.flex = '0 0 ' + percentage + '%';
                    sections[1].style.flex = '0 0 ' + (100 - percentage) + '%';
                }
            }
        });

        document.addEventListener('mouseup', () => {
            isDragging = false;
            isDraggingOutput = false;
        });

        // Examples dropdown
        examplesSelect.addEventListener('change', (e) => {
            const example = e.target.value;
            if (example && examples[example]) {
                setEditorValue(examples[example]);

                // Update URL with ?example= parameter using example number
                const url = new URL(window.location);
                url.searchParams.delete('code'); // Remove code param if present
                const exampleNum = exampleNames[example];
                if (exampleNum) {
                    url.searchParams.set('example', exampleNum);
                }
                window.history.replaceState({}, '', url);

                // Auto-compile the new example
                compileBtn.click();
            }
        });

        // Share button - encode code in URL
        shareBtn.addEventListener('click', async () => {
            try {
                const code = getEditorValue();
                const encoded = btoa(unescape(encodeURIComponent(code)));
                const url = new URL(window.location.href.split('?')[0]);

                // Remove example param and add code param
                url.searchParams.delete('example');
                url.searchParams.set('code', encoded);

                await navigator.clipboard.writeText(url.toString());
                showToast('Share link copied to clipboard!');
            } catch (err) {
                showToast('Failed to create share link');
                console.error('Share error:', err);
            }
        });

        // Report bug button - create GitHub issue with context
        const reportBugBtn = document.getElementById('reportBugBtn');
        reportBugBtn.addEventListener('click', () => {
            // Check if compiler is loaded and code has been compiled
            if (!clangVersion || outputNullsafe.textContent === '' || outputNullsafe.textContent === '(no output yet)') {
                if (!confirm('You haven\'t compiled any code yet. The bug report will be incomplete. Continue anyway?')) {
                    return;
                }
            }

            const code = getEditorValue();
            const nullsafeOutput = outputNullsafe.textContent || '(no output yet)';
            const mainlineOutput = outputMainline.textContent || '(no output yet)';

            // Get browser info - parse user agent for readability
            const ua = navigator.userAgent;
            let browserInfo = ua;

            // Try to parse browser name and version
            let browserName = 'Unknown';
            let browserVersion = '';
            if (ua.includes('Chrome/') && !ua.includes('Edg/')) {
                browserName = 'Chrome';
                browserVersion = ua.match(/Chrome\/([0-9.]+)/)?.[1] || '';
            } else if (ua.includes('Edg/')) {
                browserName = 'Edge';
                browserVersion = ua.match(/Edg\/([0-9.]+)/)?.[1] || '';
            } else if (ua.includes('Firefox/')) {
                browserName = 'Firefox';
                browserVersion = ua.match(/Firefox\/([0-9.]+)/)?.[1] || '';
            } else if (ua.includes('Safari/') && !ua.includes('Chrome/')) {
                browserName = 'Safari';
                browserVersion = ua.match(/Version\/([0-9.]+)/)?.[1] || '';
            }

            // Get OS info
            let osInfo = 'Unknown';
            if (ua.includes('Mac OS X')) {
                const osVersion = ua.match(/Mac OS X ([0-9_]+)/)?.[1]?.replace(/_/g, '.') || '';
                osInfo = 'macOS ' + osVersion;
            } else if (ua.includes('Windows')) {
                osInfo = 'Windows';
            } else if (ua.includes('Linux')) {
                osInfo = 'Linux';
            }

            const formattedBrowser = browserName + ' ' + browserVersion + ' on ' + osInfo;

            // Build the issue body
            const issueBody = '## Bug Report from Playground\n\n' +
                '### Environment\n' +
                '- **Compiler Version:** ' + (clangVersion || 'not loaded') + '\x60\x60\x60\n' +
                '- **Browser:** ' + formattedBrowser + '\x60\x60\x60\n' +
                '- **User Agent:** ' + ua + '\x60\x60\x60\n' +
                '- **Playground URL:** ' + window.location.href + '\n\n' +
                '### Code\n' +
                '\x60\x60\x60c\n' +
                code + '\x60\x60\x60\n' +
                '### Compiler Output\n\n' +
                '#### With Null Warnings\n' +
                '\x60\x60\x60\n' +
                nullsafeOutput + '\x60\x60\x60\n' +
                '#### Without Null Warnings\n' +
                '\x60\x60\x60\n' +
                mainlineOutput + '\x60\x60\x60\n' +
                '### Expected Behavior\n' +
                '<!-- Please describe what you expected to happen -->\n\n\n' +
                '### Actual Behavior\n' +
                '<!-- Please describe what actually happened -->\n\n\n' +
                '### Additional Context\n' +
                '<!-- Add any other context about the problem here -->\n';

            // Create GitHub issue URL with pre-filled content
            const repoUrl = 'https://github.com/cs01/llvm-project';
            const issueUrl = new URL(repoUrl + '/issues/new');
            issueUrl.searchParams.set('title', '[Playground Bug] ');
            issueUrl.searchParams.set('body', issueBody);
            issueUrl.searchParams.set('labels', 'bug,playground');

            // Open in new tab
            window.open(issueUrl.toString(), '_blank');
            showToast('Opening bug report in GitHub...');
        });

        // Load code from URL on page load
        function loadCodeFromURL() {
            const urlParams = new URLSearchParams(window.location.search);

            // Priority 1: Load from ?code= (user shared code)
            const encodedCode = urlParams.get('code');
            if (encodedCode) {
                try {
                    const code = decodeURIComponent(escape(atob(encodedCode)));
                    setEditorValue(code);

                    // Set dropdown to "Custom Code" or first option to indicate custom code
                    examplesSelect.value = '';

                    showToast('Code loaded from shared link!');
                    return; // Stop here, don't load example
                } catch (err) {
                    console.error('Failed to decode URL code:', err);
                    showToast('Failed to load code from URL');
                }
            }

            // Priority 2: Load from ?example= (example number)
            const exampleParam = urlParams.get('example');
            if (exampleParam) {
                // Convert number to example name
                const exampleName = exampleNumbers[exampleParam];
                if (exampleName && examples[exampleName]) {
                    setEditorValue(examples[exampleName]);
                    examplesSelect.value = exampleName;
                    return; // Stop here
                }
            }

            // Priority 3: Default to first example (null-check)
            if (examples['null-check']) {
                setEditorValue(examples['null-check']);
                examplesSelect.value = 'null-check';
            }
        }

        async function initCompiler() {
            setStatus('Loading compiler...', true, 0);
            status.className = 'status';
            loadingBar.classList.add('active');

            let loaded = 0;

            try {
                // Determine script URL
                const isDev = window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1';

                // Allow testing production URLs on localhost with ?prod=1
                const urlParams = new URLSearchParams(window.location.search);
                const forceProd = urlParams.get('prod') === '1' || urlParams.get('mode') === 'prod';

                if (isDev && !forceProd) {
                    // Local development - files should be in the same directory
                    scriptUrl = './clang.js';
                    var wasmUrl = './clang.wasm';
                    console.log('🔧 Development mode: Using local files');
                } else {
                    // Production - files are served from GitHub Pages (same origin, no CORS issues!)
                    scriptUrl = './clang.js';
                    var wasmUrl = './clang.wasm';
                    console.log('🚀 Production mode: Using files from GitHub Pages');
                }

                // Pre-load the WASM binary ONCE with progress tracking
                console.log('Downloading WASM binary...');
                const wasmResponse = await fetch(wasmUrl);

                if (!wasmResponse.ok) {
                    throw new Error(`Failed to fetch WASM: ${wasmResponse.status} ${wasmResponse.statusText}`);
                }

                const contentLength = wasmResponse.headers.get('content-length');
                const total = parseInt(contentLength, 10);

                // Update status with actual size if known
                if (total) {
                    const sizeMB = (total / (1024 * 1024)).toFixed(0);
                    setStatus(`Loading compiler (${sizeMB}MB)...`, true, 0);
                }

                const reader = wasmResponse.body.getReader();
                const chunks = [];

                while (true) {
                    const { done, value } = await reader.read();
                    if (done) break;

                    chunks.push(value);
                    loaded += value.length;

                    if (total) {
                        const percent = (loaded / total) * 100;
                        updateProgress(percent);
                    }
                }

                // Combine chunks into final array buffer
                const chunksAll = new Uint8Array(loaded);
                let position = 0;
                for (const chunk of chunks) {
                    chunksAll.set(chunk, position);
                    position += chunk.length;
                }
                wasmBinary = chunksAll.buffer;
                console.log('WASM binary loaded:', wasmBinary.byteLength, 'bytes');

                // Now test with a worker to get the version
                const versionResult = await compileCode('int main() { return 0; }', ['--version']);
                const versionOutput = versionResult.stdout + versionResult.stderr;
                const versionMatch = versionOutput.match(/clang version ([^\s]+)/);
                if (versionMatch) {
                    clangVersion = versionMatch[1];
                    document.querySelector('.output-section-header').textContent =
                        `Null-Safe Clang ${clangVersion}`;
                }

                status.textContent = 'Ready to compile';
                status.className = 'status ready';
                status.title = '';
                compileBtn.disabled = false;
                loadingBar.classList.remove('active');
                showToast('Compiler loaded successfully!');

                setTimeout(() => compile(), 500);

            } catch (error) {
                console.error('Failed to load Clang WASM:', error);

                // Provide a helpful error message
                let errorMessage = 'Failed to load compiler';
                let toastMessage = 'Failed to load compiler';

                if (error.message && error.message.includes('Failed to fetch')) {
                    errorMessage = 'Network error - Cannot download compiler';
                    toastMessage = 'Network error: Unable to download the compiler. Please check your internet connection.';
                } else if (error.message && (error.message.includes('404') || error.message.includes('File not found'))) {
                    errorMessage = 'Compiler files not found';
                    toastMessage = 'Compiler files not found. The WASM binaries are missing from the server.';
                } else if (loaded === 0 || (wasmBinary && wasmBinary.byteLength === 0)) {
                    errorMessage = 'Download failed - 0 bytes received';
                    toastMessage = 'Failed to download compiler: Received 0 bytes. Please check your internet connection and try refreshing the page.';
                } else {
                    toastMessage = `Failed to load compiler: ${error.message}`;
                }

                status.textContent = errorMessage;
                status.className = 'status error';
                status.title = '';
                loadingBar.classList.remove('active');
                showToast(toastMessage, 6000);
            }
        }

        async function compile() {
            if (isCompiling) return;
            if (!scriptUrl) {
                outputNullsafe.innerHTML = `<span class="error">Compiler not loaded yet. Please wait...</span>`;
                outputMainline.innerHTML = `<span class="error">Compiler not loaded yet. Please wait...</span>`;
                return;
            }

            isCompiling = true;
            compileBtn.disabled = true;
            status.textContent = 'Compiling...';
            status.className = 'status compiling';
            outputNullsafe.innerHTML = '';
            outputMainline.innerHTML = '';
            loadingBar.classList.add('active');

            const startTime = performance.now();

            try {
                const code = getEditorValue();

                // Compile both versions in parallel
                const [nullsafeResult, mainlineResult] = await Promise.all([
                    compileCode(code, []),
                    compileCode(code, ['-Wno-nullability'])
                ]);

                const duration = (performance.now() - startTime).toFixed(0);

                // Build command strings
                const baseArgs = '-fsyntax-only --target=wasm32-unknown-emscripten';
                const nullsafeCmd = `$ clang ${baseArgs} input.c`;
                const mainlineCmd = `$ clang ${baseArgs} -Wno-nullability input.c`;

                // Update headers with clearer titles and timing
                const headers = document.querySelectorAll('.output-section-header');
                headers[0].innerHTML = `<strong>With</strong> Null Warnings (clang v${clangVersion}) <span style="float: right; font-size: 11px; opacity: 0.6;">${duration}ms</span>`;
                headers[1].innerHTML = `<strong>Without</strong> Null Warnings (clang v${clangVersion}) <span style="float: right; font-size: 11px; opacity: 0.6;">${duration}ms</span>`;

                // Count nullability warnings to detect missed bugs
                const nullWarningCount = (nullsafeResult.stderr.match(/\[-Wnullability\]/g) || []).length;

                // Display null-safe results with command
                if (nullsafeResult.stdout || nullsafeResult.stderr) {
                    outputNullsafe.textContent = nullsafeCmd + '\n' + nullsafeResult.stderr + nullsafeResult.stdout;
                } else {
                    outputNullsafe.textContent = nullsafeCmd + '\n✓ No errors or warnings';
                }

                // Display mainline results with command and comparison
                if (mainlineResult.stdout || mainlineResult.stderr) {
                    outputMainline.textContent = mainlineCmd + '\n' + mainlineResult.stderr + mainlineResult.stdout;
                } else {
                    // No warnings suppressed, but check if null-safe had warnings
                    if (nullWarningCount > 0) {
                        outputMainline.textContent = mainlineCmd + '\n✓ No errors or warnings\n\n⚠️  Missed ' + nullWarningCount + ' null safety bug' + (nullWarningCount !== 1 ? 's' : '') + ' (see above)';
                    } else {
                        outputMainline.textContent = mainlineCmd + '\n✓ No errors or warnings';
                    }
                }

                status.textContent = 'Ready to compile';
                status.className = 'status ready';
            } catch (error) {
                const errorMsg = error.message || String(error);
                outputNullsafe.innerHTML = `<span class="error">Compilation failed: ${errorMsg}\n\nCheck console for details.</span>`;
                outputMainline.innerHTML = `<span class="error">Compilation failed: ${errorMsg}\n\nCheck console for details.</span>`;
                status.textContent = 'Compilation error';
                status.className = 'status error';
                console.error('Compilation error:', error);
            } finally {
                isCompiling = false;
                compileBtn.disabled = false;
                loadingBar.classList.remove('active');
            }
        }

        async function compileCode(code, extraFlags = []) {
            return new Promise((resolve, reject) => {
                const worker = new Worker('compiler-worker.js');

                let stdout = '';
                let stderr = '';
                let completed = false;

                const timeout = setTimeout(() => {
                    if (!completed) {
                        worker.terminate();
                        reject(new Error('Compilation timeout'));
                    }
                }, 30000);

                worker.onmessage = function(e) {
                    const { type, text, error, exitCode } = e.data;

                    if (type === 'ready') {
                        // Worker is ready, send compile request
                        worker.postMessage({
                            type: 'compile',
                            code,
                            extraFlags
                        });
                    } else if (type === 'stdout') {
                        stdout += text + '\n';
                    } else if (type === 'stderr') {
                        stderr += text + '\n';
                    } else if (type === 'complete') {
                        completed = true;
                        clearTimeout(timeout);
                        worker.terminate();
                        resolve({ stdout, stderr, exitCode });
                    } else if (type === 'error') {
                        completed = true;
                        clearTimeout(timeout);
                        worker.terminate();
                        reject(new Error(error));
                    }
                };

                worker.onerror = function(error) {
                    completed = true;
                    clearTimeout(timeout);
                    worker.terminate();
                    reject(error);
                };

                // Send the pre-loaded WASM binary to the worker
                worker.postMessage({
                    type: 'load',
                    scriptUrl,
                    wasmBinary  // Share the pre-loaded WASM
                });
            });
        }

        compileBtn.addEventListener('click', compile);

        // Auto-compile on load
        window.addEventListener('load', async () => {
            await loadExamples();  // Load all examples from files
            await initMonaco();  // Initialize Monaco editor first
            loadCodeFromURL();   // Load code from URL if present
            initCompiler();
        });
