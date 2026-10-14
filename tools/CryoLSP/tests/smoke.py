#!/usr/bin/env python3
"""Behavioural tests for cryolsp over stdio JSON-RPC.

`make lsp-check` builds the server with the compiler under test and then runs
this against the binary it linked (`scripts/lsp-gate.py`).  The build alone
proves only that the server compiles; this proves it answers, and answers
right.

Two kinds of check:

  * Protocol checks, written in code below: the lifecycle, error replies, and
    a server that must survive what an editor sends it.
  * Fixture checks, one directory per fixture under `fixtures/`.  A fixture is
    a real project that compiles, plus `checks.json`.  Each fixture gets its
    own server process, so one crash cannot hide the rest.

Positions are named by markers, never by line and column.  A marker is a block
comment `/*@name*/` written immediately before the token it names; the
position is the first character after the comment.  `/*^name*/` names the
position where the comment starts instead, which is where a cursor sits for
completion (`wb./*^after_dot*/get()`).  Either way a check refers to it as
`file@name`.  Editing a fixture therefore cannot silently retarget a check: a
missing marker fails the run.

checks.json:

    {
      "open":   ["src/main.cryo", ...],       documents opened, in order
      "checks": [
        {"name": "...", "method": "definition", "at": "src/main.cryo@marker",
         "expect": "src/b.cryo@marker" | null},
        {"name": "...", "method": "hover" | "expandHover", "at": "...",
         "contains": ["..."], "excludes": ["..."]}   or "expect": null
        {"name": "...", "method": "completion", "at": "...",
         "includes": ["..."], "excludes": ["..."]},
        {"name": "...", "method": "diagnostics", "file": "src/main.cryo",
         "expect": [{"code": "W0001", "at": "src/main.cryo@marker"}]},
        {"name": "...", "method": "references" | "documentHighlight",
         "at": "...", "declaration": true, "expect": ["src/b.cryo@marker", ...]},
        {"name": "...", "method": "documentSymbol", "file": "src/b.cryo",
         "includes": ["Item", "Item/get"], "excludes": ["..."]},
        {"name": "...", "method": "workspaceSymbol", "query": "...",
         "includes": ["..."], "excludes": ["..."]},
        {"name": "...", "method": "semanticTokens", "file": "src/main.cryo",
         "expect": {"src/main.cryo@marker": "variable.readonly", "...": null}},
        {"name": "...", "method": "prepareRename", "at": "...",
         "expect": "src/main.cryo@marker", "placeholder": "name"},
        {"name": "...", "method": "rename", "at": "...", "new_name": "...",
         "old": "name", "expect": ["src/main.cryo@marker", ...]},
        {"name": "...", "method": "rename" | "prepareRename", "at": "...",
         "new_name": "...", "refused": "part of the reason"}
      ]
    }

A rename check compares the set of edits' starts with the markers', and
every edit must replace exactly `old`'s width with `new_name`.  A refusal
check passes only on an error reply whose message holds `refused`.

A semantic-tokens check names, per position, the token that must start
there: its type, then any modifiers it must carry (it may carry more), or
null for no token.  A marker reference may add `+N` columns
(`src/main.cryo@fstr+5`), for a position inside a string, where no comment
can sit.

A references or highlight check compares the set of locations' starts with
the markers'; `declaration` asks for the declaration too.  A symbol check
names a nested symbol by its path from the top, `Parent/child`.

Any check may carry `"edit": {"file": ..., "from": ..., "to": ...}`: the
document is changed by replacing the first `from` with `to`, the check runs
against the changed text and its markers, and the document is changed back.
That is how a check asks about a buffer mid-typing.  With `"save": true` the
edit, and the change back, are each followed by a save.

A position check may carry `"quiet_stderr": true`: it then also fails when the
server writes to stderr while answering, which an analysis of a buffer that
does not parse always does.  `expandHover` asks `cryo/expandHover`, the
editor's longer hover.

Usage:
    python tools/CryoLSP/tests/smoke.py [path/to/cryolsp[.exe]] [--fixture NAME]

The last line is `lsp-smoke: N passed, M failed (K fixtures)`; the exit code
is 0 only when M is 0 and something ran.
"""

import json
import os
import re
import subprocess
import sys
import threading
import time


HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FIXTURES = os.path.join(HERE, "fixtures")

# A cold compile of a fixture is the project plus the standard library.
COLD_TIMEOUT = 180.0
MARKER = re.compile(r"/\*([@^])([A-Za-z0-9_]+)\*/")


def default_server_path():
    exe = "cryolsp.exe" if os.name == "nt" else "cryolsp"
    return os.path.join(REPO_ROOT, "tools", "CryoLSP", "build", exe)


def server_env():
    """The server links LLVM-C; on Windows its DLL lives in the toolchain."""
    env = dict(os.environ)
    env["CRYO_STDLIB"] = os.path.join(REPO_ROOT, "stdlib")
    extra = [os.path.join(REPO_ROOT, ".toolchains", "llvm-win", "bin"),
             os.path.join(REPO_ROOT, "bin")]
    extra = [p for p in extra if os.path.isdir(p)]
    env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


class Server:
    """A running cryolsp with Content-Length framing on stdin/stdout.

    One reader thread owns stdout.  Responses are filed by id; notifications
    are kept in order so a check can wait for one.
    """

    def __init__(self, path, cwd):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=cwd, env=server_env(),
        )
        self._next_id = 0
        self._lock = threading.Condition()
        self._responses = {}
        self._arrived = []
        self._notifications = []
        self._eof = False
        self._stderr = []
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        threading.Thread(target=self._read_loop, daemon=True).start()

    def _drain_stderr(self):
        for line in self.proc.stderr:
            self._stderr.append(line.decode("utf-8", "replace").rstrip())

    def _read_message(self):
        length = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return None
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1])
        if length is None:
            return None
        buf = b""
        while len(buf) < length:
            chunk = self.proc.stdout.read(length - len(buf))
            if not chunk:
                return None
            buf += chunk
        return json.loads(buf.decode("utf-8"))

    def _read_loop(self):
        while True:
            msg = self._read_message()
            with self._lock:
                if msg is None:
                    self._eof = True
                    self._lock.notify_all()
                    return
                if "id" in msg and "method" not in msg:
                    self._responses[msg["id"]] = msg
                    self._arrived.append(msg["id"])
                elif "method" in msg:
                    self._notifications.append(msg)
                self._lock.notify_all()

    def alive(self):
        return self.proc.poll() is None and not self._eof

    def send_raw(self, body):
        try:
            self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body))
            self.proc.stdin.write(body)
            self.proc.stdin.flush()
            return True
        except OSError:
            return False

    def send(self, obj):
        return self.send_raw(json.dumps(obj).encode("utf-8"))

    def request(self, method, params, timeout=COLD_TIMEOUT):
        """Send a request; return its response, or None if none arrives."""
        rid = self.send_request(method, params)
        if rid is None:
            return None
        return self.wait_response(rid, timeout)

    def send_request(self, method, params):
        """Send a request without waiting; its id, or None if it could not
        be sent."""
        self._next_id += 1
        rid = self._next_id
        if not self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params}):
            return None
        return rid

    def order(self):
        """The ids of the responses received so far, in arrival order."""
        with self._lock:
            return list(self._arrived)

    def wait_response(self, rid, timeout):
        deadline = time.time() + timeout
        with self._lock:
            while rid not in self._responses:
                left = deadline - time.time()
                if left <= 0 or self._eof:
                    return None
                self._lock.wait(left)
            return self._responses.pop(rid)

    def notify(self, method, params):
        return self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def wait_notification(self, method, pred, timeout=COLD_TIMEOUT):
        """The newest notification `method` satisfying `pred`, waiting for one."""
        deadline = time.time() + timeout
        with self._lock:
            while True:
                for msg in reversed(self._notifications):
                    if msg.get("method") == method and pred(msg.get("params") or {}):
                        return msg.get("params")
                left = deadline - time.time()
                if left <= 0 or self._eof:
                    return None
                self._lock.wait(left)

    def stderr_text(self):
        return "\n".join(self._stderr)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        try:
            self.proc.stdin.close()          # a dead server's pipe cannot flush
        except OSError:
            pass


