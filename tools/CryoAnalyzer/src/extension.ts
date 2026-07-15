import * as vscode from 'vscode';
import * as path from 'path';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    ErrorAction,
    CloseAction,
    State,
} from 'vscode-languageclient/node';

import { getConfig, resolveServerPath } from './config';
import { createStatusBar, updateStatus, disposeStatusBar } from './statusBar';
import { registerCommands } from './commands';
import { registerDiagnosticView } from './diagnosticView';
import { extendMarkdownIt, initHighlighter } from './markdownPreview';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;

// Hover verbosity: when true, struct/class type hovers additionally list the
// type's method signatures.  VS Code's native Hover Verbosity API is still a
// proposed API (unavailable to a normally-installed extension), so we emulate
// it: the hover middleware appends a "Show/Hide methods" toggle to type
// hovers, and the `cryo.toggleHoverMethods` command flips this flag and
// re-shows the hover.  The flag is global (a "verbosity level"), matching how
// the native feature persists across hovers.
let hoverMethodsExpanded = false;

/** True when a hover's markdown looks like a struct/class type declaration -
 *  i.e. the only kind the "expand methods" toggle is meaningful for. */
function isTypeHover(hover: vscode.Hover | null | undefined): boolean {
    if (!hover || !hover.contents) { return false; }
    return hover.contents.some((c) => {
        const v = typeof c === 'string' ? c : (c as vscode.MarkdownString).value;
        return v.includes('type struct ') || v.includes('type class ');
    });
}

/** Append the expand/collapse affordance to a type hover.  The link runs the
 *  toggle command, carrying the hovered position so it can re-show the hover
 *  at the right token.  Non-type hovers are returned untouched. */
function appendMethodsToggle(
    hover: vscode.Hover | null | undefined,
    document: vscode.TextDocument,
    position: vscode.Position,
    expanded: boolean
): vscode.Hover | null | undefined {
    if (!isTypeHover(hover)) { return hover; }
    const arg = encodeURIComponent(
        JSON.stringify({
            uri: document.uri.toString(),
            line: position.line,
            character: position.character,
        })
    );
    const md = new vscode.MarkdownString(
        expanded
            ? `$(chevron-down) [Hide methods](command:cryo.toggleHoverMethods?${arg})`
            : `$(chevron-right) [Show methods](command:cryo.toggleHoverMethods?${arg})`
    );
    md.isTrusted = true;
    md.supportThemeIcons = true;
    hover!.contents.push(md);
    return hover;
}

// Restart tracking
const MAX_RESTARTS = 3;
const RESTART_WINDOW_MS = 60_000; // 1 minute
let restartTimestamps: number[] = [];
let restartInProgress = false;

export async function activate(
    context: vscode.ExtensionContext
): Promise<{ extendMarkdownIt: typeof extendMarkdownIt }> {
    outputChannel = vscode.window.createOutputChannel('CryoLSP');
    context.subscriptions.push(outputChannel);

    const statusBar = createStatusBar();
    context.subscriptions.push(statusBar);

    // Register commands
    registerCommands(
        context,
        outputChannel,
        async () => {
            // Manual restart: reset retry tracking
            restartTimestamps = [];
            restartInProgress = false;
            await stopClient();
            await startClient(context);
        },
        async () => {
            await stopClient();
            updateStatus('stopped');
        }
    );

    // Diagnostic view: register the `cryo-diagnostic:` content
    // provider and the `cryo.openRenderedDiagnostic` command once.
    // Both grab the LanguageClient lazily so they survive server
    // restarts without a re-registration step.
    registerDiagnosticView(context, () => client, outputChannel);

    // Build the Shiki highlighter for the Markdown preview before returning, so
    // it's ready when VS Code renders. Non-fatal: on failure the markdown-it
    // hook falls back to VS Code's default highlighting.
    try {
        await initHighlighter();
    } catch (err) {
        outputChannel.appendLine(`Markdown preview highlighter unavailable: ${err}`);
    }

    // Start the language server
    await startClient(context);

    // Expose the Markdown-preview highlighter. VS Code calls extendMarkdownIt
    // on the preview's markdown-it instance (gated by the
    // `markdown.markdownItPlugins` contribution in package.json).
    return { extendMarkdownIt };
}

