import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    ErrorAction,
    CloseAction,
    StreamInfo
} from 'vscode-languageclient/node';
import { Socket } from 'net';

let client: LanguageClient;
let manualShutdown = false; // Track if shutdown was intentional
let serverDisabled = false; // Track if server is completely disabled

export function activate(context: vscode.ExtensionContext) {
    console.log('CryoLang extension is now active!');
    vscode.window.showInformationMessage('CryoLang extension activated!');

    // Store context globally for restart functionality
    (global as any).cryoExtensionContext = context;

    // Start the language server
    startLanguageServer(context);

    // Register commands
    const restartCommand = vscode.commands.registerCommand('cryo.restartLanguageServer', () => {
        restartLanguageServer(context);
    });

    const shutdownCommand = vscode.commands.registerCommand('cryo.shutdownLanguageServer', () => {
        shutdownLanguageServer();
    });

    const disableCommand = vscode.commands.registerCommand('cryo.disableLanguageServer', () => {
        disableLanguageServer();
    });

    const enableCommand = vscode.commands.registerCommand('cryo.enableLanguageServer', () => {
        enableLanguageServer(context);
    });

    const openLogCommand = vscode.commands.registerCommand('cryo.openLogFile', () => {
        openLogFile();
    });

    context.subscriptions.push(restartCommand, shutdownCommand, disableCommand, enableCommand, openLogCommand);
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

function startLanguageServer(context: vscode.ExtensionContext) {
    console.log('Starting CryoLang Language Server...');
    vscode.window.showInformationMessage('Starting CryoLang Language Server...');

    // Don't start if server is disabled
    if (serverDisabled) {
        console.log('Language server is disabled, not starting');
        return;
    }

    const serverPath = findLanguageServer();
    console.log('Server path found:', serverPath);
    vscode.window.showInformationMessage('Server path: ' + (serverPath || 'NOT FOUND'));

    if (!serverPath) {
        console.error('CryoLSP server not found');
        vscode.window.showErrorMessage(
            'CryoLSP language server not found. Please check your configuration or ensure the server is built.',
            'Open Settings'
        ).then((selection: string | undefined) => {
            if (selection === 'Open Settings') {
                vscode.commands.executeCommand('workbench.action.openSettings', 'cryo.languageServer');
            }
        });
        return;
    }

    const config = vscode.workspace.getConfiguration('cryo.languageServer');
    const debugMode = config.get<boolean>('debug', true); // Default to true for now

    // Create log file in workspace root if possible, otherwise next to server
    let logFile = config.get<string>('logFile', '');
    if (!logFile) {
        const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (workspaceRoot) {
            logFile = path.join(workspaceRoot, 'cryo-lsp.log');
        } else {
            logFile = path.join(path.dirname(serverPath), '..', 'cryo-lsp.log');
        }
    }

    // Ensure log file directory exists
    const logDir = path.dirname(logFile);
    if (!fs.existsSync(logDir)) {
        fs.mkdirSync(logDir, { recursive: true });
    }

    // Use TCP mode for reliable communication
    const port = 8080;
    const serverArgs: string[] = ['--tcp', port.toString()];
    if (debugMode) {
        serverArgs.push('--debug');
    }
    if (logFile) {
        serverArgs.push('--log-file', logFile);
    }

    console.log('Server path:', serverPath);
    console.log('Server args:', serverArgs);
    console.log('Log file path:', logFile);

    // Configure server options with TCP transport
    const serverOptions: ServerOptions = () => {
        return new Promise<StreamInfo>((resolve, reject) => {
            console.log('Starting LSP server with TCP transport...');

            // Start the server process
            const { spawn } = require('child_process');
            const serverProcess = spawn(serverPath, serverArgs, {
                stdio: ['pipe', 'pipe', 'pipe'],
                cwd: path.dirname(path.dirname(serverPath)) // Set working directory to CryoLang root
            });

            serverProcess.stdout.on('data', (data: Buffer) => {
                const output = data.toString();
                console.log('Server stdout:', output);
                // Look for the "listening on port" message
                if (output.includes('LSP server listening on port')) {
                    console.log('Server is ready, attempting TCP connection...');
                    attemptConnection();
                }
            });

            serverProcess.stderr.on('data', (data: Buffer) => {
                const stderr = data.toString();
                console.log('Server stderr:', stderr);
                // Also show critical errors to user
                if (stderr.includes('Error') || stderr.includes('Failed')) {
                    vscode.window.showErrorMessage(`CryoLSP Error: ${stderr.trim()}`);
                }
            });

            serverProcess.on('exit', (code: number | null, signal: NodeJS.Signals | null) => {
                console.log(`Server process exited with code ${code}, signal ${signal}`);
                if (code !== 0 && !manualShutdown) {
                    vscode.window.showErrorMessage(`CryoLSP server crashed with exit code ${code}`);
                }
            });

            serverProcess.on('error', (error: Error) => {
                console.error('Failed to start server process:', error);
                reject(new Error(`Failed to start server: ${error.message}`));
            });

            let connectionAttempted = false;

            function attemptConnection() {
                if (connectionAttempted) return;
                connectionAttempted = true;

                const socket = new Socket();
                socket.connect(port, 'localhost', () => {
                    console.log('Successfully connected to LSP server via TCP on port', port);
                    resolve({
                        reader: socket,
                        writer: socket
                    });
                });

                socket.on('error', (err) => {
                    console.error('TCP connection error:', err);
                    reject(err);
                });
            }

            // Fallback: try to connect after a delay if we don't see the ready message
            setTimeout(() => {
                if (!connectionAttempted) {
                    console.log('Timeout waiting for server ready message, attempting connection anyway...');
                    vscode.window.showWarningMessage('CryoLSP server may be slow to start, attempting connection...');
                    attemptConnection();
                }
            }, 5000); // Increased timeout to 5 seconds
        });
    };

    // Configure client options
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cryo' }],
        synchronize: {
            configurationSection: 'cryo',
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cryo')
        },
        outputChannelName: 'CryoLang Language Server',
        traceOutputChannel: vscode.window.createOutputChannel('CryoLang Language Server Trace'),
        // Add initialization options
        initializationOptions: {
            debug: debugMode
        },

        // Add error handler
        errorHandler: {
            error: (error, message, count) => {
                console.error('LSP Client Error:', error, message, count);
                if (count !== undefined && count >= 5) {
                    vscode.window.showErrorMessage('CryoLang Language Server has crashed multiple times. Auto-restart disabled.');
                    return { action: ErrorAction.Shutdown };
                }
                return { action: ErrorAction.Continue };
            },
            closed: () => {
                console.log('LSP Connection closed');
                return { action: CloseAction.DoNotRestart };
            }
        }
    };

    // Create the language client
    client = new LanguageClient(
        'cryoLanguageServer',
        'CryoLang Language Server',
        serverOptions,
        clientOptions
    );

    console.log('Starting LSP client with server path:', serverPath);
    console.log('Server args:', serverArgs);

    // Set up event handlers before starting
    client.onDidChangeState((event) => {
        console.log('LSP Client state changed:', event.oldState, '->', event.newState);
        vscode.window.showInformationMessage(`LSP Client state: ${event.oldState} -> ${event.newState}`);
    });

    // Start the client (and server)
    client.start().then(() => {
        console.log('CryoLSP language server started successfully');
        vscode.window.showInformationMessage('CryoLang language server is running');

        // Verify the server is actually responding
        setTimeout(() => {
            if (client.isRunning()) {
                console.log('LSP client is running and ready');
            } else {
                console.warn('LSP client started but may not be responding');
            }
        }, 1000);
    }).catch((error: any) => {
        console.error('Failed to start CryoLSP language server:', error);
        const errorMsg = error.message || error.toString();
        vscode.window.showErrorMessage(`Failed to start CryoLang language server: ${errorMsg}`, 'Retry', 'Open Settings')
            .then((selection: string | undefined) => {
                if (selection === 'Retry') {
                    setTimeout(() => startLanguageServer(context), 2000);
                } else if (selection === 'Open Settings') {
                    vscode.commands.executeCommand('workbench.action.openSettings', 'cryo.languageServer');
                }
            });
    });
}