class Checks:
    def __init__(self):
        self.failed = []
        self.passed = 0

    def ok(self, cond, label, detail=""):
        if cond:
            self.passed += 1
            print("  ok   %s" % label)
        else:
            self.failed.append(label)
            print("  FAIL %s%s" % (label, ("  -- " + detail) if detail else ""))


def uri_for(path):
    p = os.path.abspath(path).replace("\\", "/")
    if not p.startswith("/"):
        p = "/" + p                          # windows drive paths
    return "file://" + p


def same_uri(a, b):
    return a.replace("%3A", ":").replace("%3a", ":").lower() == b.lower()


def position_key(uri, pos):
    """A location's file and start, comparable whatever the URI's escaping."""
    file = uri.replace("%3A", ":").replace("%3a", ":").lower().rsplit("/", 1)[-1]
    return (file, pos["line"], pos["character"])


def symbol_names(symbols, prefix=""):
    """Every symbol's name, a nested one as `Parent/child`."""
    out = []
    for s in symbols:
        full = prefix + s.get("name", "")
        out.append(full)
        out.extend(symbol_names(s.get("children", []), full + "/"))
    return out


# ---- fixtures --------------------------------------------------------------

class Fixture:
    """A fixture's files with their markers located."""

    def __init__(self, root):
        self.root = root
        self.name = os.path.basename(root)
        with open(os.path.join(root, "checks.json"), "r", encoding="utf-8") as fh:
            self.spec = json.load(fh)
        self.text = {}
        self.markers = {}                    # "rel@name" -> (line, character)
        for rel in self.spec.get("open", []):
            self.load(rel)

    def load(self, rel):
        with open(os.path.join(self.root, rel), "r", encoding="utf-8", newline="") as fh:
            self.set_text(rel, fh.read())

    def set_text(self, rel, text):
        """Make `text` the document's content and re-locate its markers."""
        self.text[rel] = text
        prefix = rel + "@"
        for key in [k for k in self.markers if k.startswith(prefix)]:
            del self.markers[key]
        for m in MARKER.finditer(text):
            key = "%s@%s" % (rel, m.group(2))
            if key in self.markers:
                raise ValueError("%s: marker %s appears twice" % (self.name, key))
            offset = m.end() if m.group(1) == "@" else m.start()
            self.markers[key] = position_of(text, offset)

    def uri(self, rel):
        return uri_for(os.path.join(self.root, rel))

    def at(self, ref):
        """`file@marker` -> (rel, position).  A missing marker is an error.
        `file@marker+N` is N columns further along the marker's line, for a
        position no comment can sit in front of (inside a string)."""
        offset = 0
        if "+" in ref:
            ref, extra = ref.rsplit("+", 1)
            offset = int(extra)
        rel = ref.split("@", 1)[0]
        if rel not in self.text:
            self.load(rel)
        if ref not in self.markers:
            raise KeyError("%s: no marker %s" % (self.name, ref))
        line, ch = self.markers[ref]
        return rel, {"line": line, "character": ch + offset}


def position_of(text, offset):
    """LSP position of a character offset: zero-based line, UTF-16 column."""
    line = text.count("\n", 0, offset)
    start = text.rfind("\n", 0, offset) + 1
    col = len(text[start:offset].encode("utf-16-le")) // 2
    return line, col


def describe_location(result):
    if result is None:
        return "null"
    loc = result[0] if isinstance(result, list) and result else result
    if not isinstance(loc, dict) or "uri" not in loc:
        return repr(result)[:200]
    s = loc["range"]["start"]
    return "%s:%d:%d" % (loc["uri"].rsplit("/", 1)[-1], s["line"] + 1, s["character"] + 1)


def location_matches(result, fx, expect):
    loc = result[0] if isinstance(result, list) and len(result) == 1 else result
    if not isinstance(loc, dict) or "uri" not in loc:
        return False
    rel, pos = fx.at(expect)
    s = loc["range"]["start"]
    return (same_uri(loc["uri"], fx.uri(rel))
            and s["line"] == pos["line"] and s["character"] == pos["character"])


def hover_text(result):
    if not result:
        return None
    c = result.get("contents")
    if isinstance(c, dict):
        return c.get("value", "")
    if isinstance(c, list):
        return "\n".join(x.get("value", "") if isinstance(x, dict) else str(x) for x in c)
    return str(c)


