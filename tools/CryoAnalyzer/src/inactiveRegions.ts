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
    regions: Array<{
        start: { line: number; character: number };
        end: { line: number; character: number };
    }>;
}

// One decoration type for the extension's lifetime. `opacity` fades the text
// (matching the ~0.55 the C/C++ extension uses); a real dim, not a color, so
// it reads correctly under any theme.
let decorationType: vscode.TextEditorDecorationType | undefined;

// Last-known inactive ranges per document URI, so an editor that becomes
// visible later (split view, tab switch) can be painted immediately without
// waiting for the server to resend.
const regionsByUri = new Map<string, vscode.Range[]>();

function ensureDecoration(): vscode.TextEditorDecorationType {
    if (!decorationType) {
        decorationType = vscode.window.createTextEditorDecorationType({
            opacity: '0.55',
            rangeBehavior: vscode.DecorationRangeBehavior.ClosedClosed,
        });
    }
    return decorationType;
}

function applyToEditor(editor: vscode.TextEditor): void {
    const ranges = regionsByUri.get(editor.document.uri.toString()) ?? [];
    editor.setDecorations(ensureDecoration(), ranges);
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
                if (regionsByUri.has(editor.document.uri.toString())) {
                    applyToEditor(editor);
                }
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
            regionsByUri.set(params.uri, ranges);
            applyToAllVisible(params.uri);
        }
    );
}
