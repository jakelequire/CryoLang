#!/usr/bin/env python3
"""End-to-end smoke test for cryolsp over stdio JSON-RPC.

CryoLSP is built by neither `make test` nor `make selfhost-check`, and
`make lsp` builds it with the PINNED compiler - which may predate the change
under test. A checker or AST change can therefore leave the server broken
with every gate green; that has happened more than once. This drives the real
binary the way an editor does and asserts it answers.

Scope is deliberately shallow: start, initialize, open a document, ask a few
questions, shut down. It is a smoke test, not a conformance suite - the point
is to catch "does not build / does not start / does not answer / crashes on
this document", which is the failure class that actually escapes.

Usage:
    python tools/CryoLSP/tests/smoke.py [path/to/cryolsp[.exe]]

Exit code is 0 when every check passes, 1 otherwise.
"""

import json
import os
import subprocess
import sys
import threading


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))


def default_server_path():
    exe = "cryolsp.exe" if os.name == "nt" else "cryolsp"
    return os.path.join(REPO_ROOT, "tools", "CryoLSP", "build", exe)


class Server:
    """A running cryolsp with Content-Length framing on stdin/stdout."""

    def __init__(self, path, root_uri):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=REPO_ROOT,
        )
        self.root_uri = root_uri
        self._next_id = 0
        # Drain stderr on a thread: the server logs there, and a full pipe
        # buffer would deadlock it mid-response.
        self._stderr = []
        t = threading.Thread(target=self._drain_stderr, daemon=True)
        t.start()

    def _drain_stderr(self):
        for line in self.proc.stderr:
            self._stderr.append(line.decode("utf-8", "replace").rstrip())

    def _send(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body))
        self.proc.stdin.write(body)
        self.proc.stdin.flush()

    def _read_message(self):
        """Read one framed message, or None at EOF."""
        length = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return None
            line = line.strip()
            if not line:
                break                       # blank line ends the header block
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1])
        if length is None:
            return None
        buf = b""
        while len(buf) < length:            # a pipe read may come up short
            chunk = self.proc.stdout.read(length - len(buf))
            if not chunk:
                return None
            buf += chunk
        return json.loads(buf.decode("utf-8"))

    def request(self, method, params, timeout=30.0):
        """Send a request and return the message carrying its id.

        Notifications the server volunteers in the meantime (diagnostics,
        log messages) are skipped rather than mistaken for the response.
        """
        self._next_id += 1
        rid = self._next_id
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        result = {}

        def pump():
            while True:
                msg = self._read_message()
                if msg is None:
                    return
                if msg.get("id") == rid:
                    result["msg"] = msg
                    return

        t = threading.Thread(target=pump, daemon=True)
        t.start()
        t.join(timeout)
        if "msg" not in result:
            raise TimeoutError("no response to %s within %.0fs" % (method, timeout))
        return result["msg"]

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def stderr_text(self):
        return "\n".join(self._stderr)


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


