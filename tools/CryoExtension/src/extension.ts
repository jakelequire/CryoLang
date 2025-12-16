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

    const killProcessesCommand = vscode.commands.registerCommand('cryo.killAllProcesses', async () => {
        vscode.window.showInformationMessage('🧹 Killing all CryoLSP processes...');
        await killExistingServers();
    });

    context.subscriptions.push(restartCommand, shutdownCommand, disableCommand, enableCommand, openLogCommand, killProcessesCommand);
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

async function killExistingServers() {
    console.log('[CLEANUP] Checking for existing CryoLSP processes...');
    
    try {
        const { exec } = require('child_process');
        
        // Find existing CryoLSP processes
        const findCommand = process.platform === 'win32' 
            ? 'tasklist /fi "imagename eq cryolsp*"' 
            : 'pgrep -f cryolsp';
            
        exec(findCommand, (error: any, stdout: string, stderr: string) => {
            if (!error && stdout) {
                console.log('[CLEANUP] Found existing CryoLSP processes:', stdout);
                
                // Kill existing processes
                const killCommand = process.platform === 'win32' 
                    ? 'taskkill /f /im cryolsp.exe' 
                    : 'pkill -f cryolsp';
                    
                exec(killCommand, (killError: any, killStdout: string, killStderr: string) => {
                    if (!killError) {
                        console.log('[CLEANUP] Successfully killed existing CryoLSP processes');
                        vscode.window.showInformationMessage('🧹 Cleaned up existing CryoLSP processes');
                    } else {
                        console.log('[CLEANUP] No existing processes to kill or failed to kill:', killError.message);
                    }
                });
            } else {
                console.log('[CLEANUP] No existing CryoLSP processes found');
            }
        });
        
        // Wait a bit for cleanup to complete
        await new Promise(resolve => setTimeout(resolve, 1000));
        
    } catch (error) {
        console.log('[CLEANUP] Error during cleanup:', error);
    }
}

