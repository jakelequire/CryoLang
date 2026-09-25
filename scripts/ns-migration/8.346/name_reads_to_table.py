"""Every `<id>.qualified_name()` in the compiler becomes `<table>.path_of(<id>)`:
a definition's path is a lookup in the compilation's DefTable, not a field
read off the id.  `<table>` is whatever reaches the DefTable where the call
sits.  Run once from the repo root; prints each rewrite and refuses any line
it cannot place.
"""
import re, subprocess, sys

TABLE = {
    "compiler/src/compiler/AST/_module.cryo": "defs",
    "compiler/src/compiler/AST/declaration.cryo": "arena.defs",
    "compiler/src/compiler/codegen/ops/declaration_emitter.cryo": "this.ctx.defs",
    "compiler/src/compiler/codegen/ops/expr_ops.cryo": "c.defs",
    "compiler/src/compiler/codegen/test_main_codegen.cryo": "ctx.defs",
    "compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo": "ctx_ptr.defs",
    "compiler/src/compiler/const_table.cryo": "this.table.defs",
    "compiler/src/compiler/decl_index.cryo": "this.defs",
    "compiler/src/compiler/mono/call_specializer.cryo": "this.decl_index.defs",
    "compiler/src/compiler/passes/specialization.cryo": "ctx.defs",
    "compiler/src/compiler/sema/async_lower.cryo": "this.ctx.defs",
    "compiler/src/compiler/sema/call_resolver.cryo": "this.ctx.defs",
    "compiler/src/compiler/sema/lambda_synth.cryo": "this.ctx.defs",
    "compiler/src/compiler/sema/sema.cryo": "this.ctx.defs",
    "compiler/src/compiler/types/arena.cryo": "this.defs",
    "compiler/src/compiler/types/generic_registry.cryo": "this.arena.defs",
    "compiler/src/compiler/types/ownership.cryo": "arena.defs",
    "compiler/src/compiler/types/resolver.cryo": "this.arena.defs",
    "compiler/src/compiler/types/trait_checker.cryo": "this.arena.defs",
}
# Static functions in a file whose instance methods reach the table otherwise.
STATIC_TABLE = {
    ("compiler/src/compiler/types/generic_registry.cryo", "annotation_unifies"): "arena.defs",
    ("compiler/src/compiler/types/generic_registry.cryo", "trait_impl_key"): "defs",
}

CALL = ".qualified_name()"
IDENT = re.compile(r"[A-Za-z0-9_:\.]")


def receiver_start(line, end):
    """Index where the receiver expression ending at `end` begins."""
    i = end
    while i > 0:
        c = line[i - 1]
        if c in ")]":
            close, open_ = c, "(" if c == ")" else "["
            depth = 0
            while i > 0:
                i -= 1
                if line[i] == close:
                    depth += 1
                elif line[i] == open_:
                    depth -= 1
                    if depth == 0:
                        break
            continue
        if IDENT.match(c):
            i -= 1
            continue
        break
    return i


files = subprocess.run(["git", "grep", "-l", "-F", CALL, "--", "compiler/src"],
                       capture_output=True, text=True).stdout.split()
bad = 0
for f in files:
    if f.endswith("resolver/res.cryo"):
        continue
    lines = open(f, encoding="utf-8", newline="").read().split("\n")
    current_fn = ""
    changed = False
    for n, line in enumerate(lines):
        m = re.match(r"^    (?:static |override )?([a-z_]\w*)\(", line)
        if m:
            current_fn = m.group(1)
        if CALL not in line or line.lstrip().startswith("//"):
            continue
        table = STATIC_TABLE.get((f, current_fn), TABLE.get(f))
        if table is None:
            print("UNPLACED %s:%d %s" % (f, n + 1, line.strip()))
            bad += 1
            continue
        while CALL in line:
            end = line.index(CALL)
            start = receiver_start(line, end)
            recv = line[start:end]
            if not recv:
                print("NO RECEIVER %s:%d" % (f, n + 1))
                bad += 1
                break
            line = line[:start] + "%s.path_of(%s)" % (table, recv) + line[end + len(CALL):]
        lines[n] = line
        changed = True
        print("%s:%d %s" % (f, n + 1, line.strip()))
    if changed:
        open(f, "w", encoding="utf-8", newline="").write("\n".join(lines))
sys.exit(1 if bad else 0)