def decode_tokens(data, legend):
    """Semantic tokens' wire data -> {(line, start): (type, {modifiers})}."""
    types = legend.get("tokenTypes", [])
    mods = legend.get("tokenModifiers", [])
    out = {}
    line = start = 0
    for k in range(0, len(data) - 4, 5):
        dl, ds, _length, ty, bits = data[k:k + 5]
        line += dl
        start = start + ds if dl == 0 else ds
        names = {mods[b] for b in range(len(mods)) if bits & (1 << b)}
        out[(line, start)] = (types[ty] if ty < len(types) else str(ty), names)
    return out


def completion_labels(result):
    items = result.get("items", []) if isinstance(result, dict) else (result or [])
    return [i.get("label") for i in items]


def run_check(srv, fx, chk, c):
    name = "%s: %s" % (fx.name, chk["name"])
    method = chk["method"]
    if method == "diagnostics":
        uri = fx.uri(chk["file"])
        want = sorted((d["code"],) + tuple(fx.at(d["at"])[1].values()) for d in chk["expect"])

        def got_of(params):
            return sorted((d.get("code"), d["range"]["start"]["line"],
                           d["range"]["start"]["character"]) for d in params.get("diagnostics", []))
        params = srv.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: same_uri(p.get("uri", ""), uri) and got_of(p) == want,
            timeout=COLD_TIMEOUT)
        if params is None:
            last = srv.wait_notification("textDocument/publishDiagnostics",
                                         lambda p: same_uri(p.get("uri", ""), uri), timeout=0)
            c.ok(False, name, "want %r, last published %r" % (want, got_of(last) if last else None))
        else:
            c.ok(True, name)
        return

    if method == "semanticTokens":
        resp = srv.request("textDocument/semanticTokens/full",
                           {"textDocument": {"uri": fx.uri(chk["file"])}})
        if resp is None or "error" in resp or not isinstance(resp.get("result"), dict):
            c.ok(False, name, "no result: %r" % (resp,))
            return
        tokens = decode_tokens(resp["result"].get("data", []), fx.legend)
        bad = []
        for ref, want in chk["expect"].items():
            _, pos = fx.at(ref)
            got = tokens.get((pos["line"], pos["character"]))
            if want is None:
                if got is not None:
                    bad.append("%s: want no token, got %s" % (ref, ".".join([got[0]] + sorted(got[1]))))
                continue
            parts = want.split(".")
            if got is None or got[0] != parts[0] or not set(parts[1:]) <= got[1]:
                shown = None if got is None else ".".join([got[0]] + sorted(got[1]))
                bad.append("%s: want %s, got %s" % (ref, want, shown))
        c.ok(not bad, name, "; ".join(bad))
        return

    if method in ("documentSymbol", "workspaceSymbol"):
        if method == "documentSymbol":
            resp = srv.request("textDocument/documentSymbol",
                               {"textDocument": {"uri": fx.uri(chk["file"])}})
        else:
            resp = srv.request("workspace/symbol", {"query": chk["query"]})
        if resp is None or "error" in resp:
            c.ok(False, name, "no result: %r" % (resp,))
            return
        names = symbol_names(resp.get("result") or [])
        missing = [s for s in chk.get("includes", []) if s not in names]
        present = [s for s in chk.get("excludes", []) if s in names]
        c.ok(not missing and not present, name,
             "symbols %r; missing %r, unwanted %r" % (names[:40], missing, present))
        return

    if method in ("prepareRename", "rename"):
        run_rename_check(srv, fx, chk, c, name)
        return

    rel, pos = fx.at(chk["at"])
    params = {"textDocument": {"uri": fx.uri(rel)}, "position": pos}
    if method == "completion":
        params["context"] = {"triggerKind": 1}
    if method == "references":
        params["context"] = {"includeDeclaration": chk.get("declaration", False)}
    wire = "cryo/expandHover" if method == "expandHover" else "textDocument/" + method
    stderr_before = len(srv._stderr)
    resp = srv.request(wire, params)
    if chk.get("quiet_stderr"):
        # An analysis of a buffer that does not parse reports it on stderr,
        # so a request answered without one leaves stderr as it was.  The
        # pause lets the reader thread catch a line written before the reply.
        time.sleep(0.5)
        if len(srv._stderr) != stderr_before:
            c.ok(False, name, "the server wrote to stderr while answering: %r"
                 % srv._stderr[stderr_before:][:5])
            return
    if resp is None:
        c.ok(False, name, "no response (server %s)" % ("alive" if srv.alive() else "dead"))
        return
    if "error" in resp:
        c.ok(False, name, "error %r" % resp["error"])
        return
    result = resp.get("result")

    if method == "definition":
        if chk["expect"] is None:
            c.ok(result is None or result == [], name, "got %s" % describe_location(result))
        else:
            c.ok(location_matches(result, fx, chk["expect"]), name,
                 "want %s, got %s" % (chk["expect"], describe_location(result)))
    elif method in ("hover", "expandHover"):
        text = hover_text(result)
        if "expect" in chk and chk["expect"] is None:
            c.ok(text is None, name, "got %r" % text)
            return
        missing = [s for s in chk.get("contains", []) if text is None or s not in text]
        present = [s for s in chk.get("excludes", []) if text is not None and s in text]
        c.ok(not missing and not present, name,
             "hover %r; missing %r, unwanted %r" % (text, missing, present))
    elif method == "completion":
        labels = completion_labels(result)
        missing = [s for s in chk.get("includes", []) if s not in labels]
        present = [s for s in chk.get("excludes", []) if s in labels]
        c.ok(not missing and not present, name,
             "items %r; missing %r, unwanted %r" % (labels[:30], missing, present))
    elif method in ("references", "documentHighlight"):
        default_uri = fx.uri(rel)
        got = sorted(position_key(r.get("uri", default_uri), r["range"]["start"])
                     for r in (result or []))
        want = []
        for marker in chk["expect"]:
            mrel, mpos = fx.at(marker)
            want.append(position_key(fx.uri(mrel), mpos))
        c.ok(got == sorted(want), name, "want %r, got %r" % (sorted(want), got))
    else:
        raise ValueError("%s: unknown check method %r" % (fx.name, method))


