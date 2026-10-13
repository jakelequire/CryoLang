"""One-off codemod: in tools/CryoLSP/src, rewrite an explicit destructor
call on a local, `x.drop()`, as `mem::drop(x)`, and import std::core::mem
where a file gains its first `mem::drop`.

The population is the W0015 sites the LSP build reports whose receiver is
a bare name declared `const`/`mut` in the same function, with an owned
(non-reference) type.  Sites on fields, parameters and match bindings are
not in it.  Each entry is (file under src, line, name); every one must be
rewritten exactly once or nothing is written.

usage: python scripts/lsp-local-drop-codemod.py
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "CryoLSP", "src")

SITES = [
    ("handlers/code_action.cryo", 202, "uri_str"),
    ("handlers/code_action.cryo", 208, "uri_str"),
    ("handlers/code_action.cryo", 208, "new_text"),
    ("handlers/code_action.cryo", 224, "uri_str"),
    ("handlers/code_lens.cryo", 176, "fs_path_owned"),
    ("handlers/code_lens.cryo", 182, "path_buf"),
    ("handlers/code_lens.cryo", 82, "id"),
    ("handlers/code_lens.cryo", 84, "fs_path_owned"),
    ("handlers/completion.cryo", 211, "patched"),
    ("handlers/completion.cryo", 212, "ident_buf"),
    ("handlers/completion.cryo", 216, "patched"),
    ("handlers/completion.cryo", 220, "ident_buf"),
    ("handlers/completion.cryo", 230, "fs_path_owned"),
    ("handlers/completion.cryo", 236, "path_buf"),
    ("handlers/completion.cryo", 238, "ident_buf"),
    ("handlers/completion.cryo", 243, "ident_buf"),
    ("handlers/completion.cryo", 289, "ident_buf"),
    ("handlers/completion.cryo", 292, "items"),
    ("handlers/definition.cryo", 102, "fs_path_owned"),
    ("handlers/definition.cryo", 108, "path_buf"),
    ("handlers/definition.cryo", 622, "l"),
    ("handlers/definition.cryo", 670, "l"),
    ("handlers/diagnostics.cryo", 74, "body"),
    ("handlers/hover.cryo", 1104, "h"),
    ("handlers/hover.cryo", 141, "fs_path_owned"),
    ("handlers/hover.cryo", 147, "path_buf"),
    ("handlers/hover.cryo", 990, "ident"),
    ("handlers/inactive_regions.cryo", 117, "body"),
    ("handlers/inactive_regions.cryo", 75, "fs_path_owned"),
    ("handlers/keyword_docs.cryo", 113, "h"),
    ("handlers/keyword_docs.cryo", 78, "word_buf"),
    ("handlers/keyword_docs.cryo", 88, "word_buf"),
    ("handlers/keyword_docs.cryo", 93, "word_buf"),
    ("handlers/semantic_tokens.cryo", 1669, "fs_path_owned"),
    ("handlers/semantic_tokens.cryo", 1675, "path_buf"),
    ("handlers/semantic_tokens.cryo", 1678, "path_buf"),
    ("handlers/semantic_tokens.cryo", 1707, "path_buf"),
    ("handlers/text_sync.cryo", 145, "uris"),
    ("handlers/text_sync.cryo", 162, "u"),
    ("handlers/text_sync.cryo", 265, "body"),
    ("main.cryo", 62, "server"),
    ("protocol/conv.cryo", 117, "owned"),
    ("protocol/conv.cryo", 241, "fs_path_owned"),
    ("protocol/conv.cryo", 474, "id"),
    ("protocol/framing.cryo", 111, "line_buf"),
    ("protocol/framing.cryo", 130, "line_buf"),
    ("protocol/framing.cryo", 140, "line_buf"),
    ("protocol/framing.cryo", 145, "line_buf"),
    ("protocol/framing.cryo", 296, "header"),
    ("protocol/jsonrpc.cryo", 161, "err"),
    ("protocol/jsonrpc.cryo", 329, "owned_wire"),
    ("protocol/jsonrpc.cryo", 342, "owned"),
    ("protocol/jsonrpc.cryo", 360, "owned"),
    ("server/diag_render_cache.cryo", 122, "owned"),
    ("server/diag_render_cache.cryo", 78, "ids"),
    ("server/diag_render_cache.cryo", 91, "k"),
    ("server/docs.cryo", 77, "owned"),
    ("server/docs.cryo", 89, "d"),
    ("server/docs.cryo", 91, "k"),
    ("server/docs.cryo", 96, "k"),
    ("server/server.cryo", 105, "owned"),
    ("server/server.cryo", 314, "owned"),
    ("server/server.cryo", 325, "owned"),
    ("server/server.cryo", 337, "body"),
    ("server/session.cryo", 171, "fs_path_owned"),
    ("server/session.cryo", 296, "evicted"),
    ("server/session.cryo", 310, "gone"),
    ("server/session.cryo", 392, "owned"),
    ("server/session.cryo", 463, "s"),
    ("server/session.cryo", 465, "k"),
    ("server/session.cryo", 470, "k"),
    ("server/session.cryo", 504, "dir"),
    ("server/session.cryo", 522, "probe"),
    ("server/session.cryo", 523, "dir"),
    ("server/session.cryo", 526, "probe"),
    ("server/session.cryo", 529, "dir"),
    ("server/session.cryo", 548, "dir"),
    ("server/session.cryo", 560, "dir"),
    ("server/session.cryo", 563, "candidate"),
    ("server/session.cryo", 568, "dir"),
]

IMPORT = b"import std::core::mem;"


def main():
    by_file = {}
    for f, line, name in SITES:
        by_file.setdefault(f, []).append((line, name))
    out = {}
    for f, sites in sorted(by_file.items()):
        path = os.path.join(ROOT, f)
        with open(path, "rb") as h:
            lines = h.read().splitlines(keepends=True)
        for line, name in sites:
            pat = re.compile(rb"(?<![A-Za-z0-9_.])" + re.escape(name.encode()) + rb"\.drop\(\)")
            new, n = pat.subn(b"mem::drop(" + name.encode() + b")", lines[line - 1], count=1)
            if n != 1:
                sys.exit("%s:%d: no `%s.drop()` to rewrite" % (f, line, name))
            lines[line - 1] = new
        if not any(l.strip() == IMPORT for l in lines):
            core = [i for i, l in enumerate(lines) if l.startswith(b"import std::core::")]
            std = [i for i, l in enumerate(lines) if l.startswith(b"import std::")]
            if not std:
                sys.exit("%s: no std import to place `%s` beside" % (f, IMPORT.decode()))
            at = core[-1] + 1 if core else std[0]
            eol = b"\r\n" if lines[at - 1 if at > 0 else 0].endswith(b"\r\n") else b"\n"
            lines.insert(at, IMPORT + eol)
        out[path] = b"".join(lines)
    for path, data in out.items():
        with open(path, "wb") as h:
            h.write(data)
    print("lsp-local-drop-codemod: %d site(s) in %d file(s)" % (len(SITES), len(out)))


if __name__ == "__main__":
    main()
