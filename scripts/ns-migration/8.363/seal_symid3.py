import io

ROOT = r"C:\Programming\apps\CryoLang\compiler\src\compiler"


def edit(rel, pairs):
    p = ROOT + "\\" + rel
    s = io.open(p, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    for a, b in pairs:
        a = a.replace("\n", nl)
        b = b.replace("\n", nl)
        assert s.count(a) == 1, (rel, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, "w", encoding="utf-8", newline="").write(s)


edit(r"passes\move_check.cryo", [
("""    mark_moved(mut &this, sym: SymbolID, span: SourceSpan, real: boolean) -> void {
        if (!sym.is_valid()) { return; }
        if (this.is_moved(sym)) {
            // Already moved: upgrade to a real (storage-duplicating) move
            // if this site is one, so a borrow-then-move sequence still
            // registers for loop-carried detection.
            if (real) {
                for (mut i: i64 = 0; i < this.moved_keys.length; i++) {
                    if (this.moved_keys[i] == sym.key()) { this.moved_real[i] = true; }
                }
            }
            return;
        }
        this.moved_keys.push(sym.key());""",
"""    mark_moved(mut &this, sym: SymbolID, span: SourceSpan, real: boolean) -> void {
        this.mark_moved_key(sym.key(), span, real);
    }

    /// `mark_moved` by the binding's key, as this pass's tables hold it.
    mark_moved_key(mut &this, key: u64, span: SourceSpan, real: boolean) -> void {
        if (key == 0) { return; }
        mut was_moved: boolean = false;
        for (mut i: i64 = 0; i < this.moved_keys.length; i++) {
            if (this.moved_keys[i] == key) { was_moved = true; }
        }
        if (was_moved) {
            // Already moved: upgrade to a real (storage-duplicating) move
            // if this site is one, so a borrow-then-move sequence still
            // registers for loop-carried detection.
            if (real) {
                for (mut i: i64 = 0; i < this.moved_keys.length; i++) {
                    if (this.moved_keys[i] == key) { this.moved_real[i] = true; }
                }
            }
            return;
        }
        this.moved_keys.push(key);"""),
("""            this.mark_moved(SymbolID::new(u_keys[i]), u_spans[i], u_real[i]);""",
 """            this.mark_moved_key(u_keys[i], u_spans[i], u_real[i]);"""),
])
edit(r"resolver\resolver.cryo", [
("import compiler::resolver::symbol_id::{ SymbolID };",
 "import compiler::resolver::symbol_id::{ SymbolID, SymbolIds };"),
])
print("ok")
