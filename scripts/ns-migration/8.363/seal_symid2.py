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


edit(r"passes\drop_insertion.cryo", [
("""    mark_moved(mut &this, sym: SymbolID, span: SourceSpan) -> void {
        if (!sym.is_valid()) { return; }
        // Inside a simple statement, a flagged binding's move is written by
        // the statement itself once it ends.
        this.mark_moved_gov(sym, span,
            this.stmt_writes_flags && this.get_drop_flag(sym.key()).is_valid());
    }

    /// `mark_moved`, with whether the move's flag write is in place given.
    mark_moved_gov(mut &this, sym: SymbolID, span: SourceSpan, gov: boolean) -> void {
        if (!sym.is_valid()) { return; }
        if (this.is_moved(sym)) { return; }
        this.moved_keys.push(sym.key());""",
"""    mark_moved(mut &this, sym: SymbolID, span: SourceSpan) -> void {
        this.mark_moved_key(sym.key(), span);
    }

    /// `mark_moved` by the binding's key, as this pass's tables hold it.
    mark_moved_key(mut &this, key: u64, span: SourceSpan) -> void {
        if (key == 0) { return; }
        // Inside a simple statement, a flagged binding's move is written by
        // the statement itself once it ends.
        this.mark_moved_gov_key(key, span,
            this.stmt_writes_flags && this.get_drop_flag(key).is_valid());
    }

    /// `mark_moved`, with whether the move's flag write is in place given.
    mark_moved_gov(mut &this, sym: SymbolID, span: SourceSpan, gov: boolean) -> void {
        this.mark_moved_gov_key(sym.key(), span, gov);
    }

    mark_moved_gov_key(mut &this, key: u64, span: SourceSpan, gov: boolean) -> void {
        if (key == 0) { return; }
        if (this.is_moved_id(key)) { return; }
        this.moved_keys.push(key);"""),
("""            if (this.link_payload[i] == sym.key()) {
                this.mark_moved(SymbolID::new(this.link_subject[i]), span);
            }""",
"""            if (this.link_payload[i] == key) {
                this.mark_moved_key(this.link_subject[i], span);
            }"""),
("""    lookup_type(&this, sym: SymbolID) -> TypeRef {
        if (!sym.is_valid()) { return TypeRef::invalid(); }
        for (mut i: i64 = this.types_keys.length - 1; i >= 0; i--) {
            if (this.types_keys[i] == sym.key()) { return this.types_refs[i]; }
        }
        return TypeRef::invalid();
    }""",
"""    lookup_type(&this, sym: SymbolID) -> TypeRef {
        return this.lookup_type_key(sym.key());
    }

    /// `lookup_type` by the binding's key, as this pass's tables hold it.
    lookup_type_key(&this, key: u64) -> TypeRef {
        if (key == 0) { return TypeRef::invalid(); }
        for (mut i: i64 = this.types_keys.length - 1; i >= 0; i--) {
            if (this.types_keys[i] == key) { return this.types_refs[i]; }
        }
        return TypeRef::invalid();
    }"""),
("""            const ty:  TypeRef   = this.lookup_type(SymbolID::new(key));""",
 """            const ty:  TypeRef   = this.lookup_type_key(key);"""),
])

edit(r"resolver\resolver.cryo", [
("""    next_symbol_id:     u64;""", """    symbol_ids:         SymbolIds;"""),
("""            next_symbol_id:       1,""", """            symbol_ids:           SymbolIds::new(),"""),
("""    alloc_symbol_id(mut &this) -> SymbolID {
        const id: SymbolID = SymbolID::new(this.next_symbol_id);
        this.next_symbol_id += 1;
        return id;
    }""",
"""    alloc_symbol_id(mut &this) -> SymbolID {
        return this.symbol_ids.next();
    }"""),
])
print("ok")
