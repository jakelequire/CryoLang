import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as net from 'net';
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind, ErrorAction, CloseAction } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let clientStopped = false; // Flag to prevent auto-restart

// File logging utility
function logToFile(level: string, component: string, message: string) {
    try {
        const timestamp = new Date().toISOString().replace('T', ' ').replace('Z', '');
        const logLine = `[${timestamp}] [${level}] [${component}] ${message}\n`;

        // Try to find the logs directory - look for CryoLang project root
        const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        let logsDir = '';

        if (workspaceRoot) {
            // Check if we're in the CryoLang project
            if (workspaceRoot.includes('CryoLang')) {
                logsDir = path.join(workspaceRoot, 'logs');
            } else {
                // Try to find CryoLang directory
                const cryoPath = 'C:\\Programming\\apps\\CryoLang\\logs';
                if (fs.existsSync(cryoPath)) {
                    logsDir = cryoPath;
                }
            }
        }

        if (!logsDir) {
            // Fallback to extension directory
            logsDir = path.join(__dirname, '..', 'logs');
        }

        // Ensure logs directory exists
        if (!fs.existsSync(logsDir)) {
            fs.mkdirSync(logsDir, { recursive: true });
        }

        const logFile = path.join(logsDir, 'cryo-extension.log');
        fs.appendFileSync(logFile, logLine);
    } catch (error) {
        console.error('Failed to write to log file:', error);
    }
}

// Convenience logging functions
const LOG = {
    debug: (component: string, message: string) => {
        console.log(`[DEBUG] [${component}] ${message}`);
        logToFile('DEBUG', component, message);
    },
    info: (component: string, message: string) => {
        console.log(`[INFO] [${component}] ${message}`);
        logToFile('INFO', component, message);
    },
    error: (component: string, message: string) => {
        console.error(`[ERROR] [${component}] ${message}`);
        logToFile('ERROR', component, message);
    }
};

// Helper function to check if LSP server is already running on a port
async function checkServerRunning(port: number): Promise<boolean> {
    return new Promise((resolve) => {
        const socket = new net.Socket();

        const timeout = setTimeout(() => {
            socket.destroy();
            resolve(false);
        }, 1000);

        socket.on('connect', () => {
            clearTimeout(timeout);
            socket.destroy();
            resolve(true);
        });

        socket.on('error', () => {
            clearTimeout(timeout);
            resolve(false);
        });

        socket.connect(port, 'localhost');
    });
}

