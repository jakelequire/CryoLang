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

/**
 * Locate the `cryolsp` binary.  Resolution order:
 *
 *   1. `cryo.languageServer.path` user setting.  Relative paths are
 *      resolved against the first workspace folder so settings like
 *      `./bin/cryolsp` work without expanding to an absolute path.
 *   2. `$CRYO_HOME` - the same install-root env var the compiler reads
 *      for stdlib lookup (see compiler/src/compiler/instance.cryo).
 *      Probed at `$CRYO_HOME/bin/cryolsp` (FHS-style install) and
 *      `$CRYO_HOME/tools/CryoLSP/build/cryolsp` (when CRYO_HOME
 *      points at a source checkout).
 *   3. Workspace-local `tools/CryoLSP/build/cryolsp` - the output of
 *      `cryo build` (which emits into `output_dir`, = "build") inside
 *      the in-repo CryoLSP project.  Picks up dev builds without
 *      needing any setting changes.  The legacy `build/bin/` layout is
 *      also probed as a fallback for older packaged installs.
 *   4. Workspace-local `bin/cryolsp` - legacy install location for
 *      older CryoLSP packages; kept so existing users don't break.
 *   5. `cryolsp` on `$PATH`.
 *   6. Sibling of `cryo` on `$PATH`.  We resolve the `cryo` binary
 *      (following symlinks) and probe `<cryo-dir>/cryolsp` plus the
 *      in-repo `tools/CryoLSP/build/bin/cryolsp` location relative to
 *      the resolved repo root - handles the common setup where
 *      install.sh symlinks /usr/local/bin/cryo into a source checkout.
 *   7. Extension-relative paths (sibling CryoLSP/ tree, then the
 *      legacy `../../bin/` location).
 *
 * If a user-configured path is set but doesn't exist, the resolver
 * SILENTLY falls through to auto-detection.  A warning is logged to
 * the output channel only if the eventual return value is `undefined`
 * - otherwise stale settings (e.g. a Windows `.exe` path on Linux)
 * spam a popup on every restart even when auto-detection succeeds.
 *
 * `.exe` candidates are only probed on Windows; Linux/macOS skip them
 * to avoid wasted `existsSync` calls and to avoid suggesting a path
 * that would mis-spawn on the wrong platform.
 */
