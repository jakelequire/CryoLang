"""Drive cryolsp over stdio against the probe project and print what
go-to-definition and hover answer at chosen positions.

Usage: python probe_lsp.py <server.exe> <project_dir> <file_rel> <queries.json>
queries.json: [{"label":..., "method": "definition"|"hover", "line": L0, "character": C0}]
Positions are 0-based (LSP).
"""
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SMOKE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "tools", "CryoLSP", "tests", "smoke.py")
spec = importlib.util.spec_from_file_location("smoke", SMOKE)
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def main():
    server, proj, rel, qfile = sys.argv[1:5]
    with open(qfile, "r", encoding="utf-8") as fh:
        queries = json.load(fh)
    srv = smoke.Server(server, smoke.uri_for(proj))
    srv.request("initialize", {
        "processId": os.getpid(),
        "rootUri": smoke.uri_for(proj),
        "capabilities": {"general": {"positionEncodings": ["utf-8"]}},
    }, timeout=60)
    srv.notify("initialized", {})
    path = os.path.join(proj, rel)
    uri = smoke.uri_for(path)
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    lines = text.split("\n")
    srv.notify("textDocument/didOpen", {
        "textDocument": {"uri": uri, "languageId": "cryo", "version": 1, "text": text},
    })
    version = 1
    for q in queries:
        if "replace" in q:
            old, new = q["replace"]
            assert old in text, "replace target not in text: %r" % old
            text = text.replace(old, new, 1)
            lines = text.split("\n")
            version += 1
            srv.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": text}],
            })
        m = "textDocument/" + q["method"]
        if q["method"] == "codeLens":
            r = srv.request(m, {"textDocument": {"uri": uri}}, timeout=180)
            for lens in (r.get("result") or []):
                cmd = lens.get("command") or {}
                args = cmd.get("arguments") or []
                locs = args[2] if len(args) > 2 else []
                where = ", ".join("%s:%d" % (l.get("uri", "").rsplit("/", 1)[-1],
                                             l.get("range", {}).get("start", {}).get("line", -1) + 1)
                                  for l in locs)
                print("%-44s | lens at line %d | %r -> [%s]" % (
                    q["label"], lens.get("range", {}).get("start", {}).get("line", -1) + 1,
                    cmd.get("title"), where))
            continue
        r = srv.request(m, {"textDocument": {"uri": uri},
                            "position": {"line": q["line"], "character": q["character"]}},
                        timeout=180)
        src_line = lines[q["line"]] if q["line"] < len(lines) else ""
        word = src_line[q["character"]:q["character"] + 12]
        res = r.get("result")
        if q["method"] == "definition" and isinstance(res, dict):
            u = res.get("uri", "")
            rng = res.get("range", {}).get("start", {})
            short = u.rsplit("/", 1)[-1]
            ans = "%s:%d:%d" % (short, rng.get("line", -1) + 1, rng.get("character", -1) + 1)
        elif q["method"] == "hover" and isinstance(res, dict):
            ans = json.dumps(res.get("contents", {}).get("value", ""))
        elif q["method"] == "completion" and isinstance(res, dict):
            items = res.get("items", [])
            ans = "%d items: %s" % (len(items), ", ".join(
                "%s[%s]" % (i.get("label"), i.get("detail", "")) for i in items[:14]))
        else:
            ans = json.dumps(res) if "error" not in r else "ERROR " + json.dumps(r["error"])
        print("%-44s | at %r | -> %s" % (q["label"], word, ans))
        sys.stdout.flush()
    try:
        srv.request("shutdown", {}, timeout=20)
        srv.notify("exit", {})
        srv.proc.wait(timeout=10)
    except Exception:
        srv.proc.kill()
    print("PROBE_DONE")


if __name__ == "__main__":
    main()
