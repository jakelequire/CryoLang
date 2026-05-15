import * as vscode from 'vscode';

/**
 * Decoration-based renderer for `cryo-diagnostic:` virtual documents.
 *
 * The TextMate grammar in `syntaxes/cryo-diag.tmGrammar.json` provides
 * baseline scope tagging so the document still reads in users who
 * haven't installed this extension's decorations, but TextMate colors
 * are entirely theme-driven — and themes are wildly inconsistent about
 * niche scopes like `markup.error` or `keyword.other.severity.help`.
 *
 * To get guaranteed rustc-quality output we layer
 * `vscode.TextEditorDecorationType` ranges on top: each role (severity
 * tag, error code chip, caret, label caption, inline backticked code,
 * line-number gutter, arrow, file link, …) has its own decoration with
 * an explicit `ThemeColor` reference.  The colors themselves live in
 * `package.json` under `contributes.colors`, with separate dark / light
 * / high-contrast palettes, so the user's color theme still tints the
 * tone but the *semantic role* of each glyph is preserved.
 */

const CRYO_DIAG_SCHEME = 'cryo-diagnostic';

// ============================================================================
// Decoration types
// ============================================================================

type DecoKey =
    | 'severityError'
    | 'severityWarning'
    | 'severityNote'
    | 'severityHelp'
    | 'severityInfo'
    | 'severitySuggestion'
    | 'errorCodeBracket'
    | 'errorCode'
    | 'separatorColon'
    | 'headline'
    | 'arrow'
    | 'fileLink'
    | 'positionNumber'
    | 'positionColon'
    | 'lineNumber'
    | 'lineNumberActive'
    | 'gutter'
    | 'gutterActive'
    | 'caretPrimaryError'
    | 'caretPrimaryWarning'
    | 'caretPrimaryNote'
    | 'caretPrimaryHelp'
    | 'caretSecondary'
    | 'caretReplace'
    | 'caretInsert'
    | 'caretDelete'
    | 'labelPrimaryError'
    | 'labelPrimaryWarning'
    | 'labelPrimaryNote'
    | 'labelPrimaryHelp'
    | 'labelSecondary'
    | 'labelReplace'
    | 'labelInsert'
    | 'inlineCode'
    | 'ellipsis'
    | 'ruleLine'
    | 'applicabilityChip'
    | 'summaryError'
    | 'summaryWarning';

function makeDecorations(): Record<DecoKey, vscode.TextEditorDecorationType> {
    const create = (
        opts: vscode.DecorationRenderOptions
    ): vscode.TextEditorDecorationType =>
        vscode.window.createTextEditorDecorationType(opts);

    const themeFg = (id: string) => new vscode.ThemeColor(id);

    const sev = (id: string): vscode.DecorationRenderOptions => ({
        color: themeFg(id),
        fontWeight: 'bold',
    });

    const caret = (id: string): vscode.DecorationRenderOptions => ({
        color: themeFg(id),
        fontWeight: 'bold',
    });

    const label = (id: string): vscode.DecorationRenderOptions => ({
        color: themeFg(id),
        fontStyle: 'italic',
    });

    return {
        severityError:       create(sev('cryo.diag.error')),
        severityWarning:     create(sev('cryo.diag.warning')),
        severityNote:        create(sev('cryo.diag.note')),
        severityHelp:        create(sev('cryo.diag.help')),
        severityInfo:        create(sev('cryo.diag.info')),
        severitySuggestion:  create(sev('cryo.diag.suggestion')),

        errorCodeBracket:    create({
            color: themeFg('cryo.diag.errorCodeBracket'),
        }),
        errorCode:           create({
            color: themeFg('cryo.diag.errorCode'),
            fontWeight: 'bold',
        }),

        separatorColon:      create({
            color: themeFg('cryo.diag.separator'),
            fontWeight: 'bold',
        }),

        headline:            create({
            color: themeFg('cryo.diag.headline'),
            fontWeight: 'bold',
        }),

        arrow:               create({
            color: themeFg('cryo.diag.arrow'),
            fontWeight: 'bold',
        }),
        fileLink:            create({
            color: themeFg('cryo.diag.fileLink'),
            textDecoration: 'underline',
        }),
        positionNumber:      create({
            color: themeFg('cryo.diag.position'),
        }),
        positionColon:       create({
            color: themeFg('cryo.diag.gutter'),
        }),

        lineNumber:          create({
            color: themeFg('cryo.diag.lineNumber'),
        }),
        lineNumberActive:    create({
            color: themeFg('cryo.diag.lineNumberActive'),
            fontWeight: 'bold',
        }),
        gutter:              create({
            color: themeFg('cryo.diag.gutter'),
        }),
        gutterActive:        create({
            color: themeFg('cryo.diag.gutterActive'),
            fontWeight: 'bold',
        }),

        caretPrimaryError:   create(caret('cryo.diag.error')),
        caretPrimaryWarning: create(caret('cryo.diag.warning')),
        caretPrimaryNote:    create(caret('cryo.diag.note')),
        caretPrimaryHelp:    create(caret('cryo.diag.help')),
        caretSecondary:      create(caret('cryo.diag.note')),
        caretReplace:        create(caret('cryo.diag.suggestion')),
        caretInsert:         create(caret('cryo.diag.suggestion')),
        caretDelete:         create(caret('cryo.diag.error')),

        labelPrimaryError:   create(label('cryo.diag.error')),
        labelPrimaryWarning: create(label('cryo.diag.warning')),
        labelPrimaryNote:    create(label('cryo.diag.note')),
        labelPrimaryHelp:    create(label('cryo.diag.help')),
        labelSecondary:      create(label('cryo.diag.note')),
        labelReplace:        create(label('cryo.diag.suggestion')),
        labelInsert:         create(label('cryo.diag.suggestion')),

        inlineCode:          create({
            color: themeFg('cryo.diag.inlineCode'),
            fontWeight: 'bold',
        }),

        ellipsis:            create({
            color: themeFg('cryo.diag.gutter'),
        }),
        ruleLine:            create({
            color: themeFg('cryo.diag.rule'),
            fontWeight: 'bold',
        }),
        applicabilityChip:   create({
            color: themeFg('cryo.diag.lineNumber'),
            fontStyle: 'italic',
        }),
        summaryError:        create(sev('cryo.diag.error')),
        summaryWarning:      create(sev('cryo.diag.warning')),
    };
}

