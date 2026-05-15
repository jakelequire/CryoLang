import * as vscode from 'vscode';
import { LanguageClient } from 'vscode-languageclient/node';

/**
 * Virtual-document scheme that the LSP server-side code action targets.
 *
 * The full pipeline:
 *
 *   1. The CryoLSP server stamps each `publishDiagnostics` entry with a
 *      stable `data.diagId` and caches the rendered report under that
 *      id.
 *   2. The server's `textDocument/codeAction` handler emits a
 *      "Show full Cryo diagnostic" action whose `command` is
 *      `cryo.openRenderedDiagnostic` with the id as argument.
 *   3. The command handler below parses the id into a
 *      `cryo-diagnostic:/<id>.cryo-diag` URI and asks VS Code to open
 *      it as a text document.
 *   4. VS Code calls our `TextDocumentContentProvider`, which fires a
 *      `cryo/renderedDiagnostic` LSP request and returns the cached
 *      text.
 *
 * The document is read-only because we never register a write provider;
 * VS Code marks `cryo-diagnostic:` content as untitled/virtual and
 * refuses save attempts.
 */
export const CRYO_DIAG_SCHEME = 'cryo-diagnostic';

interface RenderedDiagnosticResult {
    content: string;
}

/**
 * Register the content provider and the `cryo.openRenderedDiagnostic`
 * command.  Both reference the LanguageClient lazily through
 * `getClient` so they keep working across server restarts.
 */
export function registerDiagnosticView(
    context: vscode.ExtensionContext,
    getClient: () => LanguageClient | undefined,
    outputChannel: vscode.OutputChannel
): void {
    const provider: vscode.TextDocumentContentProvider = {
        async provideTextDocumentContent(uri: vscode.Uri): Promise<string> {
            const client = getClient();
            if (!client) {
                return '// Cryo language server is not running.';
            }

            const id = diagIdFromUri(uri);
            if (!id) {
                return `// Malformed diagnostic URI: ${uri.toString()}`;
            }

            try {
                const result = await client.sendRequest<RenderedDiagnosticResult>(
                    'cryo/renderedDiagnostic',
                    { id }
                );
                const content = result?.content ?? '';
                if (!content) {
                    return `// Diagnostic ${id} is no longer cached.\n// Save the file or restart the language server to repopulate.`;
                }
                return content;
            } catch (error) {
                const message = error instanceof Error ? error.message : String(error);
                outputChannel.appendLine(
                    `cryo/renderedDiagnostic failed: ${message}`
                );
                return `// Failed to fetch diagnostic ${id}:\n// ${message}`;
            }
        },
    };

    context.subscriptions.push(
        vscode.workspace.registerTextDocumentContentProvider(
            CRYO_DIAG_SCHEME,
            provider
        )
    );

    context.subscriptions.push(
        vscode.commands.registerCommand(
            'cryo.openRenderedDiagnostic',
            async (diagId: unknown) => {
                if (typeof diagId !== 'string' || diagId.length === 0) {
                    outputChannel.appendLine(
                        `cryo.openRenderedDiagnostic: expected a diagnostic id, got ${typeof diagId}`
                    );
                    return;
                }
                // Append a `.cryo-diag` suffix so the tab gets a
                // sensible title; the path before the dot is the id.
                const uri = vscode.Uri.parse(
                    `${CRYO_DIAG_SCHEME}:/${diagId}.cryo-diag`
                );
                const doc = await vscode.workspace.openTextDocument(uri);
                await vscode.window.showTextDocument(doc, {
                    preview: false,
                    viewColumn: vscode.ViewColumn.Beside,
                });
            }
        )
    );
}

/**
 * Pull the diagnostic id from a `cryo-diagnostic:/<id>.cryo-diag` URI.
 *
 * The leading `/` and the trailing `.cryo-diag` are presentation only;
 * the id itself is the 16-char hex hash the LSP cache key uses.
 * Returns `null` on shape mismatch so the content provider can fall
 * back to an inline error message.
 */
function diagIdFromUri(uri: vscode.Uri): string | null {
    let path = uri.path;
    if (path.startsWith('/')) {
        path = path.slice(1);
    }
    const dot = path.lastIndexOf('.');
    const id = dot >= 0 ? path.slice(0, dot) : path;
    return id.length > 0 ? id : null;
}
