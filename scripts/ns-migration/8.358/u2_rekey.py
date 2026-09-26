"""Unit 2: the visibility store keyed by the definition, not by its path."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:90])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

RES = 'compiler/src/compiler/resolver/res.cryo'
edit(RES, [
("""    Instance;
}
""", """    Instance;
}

/// Whether a definition may be named from outside the module that declares
/// it, as its registration recorded.  `Unrecorded` until the registrar that
/// holds the declaration says, so a definition asked about before then is
/// told apart from a private one.
type enum DeclVisibility {
    Unrecorded;
    Public;
    Private;
}
""", 1),
("""    paths:   SymbolStr[];
    spans:   SourceSpan[];

public:""", """    paths:   SymbolStr[];
    spans:   SourceSpan[];
    visibility: DeclVisibility[];

public:""", 1),
("""            paths:   [],
            spans:   [],
        };""", """            paths:   [],
            spans:   [],
            visibility: [],
        };""", 1),
("""        this.spans.push(span);
        return DefId { index: at };""", """        this.spans.push(span);
        this.visibility.push(DeclVisibility::Unrecorded);
        return DefId { index: at };""", 1),
("""    kind_of(&this, d: DefId) -> DefKind {
        return this.kinds[d.index as i64];
    }
}
""", """    kind_of(&this, d: DefId) -> DefKind {
        return this.kinds[d.index as i64];
    }

    /// The module `d` is declared in: `d` itself for a module, else the
    /// nearest module on its parent chain; invalid when there is none.
    module_of(&this, d: DefId) -> DefId {
        mut at: DefId = d;
        while (at.is_valid()) {
            if (this.kinds[at.index as i64] == DefKind::Module) { return at; }
            at = this.parents[at.index as i64];
        }
        return DefId::invalid();
    }

    /// Record `d`'s visibility.  Called by the registrar that holds the
    /// declaration, which is the one place its `public` / `private` is read.
    record_visibility(mut &this, d: DefId, is_public: boolean) -> void {
        if (!d.is_valid()) { return; }
        this.visibility[d.index as i64] = if (is_public) {
            DeclVisibility::Public
        } else {
            DeclVisibility::Private
        };
    }

    /// `d`'s visibility as its registration recorded it.
    visibility_of(&this, d: DefId) -> DeclVisibility {
        if (!d.is_valid()) { return DeclVisibility::Unrecorded; }
        return this.visibility[d.index as i64];
    }
}
""", 1),
])

DI = 'compiler/src/compiler/decl_index.cryo'
edit(DI, [
("""    // Qualified declaration-name SymbolStr.id -> is_public, for TYPES and FREE
    // FUNCTIONS alike, recorded by every registration
    // (`register_decl_in_index`, type_resolution.cryo).  A name absent from
    // this map is a declaration that never registered, recorded as such
    // (`is_candidate_public`).
    //
    // It does not gate bare function calls.  A bare `foo()` binds from its
    // stamp, and the name layer refuses a private declaration at the import
    // that would have bound it; this map is read for a QUALIFIED call's
    // callee (`enforce_callee_visibility`), where a module's own private
    // items are never refused because the namespaces are compared first.
    decl_visibility:     HashMap<u32, boolean>;
""", "", 1),
("""            decl_visibility:    HashMap::<u32, boolean>::new(),
""", "", 1),
("""        this.type_reverse.insert(ty.id, def);
        this.decl_visibility.insert(qualified_name.id, is_public);""",
 """        this.type_reverse.insert(ty.id, def);
        this.defs.record_visibility(def, is_public);""", 1),
("""    /// Record a declaration's visibility (keyed by its qualified name) so
    /// same-leaf resolution can exclude a private cross-module candidate.
    /// Covers types AND free functions; both share one identity space here.
    set_decl_visibility(mut &this, qualified: SymbolStr, is_public: boolean) -> void {
        this.decl_visibility.insert(qualified.id, is_public);
    }""",
 """    /// Record a function's visibility on its definition: a type's is
    /// recorded by `register_type`.  A type and a function declared under one
    /// leaf are two definitions with a verdict each.
    ///
    /// It does not gate bare function calls.  A bare `foo()` binds from its
    /// stamp, and the name layer refuses a private declaration at the import
    /// that would have bound it; the verdict is read for a QUALIFIED call's
    /// callee (`hidden_from`), where a module's own private items are never
    /// refused because the modules are compared first.
    set_decl_visibility(mut &this, def: DefId, is_public: boolean) -> void {
        this.defs.record_visibility(def, is_public);
    }""", 1),
])

src = open(DI, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
a = src.index('    /// The defining namespace of a qualified name (everything before the leaf),')
b = src.index('    /// The namespaces reachable without an import, as recorded.', a)
src = src[:a] + src[b:]
a = src.index('    /// Visibility verdict for a candidate.  Every registration records one')
b = src.index('    /// Whether the definition `d` is private to a module other than', a)
c = src.index('        return !this.is_candidate_public(q);', b)
c = src.index('    }', c) + len('    }')
new = """    /// Whether the definition `d` is private to a module other than
    /// `use_module`: declared in another module, and recorded as not public
    /// by its registration.  False for a definition of `use_module` itself,
    /// or one declared in no module.
    ///
    /// Every registration records a verdict (a type's with `register_type`,
    /// a function's beside its signature), so a definition with none never
    /// registered - recorded as such, which fails the build; the answer given
    /// meanwhile is the top-level default rather than a refusal that would
    /// report a public item as private.
    hidden_from(&this, d: DefId, use_module: DefId) -> boolean {
        if (!d.is_valid() || !use_module.is_valid()) { return false; }
        const home: DefId = this.defs.module_of(d);
        if (!home.is_valid() || home.equals(use_module)) { return false; }
        return match (this.defs.visibility_of(d)) {
            DeclVisibility::Public     => { false }
            DeclVisibility::Private    => { true }
            DeclVisibility::Unrecorded => {
                compiler::resolver::res::record_unregistered_def(
                    "decl_index: visibility of a definition no registration recorded a verdict for");
                false
            }
        };
    }""".replace('\n', nl)
src = src[:a] + new + src[c:]
open(DI, 'w', encoding='utf-8', newline='').write(src)
print('ok')