async function startClient(context: vscode.ExtensionContext): Promise<void> {
    const config = getConfig();

    if (!config.enabled) {
        outputChannel.appendLine('CryoLSP is disabled in settings');
        updateStatus('disabled');
        return;
    }

    const serverPath = resolveServerPath(context.extensionPath, outputChannel);
    if (!serverPath) {
        const hint =
            'CryoLSP binary not found. Build with "cryo build" inside ' +
            'tools/CryoLSP, then either set $CRYO_HOME to the install root, ' +
            'put cryolsp on $PATH, or set cryo.languageServer.path.';
        outputChannel.appendLine(hint);
        vscode.window.showWarningMessage(hint);
        updateStatus('error');
        return;
    }

    outputChannel.appendLine(`Starting CryoLSP: ${serverPath}`);
    updateStatus('starting');

    // Build server options
    const args: string[] = [];
    if (config.debug) {
        args.push('--debug');
    }

    const serverOptions: ServerOptions = {
        run: {
            command: serverPath,
            args: args,
            transport: TransportKind.stdio,
        },
        debug: {
            command: serverPath,
            args: [...args, '--debug'],
            transport: TransportKind.stdio,
        },
    };

    // Build client options
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cryo' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cryo'),
        },
        outputChannel: outputChannel,
        middleware: {
            // Emulate hover verbosity: on a type hover, append a "Show/Hide
            // methods" toggle.  When expanded, bypass the standard hover and
            // ask the server for the method-listing variant via the custom
            // `cryo/expandHover` request.
            provideHover: async (document, position, token, next) => {
                if (!hoverMethodsExpanded) {
                    const base = await next(document, position, token);
                    return appendMethodsToggle(base, document, position, false);
                }
                // Expanded mode: request the verbose hover from the server.
                try {
                    const params = {
                        textDocument: { uri: document.uri.toString() },
                        position: {
                            line: position.line,
                            character: position.character,
                        },
                    };
                    const resp = await client!.sendRequest<any>(
                        'cryo/expandHover',
                        params,
                        token
                    );
                    if (!resp) {
                        // No verbose result (e.g. not over a symbol) - fall
                        // back to the standard hover.
                        return await next(document, position, token);
                    }
                    const hover = await client!.protocol2CodeConverter.asHover(
                        resp
                    );
                    return appendMethodsToggle(hover, document, position, true);
                } catch {
                    // Older server without `cryo/expandHover`, or a transient
                    // error - degrade gracefully to the standard hover.
                    const base = await next(document, position, token);
                    return appendMethodsToggle(base, document, position, false);
                }
            },
        },
        errorHandler: {
            error: (_error, _message, count) => {
                // After 5 errors, shut down
                if (count && count >= 5) {
                    return { action: ErrorAction.Shutdown };
                }
                return { action: ErrorAction.Continue };
            },
            closed: () => {
                // Check if we should auto-restart
                if (canRestart()) {
                    outputChannel.appendLine('Server closed unexpectedly, restarting...');
                    updateStatus('starting');
                    return { action: CloseAction.Restart };
                }

                outputChannel.appendLine(
                    `Server crashed ${MAX_RESTARTS} times in the last minute. Not restarting. Use "Cryo: Restart Language Server" to retry.`
                );
                updateStatus('error');
                return { action: CloseAction.DoNotRestart };
            },
        },
    };

    // Create and start client
    client = new LanguageClient(
        'cryoLanguageServer',
        'CryoLSP',
        serverOptions,
        clientOptions
    );

    // Track state changes for status bar
    client.onDidChangeState((event) => {
        switch (event.newState) {
            case State.Starting:
                updateStatus('starting');
                break;
            case State.Running:
                updateStatus('ready');
                break;
            case State.Stopped:
                // Only set error if we didn't intentionally stop
                if (!restartInProgress) {
                    updateStatus('stopped');
                }
                break;
        }
    });

    // Hover-verbosity toggle: flip the expand flag and re-show the hover.
    // Invoked from the "Show/Hide methods" link in a type hover (with the
    // hovered position as its argument) or from a keybinding (no argument,
    // uses the current cursor position).
    context.subscriptions.push(
        vscode.commands.registerCommand(
            'cryo.toggleHoverMethods',
            async (arg?: { uri: string; line: number; character: number }) => {
                hoverMethodsExpanded = !hoverMethodsExpanded;
                // `editor.action.showHover` renders at the cursor, not the
                // mouse, so move the selection onto the hovered token first
                // when the link handed us its position.
                if (arg && typeof arg.line === 'number') {
                    const editor = vscode.window.visibleTextEditors.find(
                        (e) => e.document.uri.toString() === arg.uri
                    );
                    if (editor) {
                        const pos = new vscode.Position(arg.line, arg.character);
                        editor.selection = new vscode.Selection(pos, pos);
                        await vscode.window.showTextDocument(editor.document, {
                            viewColumn: editor.viewColumn,
                            preserveFocus: false,
                            preview: false,
                        });
                    }
                }
                await vscode.commands.executeCommand('editor.action.showHover');
            }
        )
    );

    try {
        await client.start();
        outputChannel.appendLine('CryoLSP started successfully');
        updateStatus('ready');
    } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        outputChannel.appendLine(`Failed to start CryoLSP: ${message}`);
        updateStatus('error');
        client = undefined;
    }
}

function canRestart(): boolean {
    const now = Date.now();

    // Clean old timestamps outside the window
    restartTimestamps = restartTimestamps.filter(
        (ts) => now - ts < RESTART_WINDOW_MS
    );

    // Check if we've hit the limit
    if (restartTimestamps.length >= MAX_RESTARTS) {
        return false;
    }

    restartTimestamps.push(now);
    return true;
}

async function stopClient(): Promise<void> {
    if (client) {
        restartInProgress = true;
        try {
            await client.stop(2000); // 2 second timeout
        } catch {
            // Ignore stop errors
        }
        restartInProgress = false;
        client = undefined;
    }
}

export async function deactivate(): Promise<void> {
    await stopClient();
    disposeStatusBar();
}
