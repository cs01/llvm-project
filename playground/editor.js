// Small dependency-free code editor for the playground. A transparent
// <textarea> handles input, selection and undo; a syntax-highlighted <pre>
// sits underneath it, a third layer draws wavy underlines for compiler
// diagnostics, and a gutter shows line numbers. All layers share one font and
// line height and scroll together.
(function () {
    const KEYWORDS = new Set([
        'alignas', 'alignof', 'auto', 'break', 'case', 'catch', 'class', 'const',
        'consteval', 'constexpr', 'constinit', 'const_cast', 'continue', 'decltype',
        'default', 'delete', 'do', 'dynamic_cast', 'else', 'enum', 'explicit',
        'export', 'extern', 'final', 'for', 'friend', 'goto', 'if', 'inline',
        'mutable', 'namespace', 'new', 'noexcept', 'operator', 'override',
        'private', 'protected', 'public', 'register', 'reinterpret_cast',
        'requires', 'restrict', 'return', 'sizeof', 'static', 'static_assert',
        'static_cast', 'struct', 'switch', 'template', 'this', 'throw', 'try',
        'typedef', 'typeid', 'typename', 'union', 'using', 'virtual', 'volatile',
        'while', 'true', 'false', 'nullptr', 'NULL',
    ]);
    const TYPES = new Set([
        'void', 'bool', '_Bool', 'char', 'short', 'int', 'long', 'float', 'double',
        'signed', 'unsigned', 'size_t', 'ssize_t', 'ptrdiff_t', 'intptr_t',
        'uintptr_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t',
        'uint16_t', 'uint32_t', 'uint64_t', 'wchar_t', 'char8_t', 'char16_t',
        'char32_t', 'FILE', 'std',
    ]);
    const NULLABLE = new Set(['_Nullable', '_Null_unspecified']);
    const NONNULL = new Set(['_Nonnull']);

    const TOKEN = /(\/\/[^\n]*)|(\/\*[\s\S]*?(?:\*\/|$))|("(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?)|(^[ \t]*#[^\n]*)|(\b\d[\w.']*)|([A-Za-z_]\w*)/gm;

    function escapeHtml(text) {
        return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function span(cls, text) {
        return `<span class="tok-${cls}">${escapeHtml(text)}</span>`;
    }

    function highlight(code) {
        let html = '';
        let last = 0;
        TOKEN.lastIndex = 0;
        let m;
        while ((m = TOKEN.exec(code)) !== null) {
            if (m[0] === '') {
                TOKEN.lastIndex++;
                continue;
            }
            html += escapeHtml(code.slice(last, m.index));
            const [text, line, block, str, pre, num, word] = m;
            if (line || block) html += span('comment', text);
            else if (str) html += span('string', text);
            else if (pre) html += span('preproc', text);
            else if (num) html += span('number', text);
            else if (NULLABLE.has(word)) html += span('nullable', text);
            else if (NONNULL.has(word)) html += span('nonnull', text);
            else if (KEYWORDS.has(word)) html += span('keyword', text);
            else if (TYPES.has(word)) html += span('type', text);
            else html += escapeHtml(text);
            last = m.index + text.length;
        }
        return html + escapeHtml(code.slice(last));
    }

    function create(container, { value = '', tabSize = 4 } = {}) {
        container.classList.add('code-editor');
        container.innerHTML = `
            <div class="ce-gutter"><div class="ce-gutter-inner"></div></div>
            <div class="ce-main">
                <pre class="ce-layer ce-highlight" aria-hidden="true"></pre>
                <pre class="ce-layer ce-markers" aria-hidden="true"></pre>
                <textarea class="ce-input" spellcheck="false" autocapitalize="off"
                    autocomplete="off" autocorrect="off" wrap="off"
                    aria-label="Source code"></textarea>
                <div class="ce-tooltip" role="tooltip"></div>
            </div>`;
        const gutter = container.querySelector('.ce-gutter-inner');
        const main = container.querySelector('.ce-main');
        const highlightLayer = container.querySelector('.ce-highlight');
        const markerLayer = container.querySelector('.ce-markers');
        const input = container.querySelector('.ce-input');
        const tooltip = container.querySelector('.ce-tooltip');
        const indent = ' '.repeat(tabSize);

        const changeListeners = [];
        let runHandler = null;
        let markers = [];

        function renderText() {
            const code = input.value;
            highlightLayer.innerHTML = highlight(code) + '\n';
            const lineCount = code.split('\n').length;
            if (gutter.childElementCount !== lineCount) {
                let nums = '';
                for (let i = 1; i <= lineCount; i++) nums += `<div>${i}</div>`;
                gutter.innerHTML = nums;
            }
        }

        function markerEnd(lineText, column) {
            const rest = lineText.slice(column - 1);
            const word = rest.match(/^\w+/);
            return column + (word ? word[0].length : 1);
        }

        function renderMarkers() {
            const lines = input.value.split('\n');
            const byLine = new Map();
            for (const marker of markers) {
                if (marker.severity === 'note' || marker.line < 1 || marker.line > lines.length) continue;
                if (!byLine.has(marker.line)) byLine.set(marker.line, []);
                byLine.get(marker.line).push(marker);
            }
            let html = '';
            lines.forEach((lineText, i) => {
                const lineMarkers = (byLine.get(i + 1) || []).sort((a, b) => a.column - b.column);
                let pos = 1;
                for (const marker of lineMarkers) {
                    const start = Math.max(marker.column, pos);
                    const end = markerEnd(lineText, start);
                    html += escapeHtml(lineText.slice(pos - 1, start - 1));
                    html += `<span class="ce-squiggle ce-${marker.severity}">${escapeHtml(lineText.slice(start - 1, end - 1) || ' ')}</span>`;
                    pos = end;
                }
                html += escapeHtml(lineText.slice(pos - 1)) + '\n';
            });
            markerLayer.innerHTML = html;
            [...gutter.children].forEach((el, i) => {
                const lineMarkers = byLine.get(i + 1);
                el.className = lineMarkers
                    ? (lineMarkers.some(m => m.severity === 'error') ? 'ce-line-error' : 'ce-line-warning')
                    : '';
            });
        }

        function syncScroll() {
            const transform = `translate(${-input.scrollLeft}px, ${-input.scrollTop}px)`;
            highlightLayer.style.transform = transform;
            markerLayer.style.transform = transform;
            gutter.style.transform = `translateY(${-input.scrollTop}px)`;
        }

        function emitChange() {
            renderText();
            renderMarkers();
            changeListeners.forEach(fn => fn());
        }

        function insertText(text) {
            if (!document.execCommand('insertText', false, text)) {
                input.setRangeText(text, input.selectionStart, input.selectionEnd, 'end');
                emitChange();
            }
        }

        input.addEventListener('input', emitChange);
        input.addEventListener('scroll', syncScroll);
        input.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
                e.preventDefault();
                if (runHandler) runHandler();
            } else if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey) {
                e.preventDefault();
                insertText(indent);
            } else if (e.key === 'Enter' && !e.shiftKey && !e.altKey) {
                e.preventDefault();
                const before = input.value.slice(0, input.selectionStart);
                const currentLine = before.slice(before.lastIndexOf('\n') + 1);
                let lead = currentLine.match(/^[ \t]*/)[0];
                if (/\{\s*$/.test(currentLine)) lead += indent;
                insertText('\n' + lead);
            } else if (e.key === '}' && input.selectionStart === input.selectionEnd) {
                const before = input.value.slice(0, input.selectionStart);
                const currentLine = before.slice(before.lastIndexOf('\n') + 1);
                if (currentLine.length >= tabSize && currentLine.endsWith(indent) && /^[ \t]*$/.test(currentLine)) {
                    e.preventDefault();
                    input.setSelectionRange(input.selectionStart - tabSize, input.selectionStart);
                    insertText('}');
                }
            }
        });

        function metrics() {
            const style = getComputedStyle(input);
            const probe = document.createElement('span');
            probe.textContent = 'M'.repeat(100);
            probe.style.cssText = `font:${style.font};position:absolute;visibility:hidden;white-space:pre`;
            document.body.appendChild(probe);
            const charWidth = probe.getBoundingClientRect().width / 100;
            probe.remove();
            return {
                charWidth,
                lineHeight: parseFloat(style.lineHeight),
                padLeft: parseFloat(style.paddingLeft),
                padTop: parseFloat(style.paddingTop),
            };
        }

        input.addEventListener('mousemove', (e) => {
            const shown = markers.filter(m => m.severity !== 'note');
            if (shown.length === 0) {
                tooltip.classList.remove('visible');
                return;
            }
            const rect = input.getBoundingClientRect();
            const { charWidth, lineHeight, padLeft, padTop } = metrics();
            const line = Math.floor((e.clientY - rect.top - padTop + input.scrollTop) / lineHeight) + 1;
            const column = Math.floor((e.clientX - rect.left - padLeft + input.scrollLeft) / charWidth) + 1;
            const lineText = input.value.split('\n')[line - 1] || '';
            const hits = shown.filter(m => m.line === line && column >= m.column && column < markerEnd(lineText, m.column));
            if (hits.length === 0) {
                tooltip.classList.remove('visible');
                return;
            }
            tooltip.innerHTML = hits.map(m => {
                const notes = markers
                    .filter(n => n.severity === 'note' && n.line === m.line && n.column === m.column)
                    .map(n => `<div class="ce-tooltip-note">note: ${escapeHtml(n.message)}</div>`)
                    .join('');
                return `<div class="ce-tooltip-${m.severity}">${m.severity}: ${escapeHtml(m.message)}</div>${notes}`;
            }).join('');
            const mainRect = main.getBoundingClientRect();
            tooltip.style.left = `${Math.min(e.clientX - mainRect.left + 12, mainRect.width - 20)}px`;
            tooltip.style.top = `${padTop + line * lineHeight - input.scrollTop + 4}px`;
            tooltip.classList.add('visible');
        });
        input.addEventListener('mouseleave', () => tooltip.classList.remove('visible'));

        input.value = value;
        renderText();

        return {
            getValue: () => input.value,
            setValue(text) {
                input.value = text;
                input.scrollTop = 0;
                input.scrollLeft = 0;
                syncScroll();
                emitChange();
            },
            onChange: fn => changeListeners.push(fn),
            onRun: fn => { runHandler = fn; },
            setMarkers(next) {
                markers = next;
                renderMarkers();
            },
            focus: () => input.focus(),
        };
    }

    window.CodeEditor = { create, highlight };
})();