export function resolveServerPath(
    extensionPath: string,
    outputChannel?: vscode.OutputChannel
): string | undefined {
    const config = getConfig();
    const isWindows = process.platform === 'win32';
    const exeName = isWindows ? 'cryolsp.exe' : 'cryolsp';

    // 1. User-configured path.  Resolve relative paths against the
    //    first workspace folder so `./bin/cryolsp` is interpreted the
    //    way users expect (relative to the project root, not VS Code's
    //    CWD).  An absolute path is returned as-is when it exists.
    if (config.serverPath) {
        const resolved = resolveUserPath(config.serverPath);
        if (resolved && fs.existsSync(resolved)) {
            return resolved;
        }
        outputChannel?.appendLine(
            `CryoLSP: configured server path not found, falling back to ` +
            `auto-detection: ${config.serverPath}`
        );
    }

    const candidates: string[] = [];
    const pushPair = (...parts: string[]) => {
        candidates.push(path.join(...parts, exeName));
        // On Linux/macOS where the user's configured path included
        // `.exe` accidentally, also try the no-extension form.  The
        // exeName already handled the canonical case; this is a safety
        // net for the inverse mistake.
        if (isWindows) {
            candidates.push(path.join(...parts, 'cryolsp'));
        }
    };

    // 2. $CRYO_HOME - same env var the compiler uses for stdlib lookup.
    const cryoHome = process.env.CRYO_HOME;
    if (cryoHome) {
        pushPair(cryoHome, 'bin');
        // `cryo build` emits into `output_dir` (= "build") directly, so
        // the binary is at build/cryolsp; build/bin/ is a legacy layout
        // kept as a fallback for older packaged installs.
        pushPair(cryoHome, 'tools', 'CryoLSP', 'build');
        pushPair(cryoHome, 'tools', 'CryoLSP', 'build', 'bin');
    }

    // 3 + 4. Workspace-relative candidates.
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            const root = folder.uri.fsPath;
            pushPair(root, 'tools', 'CryoLSP', 'build');
            pushPair(root, 'tools', 'CryoLSP', 'build', 'bin');
            pushPair(root, 'bin');
        }
    }

    // 5. `cryolsp` on $PATH.
    const onPath = findOnPath(exeName);
    if (onPath) {
        candidates.push(onPath);
    }

    // 6. Sibling of `cryo` on $PATH.  install.sh symlinks
    //    /usr/local/bin/cryo into <repo>/bin/cryo, so resolving the
    //    symlink gives us a way back to the source tree even when the
    //    workspace folder is unrelated.
    const cryoPath = findOnPath(isWindows ? 'cryo.exe' : 'cryo');
    if (cryoPath) {
        let cryoReal = cryoPath;
        try { cryoReal = fs.realpathSync(cryoPath); } catch { /* keep original */ }
        const cryoDir = path.dirname(cryoReal);
        pushPair(cryoDir);
        // <repo>/bin/cryo -> <repo>/tools/CryoLSP/build/cryolsp
        pushPair(cryoDir, '..', 'tools', 'CryoLSP', 'build');
        pushPair(cryoDir, '..', 'tools', 'CryoLSP', 'build', 'bin');
    }

    // 7. Extension-relative candidates.  When the extension lives at
    // `<repo>/tools/CryoAnalyzer/`, CryoLSP is a sibling at
    // `<repo>/tools/CryoLSP/`; legacy bin/ is two parents up.
    pushPair(extensionPath, '..', 'CryoLSP', 'build');
    pushPair(extensionPath, '..', 'CryoLSP', 'build', 'bin');
    pushPair(extensionPath, '..', '..', 'bin');

    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }

    return undefined;
}

/**
 * Search `$PATH` for `name` and return the first match.  Honours the
 * platform's PATH separator and `PATHEXT` on Windows.
 */
function findOnPath(name: string): string | undefined {
    const pathEnv = process.env.PATH;
    if (!pathEnv) { return undefined; }

    const sep = process.platform === 'win32' ? ';' : ':';
    const dirs = pathEnv.split(sep).filter((d) => d.length > 0);

    // Try the name verbatim first (empty ext), then append PATHEXT
    // variants.  Callers pass names that already include `.exe` on
    // Windows (e.g. `cryolsp.exe`), so without the leading '' the loop
    // would only ever probe `cryolsp.exe.EXE`, `cryolsp.exe.CMD`, ... and
    // never match the actual `cryolsp.exe` on disk.  The PATHEXT entries
    // remain for callers that pass a bare command name (`cryo`).
    const exts =
        process.platform === 'win32'
            ? ['', ...(process.env.PATHEXT?.split(';') ?? ['.EXE', '.CMD', '.BAT'])]
            : [''];

    for (const dir of dirs) {
        for (const ext of exts) {
            const candidate = path.join(dir, name + ext);
            if (fs.existsSync(candidate)) { return candidate; }
        }
    }
    return undefined;
}

/**
 * Resolve a user-supplied path string.  Absolute paths and `~/...`
 * paths are returned unchanged; bare and `./...` relative paths are
 * joined to the first workspace folder if one is open.
 */
function resolveUserPath(p: string): string | undefined {
    if (path.isAbsolute(p)) { return p; }

    if (p.startsWith('~/')) {
        const home = process.env.HOME || process.env.USERPROFILE;
        if (home) { return path.join(home, p.slice(2)); }
    }

    const folders = vscode.workspace.workspaceFolders;
    if (folders && folders.length > 0) {
        return path.resolve(folders[0].uri.fsPath, p);
    }

    return p;                              // fall through to existsSync
}
