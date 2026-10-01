# One-shot migration: a destructor written as an inherent `drop` method
# becomes `implement trait Drop for <kind> <Type>`, because only a Drop
# implementation defines a destructor.  The method, with the doc comment
# directly above it, moves out of the type's body (or its inherent
# `implement` block) into a new Drop implementation placed right after that
# block.  An inherent `implement` block left holding only `drop` is turned
# into the Drop implementation in place.  The body is not changed.
#
# Usage: python scripts/ns-migration/8.451/inherent_drop_to_trait.py [ROOT]
# ROOT defaults to the repository this script sits in.  Idempotent: a target
# whose `drop` is already gone is reported and skipped.
import os, re, sys

ROOT = (sys.argv[1] if len(sys.argv) > 1 else
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")).replace("\\", "/").rstrip("/") + "/"

# (file, type) - every type in the tree whose `drop` is inherent and that has
# no Drop implementation, except the run project that checks a method merely
# named `drop` is not a destructor (`drop_in_place_releases_by_destructor`).
TARGETS = [
    ("stdlib/fmt/spec.cryo", "BodyText"),
    ("stdlib/net/dns.cryo", "ResolvedAddrs"),
    ("stdlib/net/https.cryo", "HttpsClient"),
    ("stdlib/net/http2/connection.cryo", "Http2Connection"),
    ("stdlib/net/http2/frame.cryo", "Frame"),
    ("stdlib/net/http2/hpack.cryo", "DynTable"),
    ("stdlib/net/http2/hpack.cryo", "HpackDecoder"),
    ("stdlib/net/http2/hpack.cryo", "HpackEncoder"),
    ("stdlib/net/http2/huffman.cryo", "HuffTree"),
    ("stdlib/net/http2/server.cryo", "Http2Server"),
    ("stdlib/net/socket/tcp.cryo", "TcpStream"),
    ("stdlib/net/socket/tcp.cryo", "TcpListener"),
    ("stdlib/net/socket/udp.cryo", "UdpSocket"),
    ("stdlib/net/tls/context.cryo", "TlsConnector"),
    ("stdlib/net/tls/context.cryo", "TlsAcceptor"),
    ("stdlib/net/tls/stream.cryo", "TlsStream"),
    ("stdlib/net/ws/frame.cryo", "Frame"),
    ("stdlib/process/command.cryo", "ArgvBuf"),
    ("stdlib/process/command.cryo", "WinStdio"),
    ("tests/tests/lang/async_await_shapes.cryo", "AwTag"),
    ("tests/tests/lang/async_carried_local_address_stable.cryo", "Cell"),
    ("tests/tests/lang/async_carry_across_await.cryo", "Tag"),
    ("tests/tests/lang/async_destructure_locals.cryo", "Tag"),
    ("tests/tests/lang/async_future_address_stable.cryo", "Tracked"),
    ("tests/tests/lang/async_generic_owner_byvalue_arg.cryo", "Res"),
    ("tests/tests/lang/async_giveaway_carrier_address_stable.cryo", "Handle"),
    ("tests/tests/lang/async_giveaway_use_after_move_controls.cryo", "Res"),
    ("tests/tests/lang/deref_take_in_unsafe.cryo", "Res"),
    ("tests/tests/lang/drop_flag_follows_every_write.cryo", "Item"),
    ("tests/tests/lang/iter_find_filter_release_once.cryo", "Item"),
    ("tests/tests/lang/match_catchall_binding_release_once.cryo", "Item"),
    ("tests/tests/lang/match_subject_after_payload_read.cryo", "Item"),
    ("tests/tests/lang/match_subject_owned_place.cryo", "Res"),
    ("tests/tests/lang/qualified_module_call_arg_binding.cryo", "Res"),
    ("tests/tests/lang/separate_block_drop.cryo", "Probe"),
    ("tests/tests/negative/E0306_separate_block_drop_not_copy.cryo", "Owner"),
    ("tests/tests/negative/E0452_async_giveaway_then_borrow_same_state.cryo", "Res"),
    ("tests/tests/negative/E0452_async_parameter_used_after_move.cryo", "Res"),
    ("tests/tests/negative/E0452_closure_body_use_after_move.cryo", "Res"),
    ("tests/tests/negative/E0452_closure_capture_then_use.cryo", "Res"),
    ("tests/tests/negative/E0452_destructured_binding_moved_twice.cryo", "Res"),
    ("tests/tests/negative/E0452_destructure_source_used_after.cryo", "Res"),
    ("tests/tests/negative/E0452_double_drop.cryo", "Holder"),
    ("tests/tests/negative/E0452_match_catchall_binding_moved_twice.cryo", "Res"),
    ("tests/tests/negative/E0452_match_payload_moved_twice.cryo", "Res"),
    ("tests/tests/negative/E0452_match_payload_taken_in_loop.cryo", "Res"),
    ("tests/tests/negative/E0452_match_payload_used_after_move.cryo", "Res"),
    ("tests/tests/negative/E0452_match_subject_used_after_payload_taken.cryo", "Res"),
    ("tests/tests/negative/E0452_use_after_move_in_unsafe.cryo", "Res"),
    ("tests/tests/negative/E0453_deref_move_out.cryo", "Res"),
    ("tests/tests/negative/E0453_deref_move_out_qualified_call.cryo", "Res"),
    ("tests/tests/negative/E0453_match_subject_field_move.cryo", "Res"),
    ("tests/tests/negative/E0455_return_match_payload_borrow.cryo", "Inner"),
    ("tests/tests/projects/match_expr_arm_drop/src/main.cryo", "Tracked"),
    ("tests/tests/stdlib/async_stress_shapes.cryo", "Tok"),
    ("tests/tests/stdlib/async_stress_shapes.cryo", "Pair"),
    ("tests/tests/stdlib/async_stress_shapes.cryo", "AssBorrowed"),
    ("tests/tests/stdlib/box.cryo", "InlineDropBox"),
    ("tests/tests/stdlib/io_async_traits.cryo", "MockSource"),
    ("tests/tests/stdlib/io_async_traits.cryo", "MockSink"),
    ("tests/tests/stdlib/rc.cryo", "InlineDropRc"),
    ("tests/tests/stdlib/rc.cryo", "Payload"),
    ("examples/04-calculator/src/main.cryo", "Stack"),
    ("tools/CryoLSP/src/protocol/jsonrpc.cryo", "RequestMessage"),
    ("tools/CryoLSP/src/protocol/jsonrpc.cryo", "NotificationMessage"),
    ("tools/CryoLSP/src/protocol/jsonrpc.cryo", "IncomingMessage"),
    ("tools/CryoLSP/src/protocol/jsonrpc.cryo", "ErrorObject"),
    ("tools/CryoLSP/src/protocol/jsonrpc.cryo", "ResponseMessage"),
    ("tools/CryoLSP/src/protocol/lsp.cryo", "Location"),
    ("tools/CryoLSP/src/protocol/lsp.cryo", "MarkupContent"),
    ("tools/CryoLSP/src/protocol/lsp.cryo", "Hover"),
    ("tools/CryoLSP/src/protocol/lsp.cryo", "CompletionItem"),
    ("tools/CryoLSP/src/server/server.cryo", "Server"),
    ("tools/CryoLSP/src/server/state.cryo", "ServerState"),
]

DROP_RE = re.compile(r'^(\s*)(?:public\s+)?drop\s*\(\s*mut\s*&\s*this\s*\)')

def code(ln):
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', ln.split("//")[0])

def block_end(lines, start):
    """Index of the line whose `}` closes the block opened on or after `start`."""
    depth = 0; opened = False
    for i in range(start, len(lines)):
        s = code(lines[i])
        for ch in s:
            if ch == "{":
                depth += 1; opened = True
            elif ch == "}":
                depth -= 1
                if opened and depth == 0:
                    return i
    raise SystemExit("unbalanced block from line %d" % (start + 1))

def find_owner(lines, ty):
    """(header index, kind, generic params text or '', is_impl) of the block
    holding `ty`'s inherent `drop`: the type declaration, or an inherent
    `implement` block of it."""
    decl = re.compile(r'^\s*(?:public\s+)?type\s+(struct|class|enum|union)\s+' + ty + r'\b(<[^>{]*>)?')
    impl = re.compile(r'^\s*implement(?:<[^>]*>)?\s+(?:(struct|class|enum|union)\s+)?' + ty + r'\b(?:<[^>{]*>)?\s*\{')
    kind = None; params = ""; found = []
    for i, ln in enumerate(lines):
        m = decl.match(ln)
        if m:
            kind = m.group(1); params = m.group(2) or ""
            found.append((i, False))
        m = impl.match(ln)
        if m and " for " not in ln:
            found.append((i, True))
    for (i, is_impl) in found:
        end = block_end(lines, i)
        for j in range(i + 1, end):
            if DROP_RE.match(lines[j]) and code(lines[j]).count("{") >= 0:
                # only a member at the block's own depth
                depth = 0
                for k in range(i, j):
                    s = code(lines[k]); depth += s.count("{") - s.count("}")
                if depth == 1:
                    return i, end, j, kind, params, is_impl
    return None

def param_names(params):
    if not params:
        return ""
    inner = params.strip()[1:-1]
    names = [p.split(":")[0].split("=")[0].strip() for p in inner.split(",")]
    return "<" + ", ".join(names) + ">"

def convert(path, ty):
    p = ROOT + path
    raw = open(p, "rb").read().decode("utf-8")
    nl = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(nl)
    got = find_owner(lines, ty)
    if got is None:
        print("SKIP   %s %s (no inherent drop)" % (path, ty)); return
    hdr, end, d, kind, params, is_impl = got
    if kind is None:
        raise SystemExit("no declaration of %s in %s" % (ty, path))
    names = param_names(params)
    head = "implement%s trait Drop for %s %s%s {" % (names, kind, ty, names)
    # The attributes gating the type's declaration (`![target(windows)]`)
    # gate its Drop implementation too.
    decl_at = next(i for i, ln in enumerate(lines)
                   if re.match(r'^\s*(?:public\s+)?type\s+' + kind + r'\s+' + ty + r'\b', ln))
    attrs = []
    a = decl_at
    while a - 1 >= 0 and lines[a - 1].strip().startswith("!["):
        a -= 1
        attrs.insert(0, lines[a].strip())
    d_end = block_end(lines, d)
    # The doc comment directly above the method moves with it.
    c = d
    while c - 1 > hdr and lines[c - 1].strip().startswith("//"):
        c -= 1
    method = lines[c:d_end + 1]
    body_rest = [l for k, l in enumerate(lines[hdr + 1:end]) if not (c <= hdr + 1 + k <= d_end)]
    if is_impl and all(l.strip() == "" for l in body_rest):
        assert not attrs, "an attribute-gated type converted in place: %s" % ty
        lines[hdr] = head
        print("INPLACE %s %s" % (path, ty))
        open(p, "wb").write(nl.join(lines).encode("utf-8")); return
    ind = re.match(r'^(\s*)', lines[d]).group(1)
    moved = [("    " + l[len(ind):]) if l.startswith(ind) else l.strip() and "    " + l.strip() or l for l in method]
    # Remove the method, and one blank line it leaves doubled.
    del lines[c:d_end + 1]
    if c < len(lines) and c - 1 >= 0 and lines[c - 1].strip() == "" and lines[c].strip() in ("", "}"):
        del lines[c - 1]
    end = block_end(lines, hdr)
    lines[end + 1:end + 1] = [""] + attrs + [head] + moved + ["}"]
    print("MOVED  %s %s" % (path, ty))
    open(p, "wb").write(nl.join(lines).encode("utf-8"))

for path, ty in TARGETS:
    convert(path, ty)
