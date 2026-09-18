"""Driver experiment: every analysis phase runs over EVERY module and stops
after completing only if it emitted errors itself; the name-resolution
phase's errors do not stop the walk (a refused name is an `Err` answer the
later stages read).  `run_all` already continues past reported errors and
stops before the first pass that builds from the tree."""
import io, re
P = r"C:\Programming\apps\CryoLang\compiler\src\compiler\instance.cryo"
src = io.open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in src else "\n"

PHASES = ["collect_type_declarations", "register_templates",
          "expand_defaults_and_signatures", "resolve_types",
          "process_directives", "typecheck_bodies", "monomorphize"]

lines = src.split(nl)
out = []
i = 0
fn = None
edited = set()
while i < len(lines):
    line = lines[i]
    # Any method header ends the previous function's span, whatever its
    # parameters are; only the phase functions' own headers select one.
    if re.match(r"^    (static )?[a-z_]+\(", line):
        fn = None
    m = re.match(r"^    ([a-z_]+)\(&this, ctx: CompilationContext\*, pipeline: PassRegistry\*", line)
    if m:
        fn = m.group(1)
    if fn in PHASES and line == "        const n: u32 = order.length as u32;":
        out.append(line)
        out.append("        // The phase's own errors, so the walk stops after it has reported")
        out.append("        // every module's rather than at the first module that had one.")
        out.append("        const phase_snap: DiagnosticSnapshot = ctx.diagnostics.snapshot();")
        edited.add(fn + ":snap")
        i += 1
        continue
    if fn in PHASES and line == "        return true;" and lines[i + 1] == "    }":
        out.append("        return ctx.diagnostics.errors_since(phase_snap) == 0;")
        edited.add(fn + ":ret")
        i += 1
        continue
    if fn == "resolve_types" and line == "        return CompilerInstance::check_export_grants(ctx);" and lines[i + 1] == "    }":
        out.append("        const grants_ok: boolean = CompilerInstance::check_export_grants(ctx);")
        out.append("        return grants_ok && ctx.diagnostics.errors_since(phase_snap) == 0;")
        edited.add(fn + ":ret")
        i += 1
        continue
    out.append(line)
    i += 1

expected = {f + ":snap" for f in PHASES} | {f + ":ret" for f in PHASES}
missing = expected - edited
assert not missing, missing
io.open(P, "w", encoding="utf-8", newline="").write(nl.join(out))
print("phase gates edited:", len(edited))