def run_rename_check(srv, fx, chk, c, name):
    method = chk["method"]
    rel, pos = fx.at(chk["at"])
    params = {"textDocument": {"uri": fx.uri(rel)}, "position": pos}
    if method == "rename":
        params["newName"] = chk["new_name"]
    resp = srv.request("textDocument/" + method, params)
    if resp is None:
        c.ok(False, name, "no response (server %s)" % ("alive" if srv.alive() else "dead"))
        return
    if "refused" in chk:
        message = (resp.get("error") or {}).get("message", "")
        c.ok("error" in resp and chk["refused"] in message, name,
             "want a refusal saying %r, got %r" % (chk["refused"], resp))
        return
    if "error" in resp:
        c.ok(False, name, "error %r" % resp["error"])
        return
    result = resp.get("result") or {}
    if method == "prepareRename":
        erel, epos = fx.at(chk["expect"])
        start = (result.get("range") or {}).get("start")
        c.ok(start == epos and result.get("placeholder") == chk["placeholder"], name,
             "want %s %r, got %r" % (chk["expect"], chk["placeholder"], result))
        return
    got = []
    bad = []
    for uri, edits in (result.get("changes") or {}).items():
        for e in edits:
            r = e["range"]
            got.append(position_key(uri, r["start"]))
            width = r["end"]["character"] - r["start"]["character"]
            if (r["end"]["line"] != r["start"]["line"] or width != len(chk["old"])
                    or e["newText"] != chk["new_name"]):
                bad.append(e)
    want = []
    for marker in chk["expect"]:
        mrel, mpos = fx.at(marker)
        want.append(position_key(fx.uri(mrel), mpos))
    c.ok(sorted(got) == sorted(want) and not bad, name,
         "want %r, got %r; edits not replacing %r: %r" % (sorted(want), sorted(got), chk["old"], bad))


def initialize(srv, root, c, label):
    init = srv.request("initialize", {
        "processId": os.getpid(),
        "rootUri": uri_for(root),
        "capabilities": {"general": {"positionEncodings": ["utf-16"]}},
    })
    c.ok(init is not None and "result" in init, "%s: initialize returns a result" % label)
    srv.notify("initialized", {})
    return ((init or {}).get("result") or {}).get("capabilities", {})


