import * as vscode from 'vscode';
import * as path from 'path';
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext) {
    console.log('Cryo Language Support extension is now active!');

    // Try multiple possible paths for the LSP server executable
    const possiblePaths = [
        path.join(context.extensionPath, '..', '..', '..', 'bin', 'cryo-lsp.exe'), // Development
        path.join(context.extensionPath, 'bin', 'cryo-lsp.exe'), // Bundled
        'C:\\Programming\\apps\\CryoLang\\bin\\cryo-lsp.exe', // Absolute path
    ];
    
    let serverCommand = '';
    const fs = require('fs');
    
    for (const possiblePath of possiblePaths) {
        if (fs.existsSync(possiblePath)) {
            serverCommand = possiblePath;
            console.log('Found LSP server at:', serverCommand);
            break;
        }
    }
    
    if (!serverCommand) {
        vscode.window.showErrorMessage('Cryo LSP Server not found. Please ensure cryo-lsp.exe is built.');
        return;
    }

    async function startLanguageServer() {
        // If client already exists, stop it first
        if (client) {
            try {
                await client.stop();
                client.dispose();
            } catch (error) {
                console.log('Error stopping previous client:', error);
            }
        }

        // Server options
        const serverOptions: ServerOptions = {
            command: serverCommand,
            args: [],
            transport: TransportKind.stdio
        };

        // Client options
        const clientOptions: LanguageClientOptions = {
            documentSelector: [{ scheme: 'file', language: 'cryo' }],
            synchronize: {
                fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cryo')
            }
        };

        // Create the language client
        client = new LanguageClient(
            'cryoLanguageServer',
            'Cryo Language Server',
            serverOptions,
            clientOptions
        );

        try {
            // Start the client
            await client.start();
            console.log('Cryo Language Server started successfully');
            vscode.window.showInformationMessage('Cryo Language Server started');
        } catch (error) {
            console.error('Failed to start Cryo Language Server:', error);
            vscode.window.showErrorMessage(`Failed to start Cryo Language Server: ${error}`);
        }
    }

    // Start the language server initially
    startLanguageServer();

    // Command to restart the language server (for development)
    const restartCommand = vscode.commands.registerCommand('cryo.restartLanguageServer', async () => {
        vscode.window.showInformationMessage('Restarting Cryo Language Server...');
        
        try {
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
            vscode.window.showWarningMessage('Cryo Language Server is not running.');
            return;
        }

        try {
            vscode.window.showInformationMessage('Shutting down Cryo Language Server...');
            console.log('Sending shutdown request to LSP server...');
            
            await client.stop();
            client.dispose();
            client = undefined;
            
            vscode.window.showInformationMessage('Cryo Language Server shut down successfully!');
        } catch (error) {
            console.error('Error shutting down language server:', error);
            vscode.window.showErrorMessage(`Failed to shutdown language server: ${error}`);
        }
    });

    context.subscriptions.push(restartCommand);
    context.subscriptions.push(shutdownCommand);
}

export async function deactivate(): Promise<void> {
    if (client) {
        try {
            await client.stop();
            client.dispose();
        } catch (error) {
            console.log('Error during deactivation:', error);
        }
    }
}
