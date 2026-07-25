import * as vscode from 'vscode';
import { LanguageClient } from 'vscode-languageclient/node';

/**
 * Inactive-region dimming.
 *
 * The server strips declarations gated to another OS (`![config(windows)]`
 * on a Linux build, etc.) before any downstream pass sees them, so those
 * functions/blocks would otherwise render as normal, active code. To mark
 * them as "compiled out", the server sends a custom `cryo/inactiveRegions`
 * notification listing the source ranges it gated away, and we fade them
 * with a `TextEditorDecorationType` - the same approach the C/C++ and clangd
 * extensions use for inactive `#ifdef` blocks.
 *
 * This is purely cosmetic and fully decoupled from the server: if the server
 * never sends the notification (an older build), nothing here ever fires.
 */

/** Params for the server's `cryo/inactiveRegions` notification. */
interface InactiveRegionsParams {
    uri: string;
    /**
     * Document version the ranges were computed from. Absent when the server
     * couldn't determine one (and from older servers), which we treat as
     * "applies to whatever is open" - the pre-versioning behaviour.
     */
    version?: number;
    regions: Array<{
        start: { line: number; character: number };
        end: { line: number; character: number };
    }>;
}

/** Ranges as of a given document version. */
interface VersionedRegions {
    version: number | undefined;
    ranges: vscode.Range[];
}

// One decoration type for the extension's lifetime. `opacity` fades the text
// (0.6 - a real dim, not a color, so it reads correctly under any theme).
let decorationType: vscode.TextEditorDecorationType | undefined;

// Last-known inactive ranges per document URI, so an editor that becomes
// visible later (split view, tab switch) can be painted immediately without
// waiting for the server to resend. Keyed with the document version they
// describe: positions are absolute, so ranges from an older version land on
// the wrong lines once the buffer moves on.
const regionsByUri = new Map<string, VersionedRegions>();

function ensureDecoration(): vscode.TextEditorDecorationType {
    if (!decorationType) {
        decorationType = vscode.window.createTextEditorDecorationType({
            opacity: '0.7',
            rangeBehavior: vscode.DecorationRangeBehavior.ClosedClosed,
        });
    }
    return decorationType;
}

/**
 * Paint `editor` with the last ranges we heard about, unless they describe an
 * older version of the document than the one on screen. Applying stale ranges
 * is worse than applying none: they dim whatever text has since shifted into
 * those positions, which reads as the dimming bleeding past the gated
 * declaration. A newer publish always follows an edit, so skipping costs at
 * most one compile's worth of delay.
 */
function applyToEditor(editor: vscode.TextEditor): void {
    const entry = regionsByUri.get(editor.document.uri.toString());
    if (!entry) {
        return;
    }
    if (entry.version !== undefined && entry.version !== editor.document.version) {
        return;
    }
    editor.setDecorations(ensureDecoration(), entry.ranges);
}

function applyToAllVisible(uri: string): void {
    for (const editor of vscode.window.visibleTextEditors) {
        if (editor.document.uri.toString() === uri) {
            applyToEditor(editor);
        }
    }
}

/**
 * One-time setup: keep newly-visible editors painted with whatever ranges we
 * last heard about. Call once from `activate`.
 */
export function initInactiveRegions(context: vscode.ExtensionContext): void {
    ensureDecoration();
    context.subscriptions.push(
        vscode.window.onDidChangeVisibleTextEditors((editors) => {
            for (const editor of editors) {
                applyToEditor(editor);
            }
        })
    );
    context.subscriptions.push({
        dispose: () => {
            decorationType?.dispose();
            decorationType = undefined;
            regionsByUri.clear();
        },
    });
}

/**
 * Per-client: subscribe to the server's `cryo/inactiveRegions` notifications.
 * Call from `startClient` once the client is running; the returned Disposable
 * is tied to that client's lifetime (a restart registers a fresh one).
 *
 * An empty `regions` array clears the dimming for that document - the server
 * is expected to send one whenever a file has no gated-away code, so stale
 * decorations don't linger after an edit removes the gate.
 */
export function registerInactiveRegions(client: LanguageClient): vscode.Disposable {
    return client.onNotification(
        'cryo/inactiveRegions',
        (params: InactiveRegionsParams) => {
            if (!params || typeof params.uri !== 'string') {
                return;
            }
            const ranges = (params.regions ?? []).map(
                (r) =>
                    new vscode.Range(
                        r.start.line,
                        r.start.character,
                        r.end.line,
                        r.end.character
                    )
            );
            regionsByUri.set(params.uri, {
                version: params.version,
                ranges,
            });
            applyToAllVisible(params.uri);
        }
    );
}