// ============================================================================
// Tokenizer
// ============================================================================

type Severity = 'error' | 'warning' | 'note' | 'help' | 'info';

const SEVERITY_HEADER_RE =
    /^(error|warning|note|help|info)(?:(\[)([A-Z]\d+)(\]))?(:)\s*(.*)$/;

const ARROW_PRIMARY_RE =
    /^(\s*)(-->)(\s+)(\S+?)(:)(\d+)(:)(\d+)\s*$/;

const ARROW_SECONDARY_RE =
    /^(\s*)(::)(\s+)(\S+?)(:)(\d+)(:)(\d+)\s*$/;

const ELLIPSIS_RE = /^\s*\.\.\.\s*$/;

const RULE_LINE_RE = /^={5,}$/;

const CHILD_HEADER_RE = /^(\s+)(note|help|info)(:)\s*(.*)$/;

const SUGGESTION_HEADER_RE =
    /^(\s*)(suggestion)(\s+\[[^\]]+\])?(:)\s*(.*)$/;

const SUMMARY_RE =
    /^(aborting due to \d+ errors?)(?:(; )(\d+ warnings? emitted))?$/;

const SNIPPET_LINE_RE = /^(\s*)(\d+)(\s)(\|)(\s?)/;

const MARGIN_ONLY_RE = /^(\s*)(\|)\s*$/;

const MARGIN_WITH_CARETS_RE = /^(\s*)(\|)(\s+)(.*)$/;

