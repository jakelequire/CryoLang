import * as vscode from 'vscode';

export type ServerStatus = 'starting' | 'ready' | 'error' | 'disabled' | 'stopped';

let statusBarItem: vscode.StatusBarItem | undefined;

export function createStatusBar(): vscode.StatusBarItem {
    statusBarItem = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Right,
        100
    );
    statusBarItem.command = 'cryo.showOutputChannel';
    updateStatus('stopped');
    statusBarItem.show();
    return statusBarItem;
}

export function updateStatus(status: ServerStatus): void {
    if (!statusBarItem) return;

    switch (status) {
        case 'starting':
            statusBarItem.text = '$(sync~spin) CryoLSP';
            statusBarItem.tooltip = 'CryoLSP: Starting...';
            statusBarItem.backgroundColor = undefined;
            break;
        case 'ready':
            statusBarItem.text = '$(check) CryoLSP';
            statusBarItem.tooltip = 'CryoLSP: Ready';
            statusBarItem.backgroundColor = undefined;
            break;
        case 'error':
            statusBarItem.text = '$(error) CryoLSP';
            statusBarItem.tooltip = 'CryoLSP: Error - Click to view output';
            statusBarItem.backgroundColor = new vscode.ThemeColor(
                'statusBarItem.errorBackground'
            );
            break;
        case 'disabled':
            statusBarItem.text = '$(circle-slash) CryoLSP';
            statusBarItem.tooltip = 'CryoLSP: Disabled';
            statusBarItem.backgroundColor = undefined;
            break;
        case 'stopped':
            statusBarItem.text = '$(debug-stop) CryoLSP';
            statusBarItem.tooltip = 'CryoLSP: Stopped';
            statusBarItem.backgroundColor = undefined;
            break;
    }
}

export function disposeStatusBar(): void {
    if (statusBarItem) {
        statusBarItem.dispose();
        statusBarItem = undefined;
    }
}