def shutdown(srv, c, label):
    sd = srv.request("shutdown", {}, timeout=60)
    c.ok(sd is not None and "result" in sd, "%s: shutdown returns a result" % label)
    srv.notify("exit", {})
    try:
        rc = srv.proc.wait(timeout=20)
        c.ok(rc == 0, "%s: exits 0 after shutdown and exit" % label, "rc=%r" % rc)
    except subprocess.TimeoutExpired:
        srv.stop()
        c.ok(False, "%s: exits 0 after shutdown and exit" % label, "timed out")


def run_fixture(server_path, fx, c):
    print("fixture %s" % fx.name)
    failed_before = len(c.failed)
    srv = Server(server_path, fx.root)
    try:
        caps = initialize(srv, fx.root, c, fx.name)
        fx.legend = (caps.get("semanticTokensProvider") or {}).get("legend", {})
        for rel in fx.spec.get("open", []):
            srv.notify("textDocument/didOpen", {"textDocument": {
                "uri": fx.uri(rel), "languageId": "cryo", "version": 1, "text": fx.text[rel]}})
        version = 1
        for chk in fx.spec.get("checks", []):
            if not srv.alive():
                c.ok(False, "%s: %s" % (fx.name, chk["name"]), "server is gone")
                continue
            edit = chk.get("edit")
            if edit is None:
                run_check(srv, fx, chk, c)
                continue
            # The check asks about a buffer being typed: change the document
            # (unsaved), ask, and put it back.
            rel = edit["file"]
            original = fx.text[rel]
            if edit["from"] not in original:
                raise KeyError("%s: edit text %r not in %s" % (fx.name, edit["from"], rel))
            fx.set_text(rel, original.replace(edit["from"], edit["to"], 1))
            version += 1
            change(srv, fx, rel, version, edit.get("save", False))
            run_check(srv, fx, chk, c)
            fx.set_text(rel, original)
            version += 1
            change(srv, fx, rel, version, edit.get("save", False))
        if srv.alive():
            shutdown(srv, c, fx.name)
    finally:
        srv.stop()
        report_stderr(srv, c, failed_before)


def change(srv, fx, rel, version, save):
    srv.notify("textDocument/didChange", {
        "textDocument": {"uri": fx.uri(rel), "version": version},
        "contentChanges": [{"text": fx.text[rel]}]})
    if save:
        # The file on disk is never written: an open buffer is what every
        # analysis reads, saved or not, so a save only says "analyse now".
        srv.notify("textDocument/didSave", {"textDocument": {"uri": fx.uri(rel)}})


def report_stderr(srv, c, failed_before):
    if len(c.failed) > failed_before:
        print("  server exit code %r" % srv.proc.poll())
        err = srv.stderr_text()
        if err:
            print("  --- server stderr (last 20 lines) ---")
            for line in err.split("\n")[-20:]:
                print("  | " + line)


# ---- protocol checks -------------------------------------------------------