const INLINE_CODE_RE = /`[^`\n]+`/g;

interface Buckets {
    map: Map<DecoKey, vscode.Range[]>;
    push(key: DecoKey, range: vscode.Range): void;
}

function makeBuckets(): Buckets {
    const map = new Map<DecoKey, vscode.Range[]>();
    return {
        map,
        push(key, range) {
            let arr = map.get(key);
            if (!arr) {
                arr = [];
                map.set(key, arr);
            }
            arr.push(range);
        },
    };
}

function rangeOf(line: number, start: number, end: number): vscode.Range {
    return new vscode.Range(
        new vscode.Position(line, start),
        new vscode.Position(line, end)
    );
}

interface Ctx {
    severity: Severity; // tracks the most recently opened diagnostic
    inSuggestion: boolean;
}

function severityDecoForCaret(sev: Severity): DecoKey {
    switch (sev) {
        case 'error':   return 'caretPrimaryError';
        case 'warning': return 'caretPrimaryWarning';
        case 'note':    return 'caretPrimaryNote';
        case 'help':    return 'caretPrimaryHelp';
        case 'info':    return 'caretPrimaryNote';
    }
}

function severityDecoForLabel(sev: Severity): DecoKey {
    switch (sev) {
        case 'error':   return 'labelPrimaryError';
        case 'warning': return 'labelPrimaryWarning';
        case 'note':    return 'labelPrimaryNote';
        case 'help':    return 'labelPrimaryHelp';
        case 'info':    return 'labelPrimaryNote';
    }
}

function tokenizeInlineCode(
    buckets: Buckets,
    lineNum: number,
    text: string,
    baseStart: number
): void {
    INLINE_CODE_RE.lastIndex = 0;
    let m: RegExpExecArray | null;
    while ((m = INLINE_CODE_RE.exec(text)) !== null) {
        const start = baseStart + m.index;
        const end = start + m[0].length;
        buckets.push('inlineCode', rangeOf(lineNum, start, end));
    }
}

/**
 * Walk a margin line's tail to find the caret runs and the inline
 * label.  The renderer emits, in left-to-right order:
 *
 *   `^^^^`   primary caret  (severity color)
 *   `----`   secondary caret OR delete glyph
 *   `~~~~`   suggestion replace
 *   `++++`   suggestion insert
 *   `|`      connector pipe joining a label below to its caret above
 *
 * Plus optional trailing label text.  We classify each run by its
 * leading character, then attribute the inline label (if any) to the
 * RIGHTMOST run's kind — that's the rustc convention: the caption sits
 * next to the right-most caret in the line.
 */
function tokenizeMarginTail(
    buckets: Buckets,
    ctx: Ctx,
    lineNum: number,
    fullLine: string,
    tailStart: number
): void {
    const tail = fullLine.slice(tailStart);
    if (tail.length === 0) return;

    type RunKind = 'primary' | 'secondary' | 'replace' | 'insert' | 'pipe';

    interface Run {
        kind: RunKind;
        start: number; // doc-line column
        end: number;
    }

    const runs: Run[] = [];
    let i = 0;
    while (i < tail.length) {
        const ch = tail[i];
        if (ch === ' ' || ch === '\t') {
            i += 1;
            continue;
        }
        if (ch === '^') {
            // Primary caret.  `render_underline` (single-span path)
            // emits one `^` followed by tildes to fill the span width
            // — the tildes are NOT a suggestion replace, they're
            // primary-caret continuation. Gobble them so the whole run
            // stays one primary run.
            let j = i;
            while (j < tail.length && (tail[j] === '^' || tail[j] === '~')) {
                j += 1;
            }
            runs.push({
                kind: 'primary',
                start: tailStart + i,
                end: tailStart + j,
            });
            i = j;
            continue;
        }
        if (ch === '-') {
            let j = i;
            while (j < tail.length && tail[j] === '-') j += 1;
            runs.push({
                kind: 'secondary',
                start: tailStart + i,
                end: tailStart + j,
            });
            i = j;
            continue;
        }
        if (ch === '~') {
            // Standalone tilde run — only happens inside a suggestion
            // block; primary-span tildes already got absorbed by the
            // `^` branch above.
            let j = i;
            while (j < tail.length && tail[j] === '~') j += 1;
            runs.push({
                kind: 'replace',
                start: tailStart + i,
                end: tailStart + j,
            });
            i = j;
            continue;
        }
        if (ch === '+') {
            let j = i;
            while (j < tail.length && tail[j] === '+') j += 1;
            runs.push({
                kind: 'insert',
                start: tailStart + i,
                end: tailStart + j,
            });
            i = j;
            continue;
        }
        if (ch === '|') {
            runs.push({
                kind: 'pipe',
                start: tailStart + i,
                end: tailStart + i + 1,
            });
            i += 1;
            continue;
        }
        // Anything else terminates the glyph zone — what's left is the
        // label caption.
        break;
    }

    // Decorate each run with its caret style.
    //
    // Connector pipes (`|` glyphs in the margin tail) always render in
    // the secondary tone — they're the stitch between a stacked
    // secondary caret and its caption row.  Rustc convention: primary
    // labels go inline, so the only `|` that ever survives to the
    // margin tail belongs to a secondary label.
    for (const r of runs) {
        const rng = rangeOf(lineNum, r.start, r.end);
        switch (r.kind) {
            case 'primary':
                buckets.push(severityDecoForCaret(ctx.severity), rng);
                break;
            case 'secondary':
                buckets.push('caretSecondary', rng);
                break;
            case 'replace':
                buckets.push('caretReplace', rng);
                break;
            case 'insert':
                buckets.push('caretInsert', rng);
                break;
            case 'pipe':
                buckets.push('caretSecondary', rng);
                break;
        }
    }

    // Label caption: everything from `i` onwards (after leading spaces
    // were already eaten in the loop).
    if (i < tail.length) {
        const labelStart = tailStart + i;
        const labelText = tail.slice(i);
        const labelEnd = labelStart + labelText.length;
        // Trim trailing whitespace so the decoration doesn't pad past
        // the real text.
        let trim = labelEnd;
        while (
            trim > labelStart &&
            (fullLine[trim - 1] === ' ' || fullLine[trim - 1] === '\t')
        ) {
            trim -= 1;
        }
        if (trim > labelStart) {
            const last = runs.length > 0 ? runs[runs.length - 1] : undefined;
            let labelKey: DecoKey;
            if (!last) {
                // No caret runs preceded the caption → this is a
                // stacked caption row from `render_stacked_two_labels`.
                // Default to the secondary tone: the renderer's left
                // label (the one needing a connector + caption pair)
                // is almost always the secondary, and matching the
                // connector pipe's color above keeps the column
                // visually coherent.
                labelKey = 'labelSecondary';
            } else if (last.kind === 'primary') {
                labelKey = severityDecoForLabel(ctx.severity);
            } else if (last.kind === 'secondary' || last.kind === 'pipe') {
                labelKey = 'labelSecondary';
            } else if (last.kind === 'replace') {
                labelKey = 'labelReplace';
            } else {
                labelKey = 'labelInsert';
            }
            buckets.push(labelKey, rangeOf(lineNum, labelStart, trim));
            // Inline backtick code overlays on top.
            tokenizeInlineCode(
                buckets,
                lineNum,
                labelText.slice(0, trim - labelStart),
                labelStart
            );
        }
    }
}

function tokenizeDocument(doc: vscode.TextDocument): Map<DecoKey, vscode.Range[]> {
    const buckets = makeBuckets();
    const ctx: Ctx = { severity: 'error', inSuggestion: false };

    const lineCount = doc.lineCount;
    for (let ln = 0; ln < lineCount; ln += 1) {
        const lineText = doc.lineAt(ln).text;

        // Rule line:  =========
        if (RULE_LINE_RE.test(lineText)) {
            buckets.push('ruleLine', rangeOf(ln, 0, lineText.length));
            continue;
        }

        // Severity header:  error[E0200]: message
        let m: RegExpExecArray | null;
        m = SEVERITY_HEADER_RE.exec(lineText);
        if (m && m.index === 0) {
            const sev = m[1] as Severity;
            ctx.severity = sev;
            ctx.inSuggestion = false;

            const sevKey: DecoKey =
                sev === 'error'   ? 'severityError'   :
                sev === 'warning' ? 'severityWarning' :
                sev === 'note'    ? 'severityNote'    :
                sev === 'help'    ? 'severityHelp'    :
                                    'severityInfo';
            buckets.push(sevKey, rangeOf(ln, 0, m[1].length));

            let cursor = m[1].length;
            if (m[2] !== undefined) {
                buckets.push('errorCodeBracket', rangeOf(ln, cursor, cursor + 1));
                cursor += 1;
                buckets.push('errorCode', rangeOf(ln, cursor, cursor + m[3].length));
                cursor += m[3].length;
                buckets.push('errorCodeBracket', rangeOf(ln, cursor, cursor + 1));
                cursor += 1;
            }
            buckets.push('separatorColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;

            // Skip the single space after `:` then mark the rest as headline.
            const tail = lineText.slice(cursor);
            const leadingSpaces = tail.length - tail.trimStart().length;
            const headlineStart = cursor + leadingSpaces;
            const headlineEnd = lineText.length;
            if (headlineEnd > headlineStart) {
                buckets.push('headline', rangeOf(ln, headlineStart, headlineEnd));
                tokenizeInlineCode(
                    buckets,
                    ln,
                    lineText.slice(headlineStart, headlineEnd),
                    headlineStart
                );
            }
            continue;
        }

        // Suggestion header:  suggestion [machine-applicable]: message
        m = SUGGESTION_HEADER_RE.exec(lineText);
        if (m && m.index === 0) {
            ctx.inSuggestion = true;
            // Don't override ctx.severity — suggestion lives under a
            // parent diagnostic and its caret glyphs (~, +) carry their
            // own coloring.
            const pad = m[1] ?? '';
            let cursor = pad.length;
            buckets.push(
                'severitySuggestion',
                rangeOf(ln, cursor, cursor + m[2].length)
            );
            cursor += m[2].length;
            if (m[3] !== undefined) {
                buckets.push(
                    'applicabilityChip',
                    rangeOf(ln, cursor, cursor + m[3].length)
                );
                cursor += m[3].length;
            }
            buckets.push('separatorColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;
            const tail = lineText.slice(cursor);
            const leadingSpaces = tail.length - tail.trimStart().length;
            const headlineStart = cursor + leadingSpaces;
            const headlineEnd = lineText.length;
            if (headlineEnd > headlineStart) {
                buckets.push('headline', rangeOf(ln, headlineStart, headlineEnd));
                tokenizeInlineCode(
                    buckets,
                    ln,
                    lineText.slice(headlineStart, headlineEnd),
                    headlineStart
                );
            }
            continue;
        }

        // Child header:  (whitespace)note: message
        m = CHILD_HEADER_RE.exec(lineText);
        if (m && m.index === 0) {
            const childSev = m[2] as Severity;
            const sevKey: DecoKey =
                childSev === 'note' ? 'severityNote' :
                childSev === 'help' ? 'severityHelp' :
                                      'severityInfo';
            const cursor = m[1].length;
            buckets.push(sevKey, rangeOf(ln, cursor, cursor + m[2].length));
            const colonCol = cursor + m[2].length;
            buckets.push('separatorColon', rangeOf(ln, colonCol, colonCol + 1));
            const after = colonCol + 1;
            const tail = lineText.slice(after);
            const leadingSpaces = tail.length - tail.trimStart().length;
            const msgStart = after + leadingSpaces;
            const msgEnd = lineText.length;
            if (msgEnd > msgStart) {
                buckets.push('headline', rangeOf(ln, msgStart, msgEnd));
                tokenizeInlineCode(
                    buckets,
                    ln,
                    lineText.slice(msgStart, msgEnd),
                    msgStart
                );
            }
            continue;
        }

        // Primary arrow:  --> file:line:col
        m = ARROW_PRIMARY_RE.exec(lineText);
        if (m) {
            let cursor = m[1].length;
            buckets.push('arrow', rangeOf(ln, cursor, cursor + 3));
            cursor += 3 + m[3].length;
            buckets.push('fileLink', rangeOf(ln, cursor, cursor + m[4].length));
            cursor += m[4].length;
            buckets.push('positionColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;
            buckets.push('positionNumber', rangeOf(ln, cursor, cursor + m[6].length));
            cursor += m[6].length;
            buckets.push('positionColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;
            buckets.push('positionNumber', rangeOf(ln, cursor, cursor + m[8].length));
            continue;
        }

        // Secondary arrow:  :: file:line:col
        m = ARROW_SECONDARY_RE.exec(lineText);
        if (m) {
            let cursor = m[1].length;
            buckets.push('arrow', rangeOf(ln, cursor, cursor + 2));
            cursor += 2 + m[3].length;
            buckets.push('fileLink', rangeOf(ln, cursor, cursor + m[4].length));
            cursor += m[4].length;
            buckets.push('positionColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;
            buckets.push('positionNumber', rangeOf(ln, cursor, cursor + m[6].length));
            cursor += m[6].length;
            buckets.push('positionColon', rangeOf(ln, cursor, cursor + 1));
            cursor += 1;
            buckets.push('positionNumber', rangeOf(ln, cursor, cursor + m[8].length));
            continue;
        }

        // Ellipsis (between groups):  ...
        if (ELLIPSIS_RE.test(lineText)) {
            buckets.push('ellipsis', rangeOf(ln, 0, lineText.length));
            continue;
        }

        // Snippet line with line number:  N | code
        m = SNIPPET_LINE_RE.exec(lineText);
        if (m && m.index === 0) {
            const padLen = m[1].length;
            const numLen = m[2].length;
            const pipeCol = padLen + numLen + 1; // after num + the space
            // We treat the line number as "active" (bold) when it
            // matches the snippet's caret/header line.  Heuristic: any
            // snippet line that's followed by a margin-with-carets line
            // (containing ^ ~ + -) is the active error line.  Cheap to
            // compute: peek the next line.
            const next = ln + 1 < lineCount ? doc.lineAt(ln + 1).text : '';
            const isActive = /[\^~+\-]/.test(next.slice(0, 200));
            buckets.push(
                isActive ? 'lineNumberActive' : 'lineNumber',
                rangeOf(ln, padLen, padLen + numLen)
            );
            buckets.push(
                isActive ? 'gutterActive' : 'gutter',
                rangeOf(ln, pipeCol, pipeCol + 1)
            );
            // The rest of the line is the source code — leave it to the
            // embedded `source.cryo` grammar.  We still scan for inline
            // backtick code just in case (rare in real source, but free).
            continue;
        }

        // Margin-only line:  "   |"
        //
        // The leftmost pipe is the snippet's vertical gutter — always
        // blue, never severity-tinted.  Rustc convention: the
        // "this line is part of an error" cue is the caret/label
        // color, not the gutter.
        m = MARGIN_ONLY_RE.exec(lineText);
        if (m && m.index === 0) {
            const pipeCol = m[1].length;
            buckets.push('gutter', rangeOf(ln, pipeCol, pipeCol + 1));
            continue;
        }

        // Margin with carets / connector / label:  "   |   ^^^ label"
        m = MARGIN_WITH_CARETS_RE.exec(lineText);
        if (m && m.index === 0) {
            const pipeCol = m[1].length;
            buckets.push('gutter', rangeOf(ln, pipeCol, pipeCol + 1));
            const tailStart = pipeCol + 1;
            tokenizeMarginTail(buckets, ctx, ln, lineText, tailStart);
            continue;
        }

        // Summary line:  aborting due to N errors; M warnings emitted
        m = SUMMARY_RE.exec(lineText);
        if (m && m.index === 0) {
            buckets.push('summaryError', rangeOf(ln, 0, m[1].length));
            if (m[3] !== undefined) {
                const wStart = m[1].length + m[2].length;
                buckets.push('summaryWarning', rangeOf(ln, wStart, wStart + m[3].length));
            }
            continue;
        }

        // Fallback: still highlight any inline backtick code.
        tokenizeInlineCode(buckets, ln, lineText, 0);
    }

    return buckets.map;
}

// ============================================================================
// Manager
// ============================================================================

/**
 * Lifecycle owner for the diagnostic decoration types.  Construct once
 * per extension activation; call `apply` against any text editor whose
 * document is a `cryo-diagnostic:` URI.  The manager keeps a stable set
 * of `TextEditorDecorationType` handles for the life of the extension
 * so decorations survive editor open/close cycles.
 */
export class DiagnosticDecorationManager {
    private readonly decos: Record<DecoKey, vscode.TextEditorDecorationType>;
    private readonly disposables: vscode.Disposable[] = [];

    constructor(context: vscode.ExtensionContext) {
        this.decos = makeDecorations();
        // Decoration types implement Disposable; dispose with the
        // extension to release VS Code-side handles.
        for (const key of Object.keys(this.decos) as DecoKey[]) {
            context.subscriptions.push(this.decos[key]);
        }

        const refreshVisible = () => {
            for (const editor of vscode.window.visibleTextEditors) {
                this.apply(editor);
            }
        };

        this.disposables.push(
            vscode.window.onDidChangeActiveTextEditor((editor) => {
                if (editor) this.apply(editor);
            }),
            vscode.window.onDidChangeVisibleTextEditors(() => refreshVisible()),
            vscode.workspace.onDidChangeTextDocument((e) => {
                if (e.document.uri.scheme !== CRYO_DIAG_SCHEME) return;
                for (const editor of vscode.window.visibleTextEditors) {
                    if (editor.document === e.document) {
                        this.apply(editor);
                    }
                }
            }),
            vscode.workspace.onDidOpenTextDocument(() => refreshVisible())
        );
        for (const d of this.disposables) {
            context.subscriptions.push(d);
        }

        // Initial pass for any cryo-diag editors already open at
        // activation time.
        refreshVisible();
    }

    /**
     * Tokenize the document and push every decoration bucket.  Empty
     * buckets still get a `setDecorations([])` call so stale ranges
     * from previous content are cleared.
     */
    apply(editor: vscode.TextEditor): void {
        if (editor.document.uri.scheme !== CRYO_DIAG_SCHEME) return;
        const buckets = tokenizeDocument(editor.document);
        for (const key of Object.keys(this.decos) as DecoKey[]) {
            editor.setDecorations(this.decos[key], buckets.get(key) ?? []);
        }
    }
}
