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

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;

// Restart tracking
const MAX_RESTARTS = 3;
const RESTART_WINDOW_MS = 60_000; // 1 minute
let restartTimestamps: number[] = [];
let restartInProgress = false;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
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

    // Start the language server
    await startClient(context);
}

async function startClient(context: vscode.ExtensionContext): Promise<void> {
    const config = getConfig();

    if (!config.enabled) {
        outputChannel.appendLine('CryoLSP is disabled in settings');
        updateStatus('disabled');
        return;
    }

    const serverPath = resolveServerPath(context.extensionPath);
    if (!serverPath) {
        outputChannel.appendLine(
            'CryoLSP binary not found. Build with "make lsp" or set cryo.languageServer.path'
        );
        vscode.window.showWarningMessage(
            'CryoLSP binary not found. Build with "make lsp" or configure the path in settings.'
        );
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
