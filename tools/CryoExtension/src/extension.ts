import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as net from 'net';
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind } from 'vscode-languageclient/node';

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

    // Try multiple possible paths for the LSP server executable
    const possiblePaths = [
        path.join(context.extensionPath, '..', '..', '..', 'bin', 'cryo-lsp.exe'), // Development
        path.join(context.extensionPath, 'bin', 'cryo-lsp.exe'), // Bundled
        'C:\\Programming\\apps\\CryoLang\\bin\\cryo-lsp.exe', // Absolute path
    ];
    
    let serverCommand = '';
    
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

        LOG.info('LanguageServer', 'Configuring server options for LSP communication');

        // Use our C++ LSP server
        const serverOptions: ServerOptions = {
            command: serverCommand,
            options: {
                env: process.env,
                cwd: workspacePath
            }
        };

        // Client options
        const clientOptions: LanguageClientOptions = {
            documentSelector: [{ scheme: 'file', language: 'cryo' }],
            synchronize: {
                fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cryo')
            },
            // Disable automatic restart
            connectionOptions: {
                maxRestartCount: 0, // Never restart automatically
            },
            // Better initialization handling
            initializationOptions: {},
            diagnosticCollectionName: 'cryo',
            outputChannelName: 'Cryo Language Server',
            // Add custom initialization handling to ensure initialized notification is sent
            initializationFailedHandler: (error) => {
                LOG.error('LanguageServer', 'LSP initialization failed: ' + String(error));
                return false;
            },
            errorHandler: {
                error: (error, message, count) => {
                    LOG.error('LanguageServer', `LSP error ${count}: ${error}, message: ${message}`);
                    return { action: 'Continue' as any };
                },
                closed: () => {
                    LOG.error('LanguageServer', 'LSP connection closed');
                    return { action: 'DoNotRestart' as any };
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
        
        // Add event listeners for debugging
        client.onDidChangeState((event) => {
            LOG.debug('LanguageServer', `Client state changed from ${event.oldState} to ${event.newState}`);
        });

        try {
            // Start the client - with timeout to prevent hanging
            LOG.info('LanguageServer', 'Starting Cryo Language Client...');
            
            // Wait for client to start properly - no timeout
            LOG.info('LanguageServer', 'Client.start() promise created, awaiting...');
            await client.start();
            LOG.info('LanguageServer', 'Client.start() completed successfully - LSP handshake complete');
            
            LOG.info('LanguageServer', 'Cryo Language Server connection established');
            
            // Add debugging for client state
            if (client.state) {
                LOG.debug('LanguageServer', 'Client state after start: ' + client.state);
            }
            
            // Client started successfully
            LOG.info('LanguageServer', 'Language client connected to LSP server');
            
            // Note: Hover functionality is handled entirely by the LSP server
            // The LanguageClient automatically registers hover provider capabilities
            
            // Note: Hover functionality is handled entirely by the LSP server
            // No client-side hover provider needed
            
            vscode.window.showInformationMessage('Cryo Language Server started');
            
            // Give the server a moment to fully initialize
            await new Promise(resolve => setTimeout(resolve, 1000));
            LOG.info('LanguageServer', 'Language client initialization complete');
            
            // Debug: Check if any .cryo files are currently open
            const openDocuments = vscode.workspace.textDocuments.filter(doc => doc.languageId === 'cryo');
            LOG.info('LanguageServer', `Found ${openDocuments.length} open .cryo documents`);
            for (const doc of openDocuments) {
                LOG.info('LanguageServer', `Open document: ${doc.fileName} (languageId: ${doc.languageId})`);
                
                // Manually trigger document sync if the client is running
                if (client && client.state === 3) { // State 3 = Running
                    try {
                        LOG.info('LanguageServer', `Manually sending textDocument/didOpen for: ${doc.fileName}`);
                        client.sendNotification('textDocument/didOpen', {
                            textDocument: {
                                uri: doc.uri.toString(),
                                languageId: doc.languageId,
                                version: doc.version,
                                text: doc.getText()
                            }
                        });
                        LOG.info('LanguageServer', `Successfully sent didOpen notification for: ${doc.fileName}`);
                    } catch (error) {
                        LOG.error('LanguageServer', `Failed to send didOpen for ${doc.fileName}: ${String(error)}`);
                    }
                }
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
            
            // Force send didOpen if client is ready
            if (client && client.state === 3) {
                try {
                    LOG.info('Extension', `Force-sending didOpen for newly opened document: ${document.fileName}`);
                    client.sendNotification('textDocument/didOpen', {
                        textDocument: {
                            uri: document.uri.toString(),
                            languageId: document.languageId,
                            version: (document as any).version || 1,
                            text: document.getText()
                        }
                    });
                    LOG.info('Extension', `Successfully sent didOpen for: ${document.fileName}`);
                } catch (error) {
                    LOG.error('Extension', `Failed to send didOpen: ${String(error)}`);
                }
            }
        }
    });
    
    const onDidChangeActiveTextEditor = vscode.window.onDidChangeActiveTextEditor((editor) => {
        if (editor && editor.document.languageId === 'cryo') {
            LOG.info('Extension', `Active editor changed to .cryo file: ${editor.document.fileName}`);
            LOG.info('Extension', `Client state: ${client?.state || 'no client'}`);
            
            // Also try to send didOpen when switching to a cryo file
            if (client && client.state === 3) {
                try {
                    LOG.info('Extension', `Ensuring didOpen sent for active editor: ${editor.document.fileName}`);
                    client.sendNotification('textDocument/didOpen', {
                        textDocument: {
                            uri: editor.document.uri.toString(),
                            languageId: editor.document.languageId,
                            version: (editor.document as any).version || 1,
                            text: editor.document.getText()
                        }
                    });
                    LOG.info('Extension', `didOpen sent for active editor: ${editor.document.fileName}`);
                } catch (error) {
                    LOG.error('Extension', `Failed to send didOpen for active editor: ${String(error)}`);
                }
            }
        }
    });
    
    context.subscriptions.push(onDidOpenTextDocument, onDidChangeActiveTextEditor);
    
    // Command to restart the language server (for development)
    const restartCommand = vscode.commands.registerCommand('cryo.restartLanguageServer', async () => {
        vscode.window.showInformationMessage('Restarting Cryo Language Server...');
        
        try {
            clientStopped = false; // Allow restart
            await startLanguageServer();
            vscode.window.showInformationMessage('Cryo Language Server restarted successfully!');
        } catch (error) {
            console.error('Error restarting language server:', error);
            vscode.window.showErrorMessage(`Failed to restart language server: ${error}`);
        }
    });

    // Command to shutdown the language server (for development)
    const shutdownCommand = vscode.commands.registerCommand('cryo.shutdownLanguageServer', async () => {
        if (!client) {
            vscode.window.showWarningMessage('Cryo Language Server is not rrunning.');
            return;
        }

        try {
            vscode.window.showInformationMessage('Shutting down Cryo Language Server...');
            console.log('Sending shutdown request to LSP server...');
            
            clientStopped = true; // Prevent auto-restart
            
            // Stop the client with a longer timeout to ensure proper shutdown
            await client.stop(5000); // 5 second timeout
            client.dispose();
            client = undefined;
            
            // Add a small delay to ensure the process has fully terminated
            await new Promise(resolve => setTimeout(resolve, 2000));
            
            vscode.window.showInformationMessage('Cryo Language Server shut down successfully!');
            console.log('LSP server shutdown complete - process should be released.');
        } catch (error) {
            console.error('Error shutting down language server:', error);
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