function restartLanguageServer(context: vscode.ExtensionContext) {
    if (client) {
        manualShutdown = true; // Mark as intentional shutdown to prevent auto-restart during restart
        client.stop().then(() => {
            setTimeout(() => {
                manualShutdown = false; // Reset flag before starting
                startLanguageServer(context);
            }, 1000);
        });
    } else {
        manualShutdown = false; // Ensure flag is reset
        startLanguageServer(context);
    }
    vscode.window.showInformationMessage('Restarting CryoLang language server...');
}

function shutdownLanguageServer() {
    if (client) {
        manualShutdown = true; // Mark as intentional shutdown
        client.stop().then(() => {
            vscode.window.showInformationMessage('CryoLang language server stopped');
        });
    } else {
        vscode.window.showWarningMessage('CryoLang language server is not running');
    }
}

function disableLanguageServer() {
    serverDisabled = true;
    if (client) {
        manualShutdown = true; // Prevent restart
        client.stop().then(() => {
            vscode.window.showInformationMessage('CryoLang language server disabled. It will not restart automatically.');
        });
    } else {
        vscode.window.showInformationMessage('CryoLang language server disabled.');
    }
}

function enableLanguageServer(context: vscode.ExtensionContext) {
    serverDisabled = false;
    if (!client || !client.isRunning()) {
        vscode.window.showInformationMessage('Enabling and starting CryoLang language server...');
        startLanguageServer(context);
    } else {
        vscode.window.showInformationMessage('CryoLang language server enabled and already running.');
    }
}