export function activate(context: vscode.ExtensionContext) {
    LOG.info('Extension', '=== Cryo Language Support Extension Activated ===');
    LOG.info('Extension', 'Extension path: ' + context.extensionPath);
    LOG.info('Extension', 'Workspace folders: ' + JSON.stringify(vscode.workspace.workspaceFolders?.map(f => f.uri.fsPath)));

    // Debug: Check if VS Code recognizes .cryo files
    const activeEditor = vscode.window.activeTextEditor;
    if (activeEditor) {
        LOG.debug('Extension', 'Active editor document: ' + JSON.stringify({
            fileName: activeEditor.document.fileName,
            languageId: activeEditor.document.languageId,
            uri: activeEditor.document.uri.toString()
        }));
    }

    // Listen for active editor changes to see language ID
    const editorChangeDisposable = vscode.window.onDidChangeActiveTextEditor((editor) => {
        // Logging disabled - hover functionality is working
    });

    // Determine the correct executable name based on platform
    const isWindows = process.platform === 'win32';
    const lspExecutableName = isWindows ? 'cryo-lsp.exe' : 'cryo-lsp';

    // Try multiple possible paths for the LSP server executable
    const possiblePaths = [
        // Development path (from extension directory up to project root, then to bin)
        path.join(context.extensionPath, '..', '..', '..', 'bin', lspExecutableName),
        // Bundled path (if LSP is bundled with extension)
        path.join(context.extensionPath, 'bin', lspExecutableName),
        // Look in workspace root/bin if we're in a CryoLang workspace
        ...(vscode.workspace.workspaceFolders || []).map(folder =>
            path.join(folder.uri.fsPath, 'bin', lspExecutableName)
        ),
        // Legacy Windows absolute path (keeping for backward compatibility)
        ...(isWindows ? ['C:\\Programming\\apps\\CryoLang\\bin\\cryo-lsp.exe'] : [])
    ];

    let serverCommand = '';

    LOG.info('Extension', `Platform detected: ${process.platform}, looking for executable: ${lspExecutableName}`);
    LOG.info('Extension', `Possible paths to check: ${possiblePaths.length}`);

    for (const possiblePath of possiblePaths) {
        LOG.debug('Extension', 'Checking for LSP server at: ' + possiblePath);
        if (fs.existsSync(possiblePath)) {
            serverCommand = possiblePath;
            LOG.info('Extension', 'Found LSP server at: ' + serverCommand);
            break;
        }
    }

    if (!serverCommand) {
        const errorMsg = 'Cryo LSP Server not found. Please ensure cryo-lsp.exe is built.';
        LOG.error('Extension', errorMsg);
        vscode.window.showErrorMessage(errorMsg);
        return;
    }

    async function startLanguageServer() {
        // Check if client was intentionally stopped
        if (clientStopped) {
            LOG.info('LanguageServer', 'Language client was intentionally stopped, not restarting.');
            return;
        }

        // Get workspace root for server configuration
        const workspacePath = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || '';

        // If client already exists, stop it first
        if (client) {
            try {
                LOG.info('LanguageServer', 'Stopping existing client...');
                await client.stop(5000); // 5 second timeout
                client.dispose();
                // Add a delay to ensure proper cleanup
                await new Promise(resolve => setTimeout(resolve, 1000));
            } catch (error) {
                LOG.error('LanguageServer', 'Error stopping previous client: ' + String(error));
            }
        }

        LOG.info('LanguageServer', 'Configuring server options for LSP communication');

        // Test server executable first
        LOG.debug('LanguageServer', 'Testing server executable...');
        try {
            const testResult = await new Promise<string>((resolve, reject) => {
                const testProcess = require('child_process').spawn(serverCommand, ['--version'], {
                    stdio: ['pipe', 'pipe', 'pipe']
                });
                
                let output = '';
                testProcess.stdout.on('data', (data: Buffer) => {
                    output += data.toString();
                });
                
                testProcess.stderr.on('data', (data: Buffer) => {
                    output += data.toString();
                });
                
                testProcess.on('close', (code: number) => {
                    LOG.debug('LanguageServer', `Test process exited with code: ${code}`);
                    if (code === 0 || output.length > 0) {
                        resolve(output);
                    } else {
                        reject(new Error(`Process exited with code ${code}`));
                    }
                });
                
                testProcess.on('error', (error: Error) => {
                    reject(error);
                });
                
                // Timeout after 5 seconds
                setTimeout(() => {
                    testProcess.kill();
                    reject(new Error('Test timeout'));
                }, 5000);
            });
            
            LOG.debug('LanguageServer', 'Server executable test result: ' + testResult);
        } catch (error) {
            LOG.error('LanguageServer', 'Server executable test failed: ' + String(error));
            // Continue anyway - server might not support --version
        }

        // Use TCP socket communication to avoid stdio pipe issues
        const LSP_PORT = 8080;
        
        // First start the LSP server manually with socket transport
        LOG.info('LanguageServer', `Starting LSP server with socket transport on port ${LSP_PORT}`);
        
        const serverOptions: ServerOptions = () => {
            return new Promise((resolve, reject) => {
                // Start the server process
                const { spawn } = require('child_process');
                const serverProcess = spawn(serverCommand, ['--socket', '--port', LSP_PORT.toString()], {
                    stdio: 'pipe'
                });
                
                serverProcess.on('error', (error: any) => {
                    LOG.error('LanguageServer', `Failed to start server process: ${error}`);
                    reject(error);
                });
                
                // Wait a moment for server to start, then create socket connection
                setTimeout(() => {
                    const socket = net.connect(LSP_PORT, 'localhost');
                    
                    socket.on('connect', () => {
                        LOG.info('LanguageServer', `Connected to LSP server on port ${LSP_PORT}`);
                        resolve({
                            reader: socket,
                            writer: socket
                        });
                    });
                    
                    socket.on('error', (error: any) => {
                        LOG.error('LanguageServer', `Socket connection failed: ${error}`);
                        reject(error);
                    });
                }, 1000); // Wait 1 second for server to start
            });
        };

        LOG.debug('LanguageServer', 'Server options configured: ' + JSON.stringify({
            command: serverCommand,
            transport: 'socket',
            port: LSP_PORT
        }));

        // Debug: Check if we can find the LSP server process after starting
        async function checkLSPProcess(): Promise<void> {
            try {
                const { exec } = require('child_process');
                const result = await new Promise<string>((resolve, reject) => {
                    exec('tasklist /FI "IMAGENAME eq cryo-lsp.exe" /FO CSV', (error: any, stdout: string, stderr: string) => {
                        if (error) {
                            reject(error);
                        } else {
                            resolve(stdout);
                        }
                    });
                });
                
                if (result.includes('cryo-lsp.exe')) {
                    LOG.info('LanguageServer', 'LSP server process found running: ' + result.split('\n')[1]);
                } else {
                    LOG.error('LanguageServer', 'LSP server process NOT found in task list');
                }
            } catch (error) {
                LOG.error('LanguageServer', 'Failed to check LSP process: ' + String(error));
            }
        }

        // Client options - simplified configuration for better compatibility
        const clientOptions: LanguageClientOptions = {
            documentSelector: [
                { scheme: 'file', language: 'cryo' }
            ],
            // Simplified connection options
            connectionOptions: {
                maxRestartCount: 0 // Disable auto-restart to prevent conflicts
            },
            // Standard error handler
            errorHandler: {
                error: (error, message, count) => {
                    LOG.error('LanguageServer', `LSP Error (count: ${count}): ${error}, message: ${message}`);
                    LOG.error('LanguageServer', `Error details: ${JSON.stringify(error)}`);
                    
                    // Log raw error information for debugging
                    if (error && typeof error === 'object') {
                        LOG.error('LanguageServer', `Error name: ${(error as any).name}`);
                        LOG.error('LanguageServer', `Error message: ${(error as any).message}`);
                        LOG.error('LanguageServer', `Error stack: ${(error as any).stack}`);
                    }
                    
                    // Only shutdown on repeated errors (more than 3)
                    if (count && count > 3) {
                        return { action: ErrorAction.Shutdown };
                    }
                    return { action: ErrorAction.Continue };
                },
                closed: () => {
                    LOG.info('LanguageServer', 'LSP connection closed');
                    return { action: CloseAction.DoNotRestart };
                }
            }
        };

        LOG.info('LanguageServer', 'Creating LanguageClient with options: ' + JSON.stringify({ documentSelector: clientOptions.documentSelector }));

        // Create the language client
        client = new LanguageClient(
            'cryoLanguageServer',
            'Cryo Language Server',
            serverOptions,
            clientOptions
        );

        // Add event listeners for debugging and shutdown handling
        client.onDidChangeState((event) => {
            LOG.debug('LanguageServer', `Client state changed from ${event.oldState} to ${event.newState}`);
            LOG.debug('LanguageServer', `State meanings: 1=Stopped, 2=Starting, 3=Running`);
            LOG.debug('LanguageServer', `Current time: ${new Date().toISOString()}`);

            // If the client stops unexpectedly and we didn't initiate it, mark as stopped
            if (event.newState === 1 && !clientStopped) { // State 1 = Stopped
                LOG.info('LanguageServer', 'Language server stopped unexpectedly, setting clientStopped flag to prevent auto-restart');
                clientStopped = true;
                vscode.window.showWarningMessage('Cryo Language Server stopped unexpectedly. Use "Cryo: Restart Language Server" to restart.');
            }
        });

        // Add debugging for LSP lifecycle events
        LOG.debug('LanguageServer', 'Setting up LSP lifecycle monitoring...');

        // DO NOT show output channel - this can interfere with stdio transport
        // client.outputChannel.show(); // REMOVED - potential stdio interference
        client.outputChannel.appendLine('=== Cryo Language Server Starting ===');

        try {
            // Start the client and wait for initialization to complete
            LOG.info('LanguageServer', 'Starting Cryo Language Client...');
            LOG.debug('LanguageServer', `Client instance created: ${!!client}`);
            LOG.debug('LanguageServer', `Server command: ${serverCommand}`);
            LOG.debug('LanguageServer', `Client initial state: ${client.state}`);

            // ALTERNATIVE APPROACH: Don't wait for client.start() promise
            // VS Code LanguageClient start() promise seems to hang even when state=3
            // Instead, monitor state changes and consider client ready when state=3
            
            LOG.info('LanguageServer', 'Starting client and monitoring state changes...');
            LOG.debug('LanguageServer', `Starting client.start() at: ${new Date().toISOString()}`);
            
            // Track if client becomes ready via state change
            let clientReady = false;
            
            // Monitor state changes - when state becomes 3, client is ready
            const stateMonitor = client.onDidChangeState((event) => {
                LOG.debug('LanguageServer', `State change during startup: ${event.oldState} -> ${event.newState}`);
                if (event.newState === 3 && !clientReady) { // State 3 = Running
                    LOG.info('LanguageServer', 'Client reached Running state - considering initialization complete');
                    clientReady = true;
                }
            });
            
            // Start the client (don't await the promise since it hangs)
            const clientStartPromise = client.start().then(() => {
                LOG.info('LanguageServer', 'Client.start() promise finally resolved');
                return Promise.resolve();
            }).catch((error: any) => {
                LOG.error('LanguageServer', 'Client.start() promise rejected: ' + String(error));
                throw error;
            });
            
            // Wait for either:
            // 1. Client state becomes 3 (Running) - indicates client is ready
            // 2. 10 second timeout - if state never becomes 3
            // 3. client.start() promise resolves (unlikely but possible)
            
            const stateReadyPromise = new Promise<void>((resolve, reject) => {
                const checkInterval = setInterval(() => {
                    if (clientReady || (client && client.state === 3)) {
                        clearInterval(checkInterval);
                        clearTimeout(timeout);
                        stateMonitor.dispose();
                        LOG.info('LanguageServer', 'Client ready via state monitoring');
                        resolve();
                    }
                }, 100); // Check every 100ms
                
                const timeout = setTimeout(() => {
                    clearInterval(checkInterval);
                    stateMonitor.dispose();
                    LOG.error('LanguageServer', 'Client state monitoring timeout after 10 seconds');
                    LOG.error('LanguageServer', `Final client state: ${client?.state}`);
                    reject(new Error('Client state monitoring timeout - client never reached Running state'));
                }, 10000); // 10 second timeout
            });
            
            LOG.debug('LanguageServer', 'Racing between state monitoring and promise resolution...');
            
            // Race between state monitoring and client start promise
            await Promise.race([stateReadyPromise, clientStartPromise]);
            
            LOG.info('LanguageServer', 'Client initialization completed successfully');
            LOG.debug('LanguageServer', `Final client state: ${client?.state || 'undefined'}`);

            // Don't register a custom hover provider - let the Language Client handle it automatically
            LOG.info('Extension', 'Letting Language Client handle hover requests automatically...');

            LOG.info('LanguageServer', 'Cryo Language Server connection established');

            // Add debugging for client state
            if (client.state) {
                LOG.debug('LanguageServer', 'Client state after start: ' + client.state);
            }

            // Client started successfully
            LOG.info('LanguageServer', 'Setting global client reference for hover provider...');
            
            // Ensure the global client reference is set for hover provider
            if (client) {
                LOG.debug('LanguageServer', 'Global client reference successfully set');
                LOG.debug('LanguageServer', `Global client state: ${client.state}`);
            } else {
                LOG.error('LanguageServer', 'CRITICAL: Global client reference is null after successful start!');
            }
            LOG.info('LanguageServer', 'Language client connected to LSP server');

            vscode.window.showInformationMessage('Cryo Language Server started');

            LOG.info('LanguageServer', 'Language client initialization complete');

            // Debug: Check if LSP server process is actually running
            setTimeout(async () => {
                await checkLSPProcess();
            }, 1000); // Wait 1 second for process to show up

            // Debug: Check if any .cryo files are currently open  
            const openDocuments = vscode.workspace.textDocuments.filter(doc => doc.languageId === 'cryo');
            LOG.info('LanguageServer', `Found ${openDocuments.length} open .cryo documents`);
            for (const doc of openDocuments) {
                LOG.info('LanguageServer', `Open document: ${doc.fileName} (languageId: ${doc.languageId})`);
                LOG.debug('LanguageServer', `Letting Language Client handle document sync automatically for: ${doc.fileName}`);
            }

        } catch (error) {
            const errorMsg = 'Failed to start Cryo Language Server: ' + String(error);
            LOG.error('LanguageServer', errorMsg);
            vscode.window.showErrorMessage(errorMsg);
        }
    }

    // Start the language server initially
    startLanguageServer();

    // Add document event listeners for debugging
    const onDidOpenTextDocument = vscode.workspace.onDidOpenTextDocument((document) => {
        if (document.languageId === 'cryo') {
            LOG.info('Extension', `Document opened: ${document.fileName} (languageId: ${document.languageId})`);
            LOG.info('Extension', `Document URI: ${document.uri.toString()}`);
            LOG.info('Extension', `Client state: ${client?.state || 'no client'}`);

            // The Language Client will automatically handle didOpen - don't send duplicates
            LOG.debug('Extension', `Letting Language Client handle didOpen automatically for: ${document.fileName}`);
        }
    });

    const onDidChangeActiveTextEditor = vscode.window.onDidChangeActiveTextEditor((editor) => {
        if (editor && editor.document.languageId === 'cryo') {
            LOG.info('Extension', `Active editor changed to .cryo file: ${editor.document.fileName}`);
            LOG.info('Extension', `Client state: ${client?.state || 'no client'}`);

            // Don't send redundant didOpen notifications - the Language Client handles document lifecycle
            LOG.debug('Extension', `Active editor changed, Language Client will handle document sync for: ${editor.document.fileName}`);
        }
    });

    context.subscriptions.push(onDidOpenTextDocument, onDidChangeActiveTextEditor);

    // Add a hover provider for debugging - this will run even if LSP is not working
    const debugHoverProvider = vscode.languages.registerHoverProvider('cryo', {
        async provideHover(document, position, token) {
            LOG.debug('HoverProvider', '=== HOVER REQUEST DETECTED ===');
            LOG.debug('HoverProvider', `Document: ${document.fileName}`);
            LOG.debug('HoverProvider', `Position: line ${position.line}, character ${position.character}`);
            LOG.debug('HoverProvider', `Client state: ${client?.state || 'no client'}`);
            
            if (!client || client.state !== 3) {
                LOG.info('HoverProvider', 'LSP client not ready - returning null for hover');
                return null;
            }
            
            LOG.debug('HoverProvider', 'LSP client is ready - attempting direct LSP hover request...');
            
            try {
                // Try to send a direct hover request to the LSP server
                const hoverRequest = {
                    textDocument: {
                        uri: document.uri.toString()
                    },
                    position: {
                        line: position.line,
                        character: position.character
                    }
                };
                
                LOG.debug('HoverProvider', 'Sending textDocument/hover request to LSP server...');
                
                // Add timeout to the LSP request since it might hang indefinitely
                const timeoutPromise = new Promise<never>((_, reject) => {
                    setTimeout(() => {
                        reject(new Error('LSP hover request timeout after 3 seconds'));
                    }, 3000);
                });
                
                // Race between LSP request and timeout
                const hoverResponse: any = await Promise.race([
                    client.sendRequest('textDocument/hover', hoverRequest),
                    timeoutPromise
                ]);
                
                LOG.info('HoverProvider', 'Received hover response from LSP server: ' + JSON.stringify(hoverResponse));
                
                if (hoverResponse && hoverResponse.contents) {
                    // Convert LSP hover response to VS Code hover
                    const contents = Array.isArray(hoverResponse.contents) 
                        ? hoverResponse.contents 
                        : [hoverResponse.contents];
                    
                    return new vscode.Hover(contents.map((content: any) => {
                        if (typeof content === 'string') {
                            return content;
                        } else if (content.value) {
                            return new vscode.MarkdownString(content.value);
                        }
                        return String(content);
                    }));
                }
                
                // If no hover response, fall back to debug hover
                LOG.info('HoverProvider', 'No hover response from LSP server, providing debug hover');
                
            } catch (error) {
                LOG.error('HoverProvider', 'Failed to get hover from LSP server: ' + String(error));
                LOG.debug('HoverProvider', 'Error details: ' + JSON.stringify(error));
                
                // Check if it's a timeout error specifically
                if (String(error).includes('timeout')) {
                    LOG.error('HoverProvider', 'LSP request timed out - server communication is broken');
                } else {
                    LOG.error('HoverProvider', 'LSP request failed with error: ' + String(error));
                }
            }
            
            // Fallback: Provide debug hover with LSP attempt status
            const word = document.getText(document.getWordRangeAtPosition(position));
            const testHover = new vscode.Hover([
                `**CryoLang Debug Hover**`,
                `Symbol: \`${word}\``,
                `Position: Line ${position.line + 1}, Column ${position.character + 1}`,
                `Client State: ${client.state} (Running)`,
                `LSP Request: ⏱️ Request sent but timed out (server communication broken)`,
                `---`,
                `*Debug hover - LSP stdio pipe not working correctly.*`
            ]);
            
            LOG.info('HoverProvider', `Providing debug hover for symbol: ${word}`);
            return testHover;
        }
    });

    context.subscriptions.push(debugHoverProvider);

    // Command to restart the language server (for development)
    const restartCommand = vscode.commands.registerCommand('cryo.restartLanguageServer', async () => {
        vscode.window.showInformationMessage('Restarting Cryo Language Server...');
        LOG.info('Commands', 'Manual restart command initiated');

        try {
            // Reset the stopped flag to allow restart
            clientStopped = false;

            // Stop existing client if running
            if (client) {
                LOG.info('Commands', 'Stopping existing client for restart...');
                try {
                    await client.stop(5000);
                    client.dispose();
                    client = undefined;
                } catch (stopError) {
                    LOG.error('Commands', 'Error stopping client during restart: ' + String(stopError));
                }
            }

            // Small delay to ensure cleanup
            await new Promise(resolve => setTimeout(resolve, 1000));

            // Start fresh
            await startLanguageServer();
            vscode.window.showInformationMessage('Cryo Language Server restarted successfully!');
            LOG.info('Commands', 'Manual restart completed successfully');
        } catch (error) {
            LOG.error('Commands', 'Error restarting language server: ' + String(error));
            vscode.window.showErrorMessage(`Failed to restart language server: ${error}`);
        }
    });

    // Command to shutdown the language server (for development)
    const shutdownCommand = vscode.commands.registerCommand('cryo.shutdownLanguageServer', async () => {
        if (!client) {
            vscode.window.showWarningMessage('Cryo Language Server is not running.');
            LOG.info('Commands', 'Shutdown command called but no client is running');
            return;
        }

        try {
            vscode.window.showInformationMessage('Shutting down Cryo Language Server...');
            LOG.info('Commands', 'Manual shutdown command initiated');

            // Set flag to prevent auto-restart BEFORE stopping the client
            clientStopped = true;

            LOG.info('Commands', 'Sending shutdown request to LSP server...');

            // Stop the client with a longer timeout to ensure proper shutdown
            await client.stop(5000); // 5 second timeout
            client.dispose();
            client = undefined;

            // Add a small delay to ensure the process has fully terminated
            await new Promise(resolve => setTimeout(resolve, 2000));

            vscode.window.showInformationMessage('Cryo Language Server shut down successfully!');
            LOG.info('Commands', 'LSP server shutdown complete - process should be released');
        } catch (error) {
            LOG.error('Commands', 'Error shutting down language server: ' + String(error));
            vscode.window.showErrorMessage(`Failed to shutdown language server: ${error}`);
        }
    });

    // Command to completely stop the language client (for development builds)
    const stopCommand = vscode.commands.registerCommand('cryo.stopLanguageClient', async () => {
        try {
            vscode.window.showInformationMessage('Stopping Cryo Language Client for development...');
            console.log('Stopping language client for development build...');

            clientStopped = true; // Prevent any restart

            if (client) {
                console.log('Forcibly stopping language client...');

                // Try to stop gracefully first
                try {
                    await client.stop(2000); // Shorter timeout
                } catch (stopError) {
                    console.log('Graceful stop failed, disposing directly:', stopError);
                }

                // Always dispose
                client.dispose();
                client = undefined;
                console.log('Client disposed');
            } else {
                console.log('No active client to stop');
            }

            // Longer delay to ensure process cleanup
            await new Promise(resolve => setTimeout(resolve, 5000));

            vscode.window.showInformationMessage('Cryo Language Client stopped - binary is now free for rebuild!');
            console.log('Language client stopped - binary file should be unlocked.');
        } catch (error) {
            console.error('Error stopping language client:', error);
            vscode.window.showErrorMessage(`Failed to stop language client: ${error}`);
        }
    });

    // Command to completely disable the extension (nuclear option for development)
    const disableCommand = vscode.commands.registerCommand('cryo.disableExtension', async () => {
        try {
            vscode.window.showWarningMessage('Disabling Cryo Language Extension completely for development...');
            console.log('Disabling extension completely...');

            clientStopped = true; // Prevent any restart

            if (client) {
                try {
                    await client.stop(1000);
                } catch (error) {
                    console.log('Stop failed, continuing with dispose:', error);
                }
                client.dispose();
                client = undefined;
            }

            // Clear all subscriptions to fully disable
            context.subscriptions.forEach(subscription => {
                try {
                    subscription.dispose();
                } catch (error) {
                    console.log('Error disposing subscription:', error);
                }
            });

            await new Promise(resolve => setTimeout(resolve, 2000));

            vscode.window.showInformationMessage('Cryo Extension disabled - LSP binary is completely free!');
            console.log('Extension disabled - no more LSP processes should start.');
        } catch (error) {
            console.error('Error disabling extension:', error);
            vscode.window.showErrorMessage(`Failed to disable extension: ${error}`);
        }
    });

    context.subscriptions.push(restartCommand);
    context.subscriptions.push(shutdownCommand);
    context.subscriptions.push(stopCommand);
    context.subscriptions.push(disableCommand);
    context.subscriptions.push(editorChangeDisposable);
}

export async function deactivate(): Promise<void> {
    if (client) {
        try {
            console.log('Deactivating extension, stopping client...');
            await client.stop(5000); // 5 second timeout
            client.dispose();
        } catch (error) {
            console.log('Error during deactivation:', error);
        }
    }
}
