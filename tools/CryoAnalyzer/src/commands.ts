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
}
