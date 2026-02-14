import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';

export interface CryoConfig {
    enabled: boolean;
    serverPath: string;
    debug: boolean;
    logFile: string;
}

export function getConfig(): CryoConfig {
    const config = vscode.workspace.getConfiguration('cryo.languageServer');
    return {
        enabled: config.get<boolean>('enabled', true),
        serverPath: config.get<string>('path', ''),
        debug: config.get<boolean>('debug', false),
        logFile: config.get<string>('logFile', ''),
    };
}

export function resolveServerPath(extensionPath: string): string | undefined {
    const config = getConfig();

    // 1. Check user-configured path
    if (config.serverPath) {
        if (fs.existsSync(config.serverPath)) {
            return config.serverPath;
        }
        vscode.window.showWarningMessage(
            `CryoLSP: Configured server path not found: ${config.serverPath}`
        );
    }

    // 2. Check workspace root bin/
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            const candidates = [
                path.join(folder.uri.fsPath, 'bin', 'cryolsp.exe'),
                path.join(folder.uri.fsPath, 'bin', 'cryolsp'),
            ];
            for (const candidate of candidates) {
                if (fs.existsSync(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // 3. Check relative to extension
    const extBinCandidates = [
        path.join(extensionPath, '..', '..', 'bin', 'cryolsp.exe'),
        path.join(extensionPath, '..', '..', 'bin', 'cryolsp'),
    ];
    for (const candidate of extBinCandidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }

    return undefined;
}