async function startLanguageServer(context: vscode.ExtensionContext) {
    console.log('Starting CryoLang Language Server...');
    vscode.window.showInformationMessage('Starting CryoLang Language Server...');

    // Don't start if server is disabled
    if (serverDisabled) {
        console.log('Language server is disabled, not starting');
        return;
    }
    
    // Kill any existing server processes first
    await killExistingServers();

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
    // LSP server manages its own log files in ./logs directory

    // Use TCP mode for reliable communication
    const port = 7777;
    const serverArgs: string[] = ['--port', port.toString()];
    // LSP server now handles its own logging configuration

    console.log('Server path:', serverPath);
    console.log('Server args:', serverArgs);

    // Configure server options with TCP transport
    const serverOptions: ServerOptions = () => {
        return new Promise<StreamInfo>((resolve, reject) => {
            console.log('Starting LSP server with TCP transport...');

            // Start the server process
            const { spawn } = require('child_process');
            const serverProcess = spawn(serverPath, serverArgs, {
                stdio: ['pipe', 'pipe', 'pipe'],
                cwd: path.dirname(path.dirname(serverPath)), // Set working directory to CryoLang root
                shell: true, // Use shell on Windows for better compatibility
                windowsHide: true // Hide window on Windows
            });

            serverProcess.stdout.on('data', (data: Buffer) => {
                const output = data.toString();
                console.log('Server stdout:', output);
                // Look for the server startup message
                if (output.includes('Starting CryoLSP server on') || output.includes('Ready to accept client connections')) {
                    console.log('Server is ready, attempting TCP connection...');
                    attemptConnection();
                }
            });

            serverProcess.stderr.on('data', (data: Buffer) => {
                const stderr = data.toString();
                console.log('Server stderr:', stderr);
                
                // Show all server output to help with diagnostics
                if (stderr.trim()) {
                    console.log('[DIAGNOSTIC] Server stderr:', stderr);
                }
                
                // Also show critical errors to user
                if (stderr.includes('Error') || stderr.includes('Failed') || stderr.includes('BIND FAILED') || stderr.includes('LISTEN FAILED')) {
                    vscode.window.showErrorMessage(`CryoLSP Error: ${stderr.trim()}`);
                }
            });

            serverProcess.on('exit', (code: number | null, signal: NodeJS.Signals | null) => {
                console.log(`🚨 Server process exited with code ${code}, signal ${signal}`);
                if (code !== 0 && !manualShutdown) {
                    console.error('[DIAGNOSTIC] Server crashed! This is a SERVER-SIDE problem.');
                    if (code === 1) {
                        vscode.window.showErrorMessage(`CryoLSP server failed to initialize (exit code ${code}). Check server logs.`);
                    } else {
                        vscode.window.showErrorMessage(`CryoLSP server crashed with exit code ${code}`);
                    }
                } else if (code === 0) {
                    console.log('[DIAGNOSTIC] Server exited normally');
                }
            });

            serverProcess.on('error', (error: Error) => {
                console.error('🚨 Failed to start server process:', error);
                console.error('[DIAGNOSTIC] This is a CLIENT-SIDE problem - server binary cannot be executed');
                vscode.window.showErrorMessage(`Failed to start CryoLSP server process: ${error.message}`);
                reject(new Error(`Failed to start server: ${error.message}`));
            });

            // Add a health check to see if the process is running
            setTimeout(() => {
                if (serverProcess.killed) {
                    console.error('[DIAGNOSTIC] Server process was killed shortly after startup');
                } else {
                    console.log('[DIAGNOSTIC] Server process appears to be running, PID:', serverProcess.pid);
                }
            }, 2000);

            let connectionAttempted = false;
            let connectionRetries = 0;
            const maxRetries = 10;

            function attemptConnection() {
                if (connectionAttempted && connectionRetries >= maxRetries) {
                    reject(new Error('Failed to connect to LSP server after multiple attempts'));
                    return;
                }

                connectionAttempted = true;
                connectionRetries++;

                const socket = new Socket();

                console.log(`[DIAGNOSTIC] Attempting TCP connection to localhost:${port} (attempt ${connectionRetries}/${maxRetries})`);
                
                socket.connect(port, 'localhost', () => {
                    console.log(`✅ Successfully connected to LSP server via TCP on port ${port} (attempt ${connectionRetries})`);
                    vscode.window.showInformationMessage('✅ CryoLSP server connected successfully!');
                    resolve({
                        reader: socket,
                        writer: socket
                    });
                });

                socket.on('error', (err: any) => {
                    console.error(`❌ TCP connection error (attempt ${connectionRetries}/${maxRetries}):`, err.message);
                    
                    // Show specific error information
                    if (err.code === 'ECONNREFUSED') {
                        console.log('[DIAGNOSTIC] Connection refused - server may not be running or not listening on port', port);
                        if (connectionRetries === 1) {
                            vscode.window.showWarningMessage(`CryoLSP server not responding on port ${port}. This is likely a SERVER-SIDE problem.`);
                        }
                    } else if (err.code === 'ETIMEDOUT') {
                        console.log('[DIAGNOSTIC] Connection timed out - server may be overloaded');
                    } else {
                        console.log('[DIAGNOSTIC] Connection error code:', err.code, 'message:', err.message);
                    }

                    if (connectionRetries < maxRetries) {
                        // Reset for retry
                        connectionAttempted = false;
                        // Exponential backoff: 500ms, 1s, 2s, 4s, etc.
                        const delay = Math.min(500 * Math.pow(2, connectionRetries - 1), 4000);
                        console.log(`Retrying connection in ${delay}ms...`);
                        setTimeout(attemptConnection, delay);
                    } else {
                        console.error('❌ All connection attempts failed - this indicates a SERVER-SIDE problem');
                        vscode.window.showErrorMessage('CryoLSP server connection failed after multiple attempts. Check server logs for details.');
                        reject(err);
                    }
                });
            }

            // Start trying to connect immediately, then retry with backoff
            setTimeout(() => {
                console.log('Starting LSP server connection attempts...');
                attemptConnection();
            }, 1000); // Start after 1 second to let server initialize
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

async function restartLanguageServer(context: vscode.ExtensionContext) {
    vscode.window.showInformationMessage('🔄 Restarting CryoLang language server...');
    console.log('Restarting CryoLang language server...');
    
    // Clean up existing server processes and client
    manualShutdown = true;
    
    if (client) {
        try {
            await client.stop();
            console.log('Previous client stopped successfully');
        } catch (error) {
            console.log('Error stopping previous client:', error);
        }
    }
    
    // Kill any zombie processes
    await killExistingServers();
    
    // Reset flags and start fresh
    manualShutdown = false;
    await startLanguageServer(context);
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
    // LSP server now writes logs to fixed location: ./logs/cryo-lsp.log
    const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!workspaceRoot) {
        vscode.window.showErrorMessage('No workspace folder found. Cannot locate log file.');
        return;
    }
    
    const logFile = path.join(workspaceRoot, 'logs', 'cryo-lsp.log');

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