def run_protocol(server_path, c):
    """Lifecycle and robustness, against the first fixture's project."""
    print("protocol")
    failed_before = len(c.failed)
    root = os.path.join(FIXTURES, "two_modules")
    srv = Server(server_path, root)
    try:
        caps = initialize(srv, root, c, "protocol")
        for cap in ("hoverProvider", "completionProvider", "definitionProvider",
                    "semanticTokensProvider"):
            c.ok(cap in caps, "protocol: advertises %s" % cap)

        main_rel = "src/main.cryo"
        with open(os.path.join(root, main_rel), "r", encoding="utf-8", newline="") as fh:
            text = fh.read()
        uri = uri_for(os.path.join(root, main_rel))
        srv.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "cryo", "version": 1, "text": text}})

        st = srv.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        data = ((st or {}).get("result") or {}).get("data")
        c.ok(isinstance(data, list) and len(data) > 0 and len(data) % 5 == 0,
             "protocol: semanticTokens/full returns whole tokens",
             "got %r" % (st,) if not isinstance(data, list) else "len %d" % len(data))

        # A document outside the module graph answers cleanly.
        ghost = uri_for(os.path.join(root, "src", "not_on_disk.cryo"))
        srv.notify("textDocument/didOpen", {"textDocument": {
            "uri": ghost, "languageId": "cryo", "version": 1, "text": "namespace Ghost::Doc;\n"}})
        gst = srv.request("textDocument/semanticTokens/full", {"textDocument": {"uri": ghost}})
        c.ok(gst is not None and "error" not in gst
             and isinstance((gst.get("result") or {}).get("data"), list),
             "protocol: semanticTokens on a document outside the project answers")

        # Closing documents must not take the server down.
        srv.notify("textDocument/didClose", {"textDocument": {"uri": ghost}})
        after = srv.request("textDocument/hover", {
            "textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}})
        c.ok(after is not None, "protocol: answers after a didClose",
             "server %s" % ("alive" if srv.alive() else "dead"))

        # A half-typed line is the normal state of a buffer.
        srv.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [{"text": text.replace("wb./*^after_dot*//*@get_call*/get();", "wb.")}]})
        mid = srv.request("textDocument/hover", {
            "textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}})
        c.ok(mid is not None, "protocol: survives a half-typed buffer")

        if srv.alive():
            shutdown(srv, c, "protocol")
    finally:
        srv.stop()
        report_stderr(srv, c, failed_before)


def run_protocol_errors(server_path, c):
    """Every error reply is answered, and the server lives on after it."""
    print("protocol errors")
    failed_before = len(c.failed)
    root = os.path.join(FIXTURES, "two_modules")
    srv = Server(server_path, root)
    try:
        early = srv.request("textDocument/hover", {
            "textDocument": {"uri": uri_for(os.path.join(root, "src", "main.cryo"))},
            "position": {"line": 0, "character": 0}}, timeout=30)
        c.ok(early is not None and (early.get("error") or {}).get("code") == -32002,
             "protocol: a request before initialize is refused as not initialized",
             "got %r" % (early,))
        initialize(srv, root, c, "protocol errors")

        for i, method in enumerate(("cryo/noSuchMethod", "textDocument/noSuchMethod")):
            resp = srv.request(method, {}, timeout=30)
            c.ok(resp is not None and (resp.get("error") or {}).get("code") == -32601,
                 "protocol: unknown method %d is answered with method-not-found" % (i + 1),
                 "got %r (server %s)" % (resp, "alive" if srv.alive() else "dead"))

        srv.send({"jsonrpc": "2.0", "id": "bad-1", "params": {}})
        bad = srv.wait_response("bad-1", 30)
        c.ok(bad is not None and (bad.get("error") or {}).get("code") == -32600,
             "protocol: a request with no method is answered as invalid, under its id",
             "got %r" % (bad,))

        srv.send_raw(b"{ this is not json")
        parse = srv.wait_response(None, 30)
        c.ok(parse is not None and (parse.get("error") or {}).get("code") == -32700,
             "protocol: a body that is not JSON is answered with a parse error",
             "got %r" % (parse,))

        main_uri = uri_for(os.path.join(root, "src", "main.cryo"))
        with open(os.path.join(root, "src", "main.cryo"), "r", encoding="utf-8", newline="") as fh:
            text = fh.read()
        srv.notify("textDocument/didOpen", {"textDocument": {
            "uri": main_uri, "languageId": "cryo", "version": 1, "text": text}})
        after = srv.request("textDocument/hover", {
            "textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 0}})
        c.ok(after is not None and "error" not in after,
             "protocol: answers a real request after the error replies",
             "got %r (server %s)" % (after, "alive" if srv.alive() else "dead"))

        published = srv.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: same_uri(p.get("uri", ""), main_uri), timeout=COLD_TIMEOUT)
        refresh = srv.wait_notification("workspace/semanticTokens/refresh",
                                         lambda p: True, timeout=0)
        c.ok(published is not None and refresh is None,
             "protocol: no semantic-token refresh is sent to a client that did not declare it",
             "published %r, refresh %r" % (published is not None, refresh is not None))

        sd = srv.request("shutdown", {}, timeout=60)
        c.ok(sd is not None and "result" in sd, "protocol errors: shutdown returns a result")
        late = srv.request("textDocument/hover", {
            "textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 0}}, timeout=30)
        c.ok(late is not None and (late.get("error") or {}).get("code") == -32600,
             "protocol: a request after shutdown is refused as invalid", "got %r" % (late,))
        srv.notify("exit", {})
        try:
            rc = srv.proc.wait(timeout=20)
            c.ok(rc == 0, "protocol errors: exits 0 after shutdown and exit", "rc=%r" % rc)
        except subprocess.TimeoutExpired:
            c.ok(False, "protocol errors: exits 0 after shutdown and exit", "timed out")
    finally:
        srv.stop()
        report_stderr(srv, c, failed_before)


def run_during_analysis(server_path, c):
    """Requests sent behind one that starts an analysis.  The analysis
    stops between modules to read them, so a request behind it is answered
    from the last analysis before the first one's answer, and a cancellation
    of the first one is honoured.  The order is decided by what is already
    in the pipe when the analysis starts, not by time."""
    label = "during analysis"
    print(label)
    fx = Fixture(os.path.join(FIXTURES, "two_modules"))
    srv = Server(server_path, fx.root)
    try:
        initialize(srv, fx.root, c, label)
        for rel in fx.spec.get("open", []):
            srv.notify("textDocument/didOpen", {"textDocument": {
                "uri": fx.uri(rel), "languageId": "cryo", "version": 1, "text": fx.text[rel]}})
        rel, pos = fx.at("src/main.cryo@local_x_use")
        at = {"textDocument": {"uri": fx.uri(rel)}, "position": pos}
        save = {"textDocument": {"uri": fx.uri(rel)}}
        first = srv.request("textDocument/definition", at)
        c.ok(first is not None and location_matches(first.get("result"), fx, "src/main.cryo@local_x"),
             "%s: the first analysis answers" % label)

        srv.notify("textDocument/didSave", save)
        a = srv.send_request("textDocument/definition", at)
        b = srv.send_request("textDocument/hover", at)
        ra = srv.wait_response(a, COLD_TIMEOUT)
        rb = srv.wait_response(b, COLD_TIMEOUT)
        order = srv.order()
        c.ok(ra is not None and rb is not None and order.index(b) < order.index(a),
             "%s: a request behind one that starts an analysis is answered first" % label,
             "order %r, a=%r b=%r" % (order, a, b))
        c.ok(rb is not None and "local_x" in (hover_text(rb.get("result")) or ""),
             "%s: it is answered from the last analysis" % label, "got %r" % (rb,))
        c.ok(ra is not None and location_matches(ra.get("result"), fx, "src/main.cryo@local_x"),
             "%s: the request that started the analysis is answered after it" % label)

        srv.notify("textDocument/didSave", save)
        a = srv.send_request("textDocument/definition", at)
        srv.notify("$/cancelRequest", {"id": a})
        ra = srv.wait_response(a, COLD_TIMEOUT)
        code = ((ra or {}).get("error") or {}).get("code")
        c.ok(code == -32800, "%s: a request cancelled while its analysis runs answers as cancelled" % label,
             "got %r" % (ra,))
        shutdown(srv, c, label)
    finally:
        srv.stop()


def main():
    args = sys.argv[1:]
    only = None
    if "--fixture" in args:
        i = args.index("--fixture")
        only = args[i + 1]
        del args[i:i + 2]
    server_path = os.path.abspath(args[0]) if args else default_server_path()
    if not os.path.exists(server_path):
        print("cryolsp not found at %s" % server_path)
        print("lsp-smoke: 0 passed, 1 failed (0 fixtures)")
        return 1
    print("cryolsp behavioural tests")
    print("  server: %s" % server_path)

    c = Checks()
    fixtures = 0
    if only is None:
        run_protocol(server_path, c)
        run_protocol_errors(server_path, c)
        run_during_analysis(server_path, c)
    for name in sorted(os.listdir(FIXTURES)):
        root = os.path.join(FIXTURES, name)
        if only is not None and name != only:
            continue
        if not os.path.isfile(os.path.join(root, "checks.json")):
            continue
        fixtures += 1
        run_fixture(server_path, Fixture(root), c)

    print("")
    print("lsp-smoke: %d passed, %d failed (%d fixtures)" % (c.passed, len(c.failed), fixtures))
    return 0 if (not c.failed and c.passed > 0 and fixtures > 0) else 1


if __name__ == "__main__":
    sys.exit(main())