def main():
    server_path = sys.argv[1] if len(sys.argv) > 1 else default_server_path()
    if not os.path.exists(server_path):
        print("cryolsp not found at %s" % server_path)
        print("build it with:  cd tools/CryoLSP && cryo build")
        return 1

    print("cryolsp smoke test")
    print("  server: %s" % server_path)

    c = Checks()
    root = os.path.join(REPO_ROOT, "tools", "CryoLSP")
    srv = Server(server_path, uri_for(root))

    # --- initialize -------------------------------------------------------
    # Offer BOTH position encodings so the server picks utf-8, and separately
    # exercise the utf-16 path below.
    init = srv.request("initialize", {
        "processId": os.getpid(),
        "rootUri": uri_for(root),
        "capabilities": {"general": {"positionEncodings": ["utf-8", "utf-16"]}},
    })
    caps = init.get("result", {}).get("capabilities", {})
    c.ok("result" in init, "initialize returns a result")
    c.ok(caps.get("positionEncoding") == "utf-8",
         "negotiates utf-8 when the client offers it",
         "got %r" % caps.get("positionEncoding"))
    info = init.get("result", {}).get("serverInfo", {})
    c.ok(bool(info.get("version")), "reports serverInfo.version",
         "got %r" % info)
    for cap in ("hoverProvider", "completionProvider", "definitionProvider",
                "semanticTokensProvider"):
        c.ok(cap in caps, "advertises %s" % cap)

    srv.notify("initialized", {})

    # --- didOpen ----------------------------------------------------------
    doc_path = os.path.join(root, "smoke_doc.cryo")
    doc_uri = uri_for(doc_path)
    text = (
        "namespace Smoke::Doc;\n"
        "\n"
        "type struct Point {\n"
        "    x: i32;\n"
        "    y: i32;\n"
        "\n"
        "    sum(&this) -> i32 { return this.x + this.y; }\n"
        "}\n"
        "\n"
        "function main() -> i32 {\n"
        "    const p: Point = Point { x: 1, y: 2 };\n"
        "    return p.sum();\n"
        "}\n"
    )
    srv.notify("textDocument/didOpen", {
        "textDocument": {"uri": doc_uri, "languageId": "cryo",
                         "version": 1, "text": text},
    })

    # --- hover ------------------------------------------------------------
    # Line 10 (0-based) is `    const p: Point = ...`; character 13 sits on
    # `Point`.
    hov = srv.request("textDocument/hover", {
        "textDocument": {"uri": doc_uri},
        "position": {"line": 10, "character": 13},
    })
    c.ok("error" not in hov, "hover does not error",
         str(hov.get("error")))
    c.ok("result" in hov, "hover returns a result")

    # --- completion -------------------------------------------------------
    comp = srv.request("textDocument/completion", {
        "textDocument": {"uri": doc_uri},
        "position": {"line": 11, "character": 13},
        "context": {"triggerKind": 1},
    })
    c.ok("error" not in comp, "completion does not error",
         str(comp.get("error")))

    # --- semantic tokens --------------------------------------------------
    # Against a REAL file that the project's module graph contains, not the
    # synthetic buffer above. Tokens are produced by walking the compiled AST,
    # which is reached via `find_module_by_path`; a document that is not on
    # disk (or is on disk but unreachable from the entry point) has no module
    # to walk and correctly yields an empty list. Asserting tokens on the
    # synthetic doc tested an impossible case and failed permanently.
    # A small example project, not CryoLSP's own source: opening the latter
    # compiles the whole compiler library and takes far longer than a smoke
    # test should wait.
    real_path = os.path.join(REPO_ROOT, "examples", "03-fibonacci", "src", "main.cryo")
    real_uri = uri_for(real_path)
    with open(real_path, "r", encoding="utf-8") as fh:
        real_text = fh.read()
    srv.notify("textDocument/didOpen", {
        "textDocument": {"uri": real_uri, "languageId": "cryo",
                         "version": 1, "text": real_text},
    })
    st = srv.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": real_uri},
    }, timeout=120)
    c.ok("error" not in st, "semanticTokens/full does not error",
         str(st.get("error")))
    data = (st.get("result") or {}).get("data")
    c.ok(isinstance(data, list) and len(data) > 0,
         "semanticTokens/full returns tokens",
         "got %r len %s" % (type(data).__name__, len(data) if isinstance(data, list) else "n/a"))
    c.ok(isinstance(data, list) and len(data) % 5 == 0,
         "semantic token data is a multiple of 5")

    # A document outside the module graph must answer cleanly rather than
    # erroring or crashing.
    ghost_uri = uri_for(os.path.join(REPO_ROOT, "examples", "03-fibonacci",
                                     "src", "does_not_exist_on_disk.cryo"))
    srv.notify("textDocument/didOpen", {
        "textDocument": {"uri": ghost_uri, "languageId": "cryo", "version": 1,
                         "text": "namespace Ghost::Doc;\n"},
    })
    gst = srv.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": ghost_uri},
    })
    c.ok("error" not in gst and isinstance((gst.get("result") or {}).get("data"), list),
         "semanticTokens on an unknown document answers cleanly")

    # --- closing a document must not take the server down -----------------
    # `did_close` releases the session's arena. That arena is also the one
    # published to `GlobalArena`, whose `holds()` is dereferenced on every
    # deallocation - so failing to retract it first turned the very next
    # `free` into a wild read and killed the server mid-notification.
    srv.notify("textDocument/didClose", {"textDocument": {"uri": ghost_uri}})
    srv.notify("textDocument/didClose", {"textDocument": {"uri": real_uri}})
    # Closing the example-project document sends the next request back through
    # a full recompile of THIS project, so allow for a cold compile. A dead
    # server never answers at all, so report that as a failed check rather than
    # letting the harness die with a traceback.
    try:
        after_close = srv.request("textDocument/hover", {
            "textDocument": {"uri": doc_uri},
            "position": {"line": 10, "character": 13},
        }, timeout=120)
    except TimeoutError:
        after_close = None
    c.ok(after_close is not None
         and ("result" in after_close or "error" in after_close),
         "server survives didClose and still answers",
         "no response - server exit code %r" % (srv.proc.poll(),))
    if srv.proc.poll() is not None:
        # Everything after this point would just raise on a closed pipe.
        print("\n  server is gone (exit %r); skipping remaining checks"
              % srv.proc.poll())
        print("\n  passed %d, failed %d" % (c.passed, len(c.failed)))
        return 1

    # --- a document the server must not choke on --------------------------
    # An incomplete buffer mid-edit, which is the normal state during typing.
    srv.notify("textDocument/didChange", {
        "textDocument": {"uri": doc_uri, "version": 2},
        "contentChanges": [{"text": text.replace("return p.sum();", "return p.")}],
    })
    hov2 = srv.request("textDocument/hover", {
        "textDocument": {"uri": doc_uri},
        "position": {"line": 11, "character": 14},
    })
    c.ok("result" in hov2 or "error" in hov2,
         "survives a mid-edit incomplete buffer")

    # --- shutdown ---------------------------------------------------------
    sd = srv.request("shutdown", {})
    c.ok("result" in sd, "shutdown returns a result")
    srv.notify("exit", {})
    try:
        rc = srv.proc.wait(timeout=10)
        c.ok(rc == 0, "exits cleanly after exit notification", "rc=%r" % rc)
    except subprocess.TimeoutExpired:
        srv.proc.kill()
        c.ok(False, "exits cleanly after exit notification", "timed out")

    print("")
    print("  passed %d, failed %d" % (c.passed, len(c.failed)))
    if c.failed:
        err = srv.stderr_text()
        if err:
            print("\n  --- server stderr (last 40 lines) ---")
            for line in err.split("\n")[-40:]:
                print("  " + line)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
