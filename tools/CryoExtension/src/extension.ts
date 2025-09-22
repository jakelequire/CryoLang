import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
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

        LOG.info('LanguageServer', 'Configuring server options with command: ' + serverCommand);

        // Server options
        const serverOptions: ServerOptions = {
            command: serverCommand,
            args: [],
            transport: TransportKind.stdio,
            options: {
                env: process.env
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
            outputChannelName: 'Cryo Language Server'
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
            // Start the client (remove timeout that might interfere with handshake)
            LOG.info('LanguageServer', 'Starting Cryo Language Client...');
            
            await client.start();
            
            LOG.info('LanguageServer', 'Cryo Language Server started successfully');
            
            // Add debugging for client state
            if (client.state) {
                LOG.debug('LanguageServer', 'Client state after start: ' + client.state);
            }
            
            // Client started successfully
            LOG.info('LanguageServer', 'Language client connected to LSP server');
            
            vscode.window.showInformationMessage('Cryo Language Server started');
            
            // Give the server a moment to fully initialize
            await new Promise(resolve => setTimeout(resolve, 1000));
            LOG.info('LanguageServer', 'Language client initialization complete');
            
        } catch (error) {
            const errorMsg = 'Failed to start Cryo Language Server: ' + String(error);
            LOG.error('LanguageServer', errorMsg);
            vscode.window.showErrorMessage(errorMsg);
        }
    }

    // Start the language server initially
    startLanguageServer();
    
    // Debug: Remove manual hover provider so LSP hover can work
    // The manual hover provider was intercepting requests before they reached the LSP
    LOG.info('Extension', 'Manual hover provider disabled - using LSP hover instead');
    
    // TEMPORARY: Add a basic hover provider to test functionality
    const hoverProvider = vscode.languages.registerHoverProvider('cryo', {
        provideHover(document: vscode.TextDocument, position: vscode.Position): vscode.ProviderResult<vscode.Hover> {
            // Get the word at position
            const range = document.getWordRangeAtPosition(position);
            const word = document.getText(range);
            
            // Built-in types with clean syntax definitions
            const builtinTypes: { [key: string]: { definition: string; description?: string } } = {
                'int': { 
                    definition: 'type int = i32;',
                    description: 'Signed 32-bit integer\n\nRange: -2,147,483,648 to 2,147,483,647'
                },
                'i32': { 
                    definition: 'type i32;',
                    description: 'Signed 32-bit integer'
                },
                'i64': { 
                    definition: 'type i64;',
                    description: 'Signed 64-bit integer'
                },
                'string': { 
                    definition: 'type string;',
                    description: 'UTF-8 encoded text string\n\nUse for text, names, and string operations'
                },
                'boolean': { 
                    definition: 'type boolean = bool;',
                    description: 'Boolean value: true or false\n\nUse for logical operations and conditions'
                },
                'bool': { 
                    definition: 'type bool;',
                    description: 'Boolean value: true or false'
                },
                'float': { 
                    definition: 'type float = f32;',
                    description: '32-bit floating-point number\n\nPrecision: ~7 decimal digits'
                },
                'f32': { 
                    definition: 'type f32;',
                    description: '32-bit floating-point number'
                },
                'f64': { 
                    definition: 'type f64;',
                    description: '64-bit floating-point number'
                },
                'char': { 
                    definition: 'type char;',
                    description: 'UTF-8 encoded character'
                },
                'true': { 
                    definition: 'const true: boolean = true;',
                    description: 'Boolean literal representing logical true'
                },
                'false': { 
                    definition: 'const false: boolean = false;',
                    description: 'Boolean literal representing logical false'
                },
                'void': { 
                    definition: 'type void;',
                    description: 'No value type\n\nUsed for functions that don\'t return a value'
                }
            };
            
            if (builtinTypes[word]) {
                const typeInfo = builtinTypes[word];
                let hoverContent = '```cryo\n' + typeInfo.definition + '\n```';
                if (typeInfo.description) {
                    hoverContent += '\n\n' + typeInfo.description;
                }
                return new vscode.Hover(new vscode.MarkdownString(hoverContent));
            }
            
            // Try to find variable/function definitions in the current document
            const text = document.getText();
            const lines = text.split('\n');
            
            // Look for variable declarations
            const varPattern = new RegExp(`\\b(?:const|mut)\\s+${word}\\s*:\\s*([^=\\s;]+)`, 'g');
            const varMatch = varPattern.exec(text);
            if (varMatch) {
                const variableType = varMatch[1].trim();
                const hoverContent = '```cryo\nconst ' + word + ': ' + variableType + '\n```';
                return new vscode.Hover(new vscode.MarkdownString(hoverContent));
            }
            
            // Look for function definitions
            const funcPattern = new RegExp(`\\bfunction\\s+${word}\\s*\\([^)]*\\)\\s*(?:->\\s*[^{\\s]+)?`, 'g');
            const funcMatch = funcPattern.exec(text);
            if (funcMatch) {
                const funcSignature = funcMatch[0].trim() + ';';
                const hoverContent = '```cryo\n' + funcSignature + '\n```';
                return new vscode.Hover(new vscode.MarkdownString(hoverContent));
            }
            
            // Look for enum declarations
            const enumPattern = new RegExp(`\\benum\\s+${word}\\s*\\{`, 'g');
            const enumMatch = enumPattern.exec(text);
            if (enumMatch) {
                const hoverContent = '```cryo\nenum ' + word + '\n```';
                return new vscode.Hover(new vscode.MarkdownString(hoverContent));
            }
            
            // Look for struct/class declarations
            const structPattern = new RegExp(`\\b(?:struct|class)\\s+${word}\\s*(?:<[^>]*>)?\\s*\\{`, 'g');
            const structMatch = structPattern.exec(text);
            if (structMatch) {
                const declaration = structMatch[0].replace('{', '').trim();
                const hoverContent = '```cryo\n' + declaration + '\n```';
                return new vscode.Hover(new vscode.MarkdownString(hoverContent));
            }
            
            return null;
        }
    });
    
    context.subscriptions.push(hoverProvider);

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
