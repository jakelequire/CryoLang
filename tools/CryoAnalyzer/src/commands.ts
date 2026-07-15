import * as vscode from 'vscode';

export function registerCommands(
    context: vscode.ExtensionContext,
    outputChannel: vscode.OutputChannel,
    restartCallback: () => Promise<void>,
    shutdownCallback: () => Promise<void>
): void {
    context.subscriptions.push(
        vscode.commands.registerCommand('cryo.restartLanguageServer', async () => {
            outputChannel.appendLine('Restarting CryoLSP...');
            await restartCallback();
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('cryo.shutdownLanguageServer', async () => {
            outputChannel.appendLine('Shutting down CryoLSP...');
            await shutdownCallback();
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('cryo.showOutputChannel', () => {
            outputChannel.show();
        })
    );

    // Fired by the "N implementations" code lens (server handler
    // `ImplLensBuilder`).  The server can't invoke VS Code's built-in
    // `editor.action.showReferences` directly - its arguments must be
    // real `vscode.Uri` / `Position` / `Location` objects, not the plain
    // JSON the LSP wire carries - so the lens targets this command, which
    // rehydrates the JSON and forwards it to the reference peek.
    context.subscriptions.push(
        vscode.commands.registerCommand(
            'cryo.showImplementations',
            async (
                anchorUri: unknown,
                anchorPosition: unknown,
                locations: unknown
            ) => {
                const uri = parseUri(anchorUri);
                const position = parsePosition(anchorPosition);
                if (!uri || !position || !Array.isArray(locations)) {
                    outputChannel.appendLine(
                        'cryo.showImplementations: malformed arguments'
                    );
                    return;
                }
                const locs: vscode.Location[] = [];
                for (const raw of locations) {
                    const loc = parseLocation(raw);
                    if (loc) {
                        locs.push(loc);
                    }
                }
                await vscode.commands.executeCommand(
                    'editor.action.showReferences',
                    uri,
                    position,
                    locs
                );
            }
        )
    );
}

// -- Argument rehydration (LSP JSON -> vscode types) ---------------------

function parseUri(raw: unknown): vscode.Uri | undefined {
    if (typeof raw !== 'string' || raw.length === 0) {
        return undefined;
    }
    try {
        return vscode.Uri.parse(raw);
    } catch {
        return undefined;
    }
}

function parsePosition(raw: unknown): vscode.Position | undefined {
    if (typeof raw !== 'object' || raw === null) {
        return undefined;
    }
    const obj = raw as { line?: unknown; character?: unknown };
    if (typeof obj.line !== 'number' || typeof obj.character !== 'number') {
        return undefined;
    }
    return new vscode.Position(obj.line, obj.character);
}

function parseLocation(raw: unknown): vscode.Location | undefined {
    if (typeof raw !== 'object' || raw === null) {
        return undefined;
    }
    const obj = raw as { uri?: unknown; range?: unknown };
    const uri = parseUri(obj.uri);
    if (!uri || typeof obj.range !== 'object' || obj.range === null) {
        return undefined;
    }
    const range = obj.range as { start?: unknown; end?: unknown };
    const start = parsePosition(range.start);
    const end = parsePosition(range.end);
    if (!start || !end) {
        return undefined;
    }
    return new vscode.Location(uri, new vscode.Range(start, end));
}