function openLogFile() {
    const config = vscode.workspace.getConfiguration('cryo.languageServer');
    let logFile = config.get<string>('logFile', '');

    if (!logFile) {
        const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (workspaceRoot) {
            logFile = path.join(workspaceRoot, 'cryo-lsp.log');
        }
    }

    if (!logFile) {
        vscode.window.showErrorMessage('Cannot determine log file path. Please configure cryo.languageServer.logFile in settings.');
        return;
    }

    if (!fs.existsSync(logFile)) {
        vscode.window.showWarningMessage(`Log file does not exist: ${logFile}. The language server may not have started yet.`);
        return;
    }

    // Open the log file in VS Code
    const uri = vscode.Uri.file(logFile);
    vscode.workspace.openTextDocument(uri).then(doc => {
        vscode.window.showTextDocument(doc);
    });
}

function findLanguageServer(): string | null {
    console.log('Looking for CryoLSP server...');
    const config = vscode.workspace.getConfiguration('cryo.languageServer');
    const configuredPath = config.get<string>('path', '');
    console.log('Configured server path:', configuredPath);

    // Check configured path first
    if (configuredPath && fs.existsSync(configuredPath)) {
        console.log('Using configured server path:', configuredPath);
        return configuredPath;
    }

    // Search in workspace
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            // Check common locations within workspace
            const candidates = [
                path.join(folder.uri.fsPath, 'bin', 'cryolsp.exe'),
                path.join(folder.uri.fsPath, 'bin', 'cryolsp'),
                path.join(folder.uri.fsPath, 'tools', 'CryoLSP', 'bin', 'cryolsp.exe'),
                path.join(folder.uri.fsPath, 'tools', 'CryoLSP', 'bin', 'cryolsp'),
                path.join(folder.uri.fsPath, 'cryolsp.exe'),
                path.join(folder.uri.fsPath, 'cryolsp')
            ];

            for (const candidate of candidates) {
                if (fs.existsSync(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Try to find in PATH
    const pathEnv = process.env.PATH || '';
    const pathSeparator = process.platform === 'win32' ? ';' : ':';
    const executableName = process.platform === 'win32' ? 'cryolsp.exe' : 'cryolsp';

    for (const dir of pathEnv.split(pathSeparator)) {
        const fullPath = path.join(dir, executableName);
        if (fs.existsSync(fullPath)) {
            return fullPath;
        }
    }

    return null;
}