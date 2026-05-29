// Markdown *preview* highlighting for Cryo.
//
// VS Code has two independent syntax-highlighting engines: the editor and the
// rendered Markdown preview. To make the preview match the editor exactly we
// highlight it with Shiki, which consumes the *same* TextMate grammar the
// editor uses (../syntaxes/cryo.tmGrammar.json) via an oniguruma engine — so
// the token decisions are identical. Colors follow the default VS Code themes
// (Dark+/Light+), switched to track the preview's light/dark mode.
//
// Wired into the preview's markdown-it instance via the
// `markdown.markdownItPlugins` contribution in package.json.

import * as vscode from 'vscode';
import cryoGrammar from '../syntaxes/cryo.tmGrammar.json';

// Shiki is ESM-only; this extension compiles to CommonJS, so Shiki is pulled in
// via dynamic import() (see initHighlighter). We avoid static type imports from
// it (which Node16 rejects from a CJS module) and describe the slice we use.
interface CryoHighlighter {
    codeToHtml(code: string, options: { lang: string; theme: string }): string;
}

const DARK_THEME = 'dark-plus';
const LIGHT_THEME = 'light-plus';

let highlighter: CryoHighlighter | undefined;

/**
 * Build the Shiki highlighter once, loading ONLY what we need — the Cryo
 * TextMate grammar plus the two default VS Code themes — to keep the bundle
 * lean. Called from activate() so the instance is ready before the preview
 * renders. Failures are swallowed: the markdown-it hook then falls back to
 * VS Code's default highlighting.
 */
export async function initHighlighter(): Promise<void> {
    if (highlighter) {
        return;
    }
    const [{ createHighlighterCore }, { createOnigurumaEngine }, darkPlus, lightPlus] =
        await Promise.all([
            import('shiki/core'),
            import('shiki/engine/oniguruma'),
            import('shiki/themes/dark-plus.mjs'),
            import('shiki/themes/light-plus.mjs'),
        ]);
    // The grammar's display name is "Cryo"; register it under the `cryo` id so
    // ```cryo fenced blocks resolve. The JSON grammar is cast to Shiki's
    // LanguageRegistration at this third-party (JSON -> typed grammar) boundary.
    const cryoLang = { ...cryoGrammar, name: 'cryo' };
    highlighter = await createHighlighterCore({
        themes: [darkPlus.default, lightPlus.default],
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        langs: [cryoLang as any],
        engine: createOnigurumaEngine(import('shiki/wasm')),
    });
}

// Pick the theme that matches the editor's current light/dark mode. The
// markdown-it plugin runs in the extension host, so the vscode API is live here.
function activeTheme(): string {
    switch (vscode.window.activeColorTheme.kind) {
        case vscode.ColorThemeKind.Light:
        case vscode.ColorThemeKind.HighContrastLight:
            return LIGHT_THEME;
        default:
            return DARK_THEME;
    }
}

/**
 * Returned from the extension's `activate()` so VS Code can extend the Markdown
 * preview's markdown-it instance. We wrap the preview's existing `highlight`
 * hook, render Cryo blocks with Shiki, and delegate everything else back to
 * VS Code's default implementation.
 */
export function extendMarkdownIt(md: {
    options: { highlight?: (code: string, lang: string) => string };
    set: (opts: { highlight: (code: string, lang: string) => string }) => unknown;
}) {
    const previous = md.options.highlight;
    md.set({
        highlight: (code: string, lang: string): string => {
            if (highlighter && lang && lang.toLowerCase() === 'cryo') {
                return highlighter.codeToHtml(code, { lang: 'cryo', theme: activeTheme() });
            }
            return previous ? previous(code, lang) : '';
        },
    });
    return md;
}
