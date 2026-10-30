# Name resolution: the five rules and the doors

Normative. This is what "names are resolved correctly" means for the
compiler in `compiler/src`, stated as rules a change can be checked against.
Where the code breaks one, the code is the defect.

## The five rules

1. **Every written name is resolved once, where it is written.** The name
   layer stamps the node that carries it with a definition, or refuses it at
   that spot. No later pass looks a name up again from its text. A consumer
   that finds an unstamped node treats it as a hard error, never as a cue to
   try another lookup.  A pass that synthesizes a name-bearing node -
   desugaring, the async lowering, the AST cloner - knows what it refers
   to, so it creates the node already stamped.
2. **Stores are keyed by identity.** Every write into a declaration store
   takes its key from a definition, type or module identity, or from a door.
3. **A spelling reaches a lookup only through a door** from the list below.
4. **Identities are opaque.** The text an identity hands back (a path, a
   leaf, a display name) never feeds a lookup key.
5. **The answers are right.** Not mechanically checkable; a committed corpus
   of resolution shapes with expected answers stands in for it.

```cryo
// Rule 3, refused: a spelling reaches the store outside a door.
const t: TypeRef = types_by_name.get(&intern("Point"));

// Allowed: the name was bound where it was written; the table is asked by
// the identity the stamp carries.
const k: DefKind = defs.kind_of(node.def);

// Rule 4, refused: identity text rebuilt into a key.
const key: SymbolStr = intern(table.shown(defs.path_of(d)) + "::new");
```

A definition's path or leaf is read as a `DeclName`, whose text comes back
only through `InternTable::shown` (for a message or a mangled symbol) and
`DeclName::spelling` (for a node synthesized to carry the name).  A named
type's display (`TypeArena::format_display`) is its declaration's
`DeclName` shown, and a function's linker symbol
(`DeclarationIndex::entry_symbol`) is a `MangledName`.

## The doors

A door is the one function where a spelling may become a binding, for one
kind of question.  The doors are the functions the table below lists; the
table is the normative list.

The ruled kinds of door: the bare-identifier scope lookup and its
type-namespace twin; one door per member kind, each taking an owner identity
plus a leaf name; module-by-path (private to the module graph, which offers
its callers methods each named for the one kind of text it turns into a
module); the loader's path door, which turns a module path written in an
`import`, `export` or `module` line into the file it names, and matches a
body path's head against the file's imports and its continuation against
the namespaces files declare, before any module exists - discovery's
written paths are spellings (`SymbolStr`), and what the door answers is a
file, whose path keys discovery's own sets; the fixed
primitive and language-item table; the interner; and diagnostic suggestions,
whose result may flow only into message text.

No mechanical check enforces rules three and four.  They are held by
structure: each store keeps its names in private fields reachable only
through its door; tables are keyed by typed identities (`DefKey`,
`TypeKey`, `SymbolKey`, `OverloadKey` and the rest), which a spelling's
number cannot fill; a definition's name is a `DeclName` and a link symbol a
`MangledName`, whose text comes back only through `InternTable::shown`;
and the named types' paths are sealed the same way.  A new accessor that
hands identity text out as a `SymbolStr`, or a table keyed by a raw number
or text, is a change a reviewer has to refuse.

Rule three is also held by visibility, one store at a time: a store's name
fields are `private` to the module whose file holds the store and its
door, so a lookup written by hand anywhere else is refused as a private
access (E0353), and every other reader asks for the name as text to print
or reads the store by identity.  Locked so far:

| store | file | its door(s) | readers outside |
|---|---|---|---|
| the scope ribs (`Scope`, `ScopeEntry`) | `compiler/src/compiler/resolver/resolver.cryo` | `scope-value`, `scope-type` | by identity: `Scope::binding_count`, `binding_at`, `Resolver::visible_bindings` (a scope's rib chain), `Resolver::scope_at` (the scope around a position in a module); a module's own binding by leaf: `Resolver::module_binding` |
| an enum type's variants (`EnumVariantInfo.name`) | `compiler/src/compiler/types/user_defined.cryo` | `member-variant` (`EnumType::variant_index`) | text for a message: `EnumVariantInfo::display_name` |
| the declaration index's families and module globals (`overload_index`, `func_type_refs`, `refused_methods`, `overload_func_family`, `overload_func_mangled`, `module_global_leaves`, `module_global_extern_syms`, `extern_symbol_first`, `family_slot`) | `compiler/src/compiler/decl_index.cryo` | `member-function`, `member-global` | none |
| the interner (`strings`, `lookup`) | `compiler/src/compiler/resolver/intern_table.cryo` | `intern` | `resolve`, `shown` |
| a struct's fields (`FieldInfo.name`) | `compiler/src/compiler/types/user_defined.cryo` | `member-field` (`StructType::field_position`; `MemberResolver::field_of` asks it) | by position; the name sealed for a message or a synthesized node: `FieldInfo::decl_name` |
| the loader's namespace maps (`ModuleLoader.scanned_ns`, `ns_map`) | `compiler/src/compiler/module_loader.cryo` | `loader-path` | none |
| the module graph's names (`ModuleInfo.name`, `namespace_name`; `ModuleGraph.name_index`, `module_defs`, `def_list`, `def_paths`) and each module's import list (`ModuleInfo.visible`) | `compiler/src/compiler/module_graph.cryo` | `module-by-path` | by identity: `ModuleInfo::path`, `namespace_path`, `declares_namespace`, `ModuleGraph::imports`; a body path's modules through `ModuleGraph::modules_written_as`; text for a message, a mangled symbol or a manifest entry through `ModulePath::decl_name`; a declaration's canonical name, `module::leaf`, composed by `ModulePath::qualified` and nowhere else |
| the template registry's names (`TemplateEntry.name`) | `compiler/src/compiler/types/generic_registry.cryo` | none: the registry is asked by definition | text for a message: `TemplateEntry::display_name`; a mangled name is spelled from `leaf_name`, a `DeclName` |

<!-- doors -->
| door | function | file | kind |
|---|---|---|---|
| `scope-value` | `Resolver::lookup_value` | `compiler/src/compiler/resolver/resolver.cryo` | bare identifier to its binding, value namespace |
| `scope-type` | `Resolver::type_binding` | `compiler/src/compiler/resolver/resolver.cryo` | written type name, or a type path's first segment, to its binding in the writer's ribs; the name layer's `NameResolver::type_spelling_res` asks it and reads the binding's kind |
| `scope-type` | `Resolver::type_res_in_scope` | `compiler/src/compiler/resolver/resolver.cryo` | written type name's leaf, type namespace, walking outward from an explicit scope: a module's own scope |
| `member-function` | `DeclarationIndex::lookup_family_entries` | `compiler/src/compiler/decl_index.cryo` | member: a function or method family, by owner and leaf; a generic type's own methods under the template itself (`FamilyOwner::Template`) |
| `member-export` | `Resolver::exports_named` | `compiler/src/compiler/resolver/resolver.cryo` | member: a module's public declarations, by the module and leaf, read off its export list |
| `member-global` | `DeclarationIndex::global_in_module` | `compiler/src/compiler/decl_index.cryo` | member: a module's global, by the module and leaf |
| `member-field` | `MemberResolver::field_of` | `compiler/src/compiler/sema/member_resolver.cryo` | member: a field, by owner type and leaf |
| `member-field` | `StructType::field_position` | `compiler/src/compiler/types/user_defined.cryo` | member: a field of a struct in hand, by leaf, to its position among the struct's fields |
| `member-variant` | `EnumType::variant_index` | `compiler/src/compiler/types/user_defined.cryo` | member: an enum variant, by owner type and leaf |
| `member-variant` | `EnumDecl::variant_position` | `compiler/src/compiler/AST/declaration.cryo` | member: an enum variant, by the enum's declaration and leaf, to its position among the declared variants - for a constant folded off the declaration |
| `trait-method` | `GenericRegistry::trait_item_slot` | `compiler/src/compiler/types/generic_registry.cryo` | member: a trait's declared method, by trait identity and leaf, to its position in the trait |
| `member-constant` | `ConstantTable::type_constant` | `compiler/src/compiler/const_table.cryo` | member: a type's constant, by the owning type's definition (a primitive's included) and leaf - for a path `T::NAME` sema types and for a compile-time integer the folder reads |
| `assoc-type` | `TraitDeclNode::assoc_type_index` | `compiler/src/compiler/AST/declaration.cryo` | member: a trait's associated type, by the trait's declaration and leaf, to its position among the trait's associated types |
| `module-by-path` | `ModuleGraph::module_named` | `compiler/src/compiler/module_graph.cryo` | written module path to its module |
| `module-by-path` | `ModuleGraph::module_path_over` | `compiler/src/compiler/module_graph.cryo` | an import's written path to itself as a path to list modules within, when the graph holds a module at it or under it, whether or not it names a module (`import std::`) |
| `module-by-path` | `ModuleGraph::modules_in_view` | `compiler/src/compiler/module_graph.cryo` | a path written in a body to the modules it names, among those the writing file can see: its own, the ones it imports, the prelude, or a package root written whole |
| `loader-path` | `ModuleLoader::resolve_import_path` | `compiler/src/compiler/module_loader.cryo` | a module path written in an `import`, an `export` or a `module` line to the file of the module it names: a vendored library, a file declaring the namespace, or a file at the path on disk |
| `loader-path` | `ModuleLoader::binding_of` | `compiler/src/compiler/module_loader.cryo` | a body path's head to the import of the file that binds it |
| `loader-path` | `ModuleLoader::sub_module_file` | `compiler/src/compiler/module_loader.cryo` | a path's continuation past an import binding to the file of the sub-module it names |
| `primitive` | `ResBase::is_primitive_spelling` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table |
| `primitive` | `ResBase::primitive_of_alias` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: alias keywords |
| `primitive` | `ResBase::primitive_position` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: a primitive's position, which its definition is kept under |
| `lang-item` | `GenericRegistry::claim_wellknown` | `compiler/src/compiler/types/generic_registry.cryo` | the fixed language-item table |
| `lang-item` | `LangModule::is_written` | `compiler/src/compiler/resolver/lang_item.cryo` | the fixed table of modules the compiler imports on a program's behalf: whether an import's written path names one (the prelude a file already globs) |
| `lang-item` | `IntrinsicKind::from_name` | `compiler/src/compiler/intrinsic_kind.cryo` | the fixed intrinsic table: an `intrinsic function` declaration's name to the operation it lowers as |
| `intern` | `InternTable::intern` | `compiler/src/compiler/resolver/intern_table.cryo` | text to `SymbolStr` |
| `suggestion` | `find_best_candidate` | `compiler/src/compiler/diag/edit_distance.cryo` | "did you mean": result flows only into message text |
| `suggestion` | `Diagnostics::find_shadowed_type_candidates` | `compiler/src/compiler/sema/diagnostics.cryo` | "import the type you meant": the paths, as text, of the types whose last segment is a refused access's type spelling and which own its member; result flows only into message text |
<!-- /doors -->

What this table does not say: that the tree obeys rule three. Other
functions in `compiler/src` still look a spelling up - duplicates of a
door (`EnumType::get_variant` beside `variant_index`, the rib walks
`Resolver::lookup` and `lookup_prelude` beside `lookup_value`, the four
family readers beside `lookup_family_entries`) and lookups outside every
door. Those are conversions still to make: each becomes a door's body, a
caller of a door, or an identity lookup.

## Appendix: rulings

Decisions Jake made in plain text on questions the rules above leave open.
Each is stated as the behaviour it requires; whether the compiler has it
yet is recorded with the change that builds it, not here.

### 2026-10-30

667. **`.` reads through any number of borrows and one raw pointer.**
     Field access and method calls step through every `&` and `mut &`
     level in front of the value, but through at most one raw pointer
     (`T*` or `const T*`), counted over the whole chain whatever the
     order of its levels: `r.x` with `r: &(&Foo)`, `&(Foo*)` or
     `(&Foo)*` reads the field, and `p.x` with `p: Foo**` is refused
     (E0222), with `(*p).x` as the way to write it.  A refused access
     never reaches code generation.  (Jake)

668. **With nothing expected of it, `&x` is exactly as writable as `x`.**
     A borrow taken where no type asks for one - a `match` subject among
     them - is a writable borrow of a writable place and a read-only
     borrow of a read-only one; where a type is expected, that type says
     which borrow is wanted, and a read-only place never gives a writable
     one.  (Jake)

### 2026-10-29

663. **A binding from a match on a borrow is writable exactly when the
     borrow is.**  A match on a `mut &T` or a writable `T*` binds its
     payloads as writable places, so `&value` of such a binding may be
     handed to a writer; the same match on a `&T` or a `const T*` binds
     them read-only.  (Jake)

664. **An accessor handing out a pointer into a read-only receiver is
     decided per accessor.**  A `&this` method returning a pointer into
     `this` returns `const T*` where its callers only read through it,
     and becomes a `mut &this` method returning `T*` where they write.
     A `&this` method never hands out a writable pointer into `this`.
     (Jake)

665. **Every instance method writes its receiver.**  A method that is
     not `static` and writes none of `&this`, `mut &this` and `this` is
     refused where it is named (E0118, a new code), with a help naming
     the three forms; it is no longer taken as `&this`.  Each receiver
     says what the method does: `mut &this` where it writes the
     receiver, directly or through a call, and `&this` otherwise.
     (Jake)

666. **A read-only level above another pointer groups that pointer.**
     `const` stays in front and marks the pointee of the first `*`; a
     parenthesised pointer type after it is that pointee, so
     `const (const u8*)*` is a read-only pointer to read-only pointers to
     `u8` (C's `const char* const*`).  The parentheses group one pointer
     level and are written only where a level is nested; anything else in
     them is refused.  Types print in this form, and generated bindings
     write it for each `const` level of a C declaration.  (Jake)

### 2026-10-26

598. **A member's visibility keyword comes after its directives.**  In
     a struct or union body, `![inline] public length(&this)` marks the
     method public, as the same order does on an implement-block method,
     an `extern` function and a top-level item.  The keyword is read in
     that one place, so `public ![inline] length` is refused in a struct
     body as it already was everywhere else; no file in the tree wrote
     that order.  (Jake for the order after directives; refusing the
     other order delegated)

599. **The rest of `net` is marked item by item.**  `Headers` keeps its
     map private and hands out its entries through a public `entries()`
     iterator, which HTTP/2 walks.  An allocation failure converts to an
     I/O error, `OutOfMemory`, through a public `From<AllocError>` for
     `IoError`.  In HTTP/2 the protocol's constants (frame types, flags,
     the frame header length, settings identifiers, the preface's length
     and bytes, the default connection window) are public and the limits
     the connection chooses for itself are private; `Frame`, `Settings`,
     `HeaderField` and `RequestOnStream` are plain records with public
     fields; `Http2Connection` keeps its state private behind its
     handshake, request, serve and frame-level calls.  Sockets, TLS
     streams and their futures keep their handles private behind public
     methods; the TLS error normaliser is private to the futures that use
     it; the unused ALPN length constant, which held the wrong length, is
     deleted.  WebSocket's records and handshake functions are public and
     its own caps private.  (Jake for headers, the conversion, the
     preface, the normaliser and the constant; the rest delegated under
     587)

600. **The unused-code lints run on the package being built.**  W0001,
     W0002 and W0009 report a module that belongs to the package a build
     is building, and no other: the standard library is linted by its own
     build and not by a program's, and a dependency or vendored package by
     its own build and not by its dependents'.  W0002 reports only a
     private function or method; a public or internal one nothing in its
     package calls is API for other packages, never reported and never
     removed for that.  A private helper whose only callers are gated to
     one target is gated to that target too, so no build carries it dead.
     (Jake; gating target-only helpers delegated)

601. **`?` converts an error through `From`.**  On a `Result<T, E>` in a
     function returning `Result<U, F>`, an `E` that `F` cannot hold as it
     is goes out as `Err(F::from(e))`, through the language's `From`
     (`![language("from")]`, its method `![language("from_from")]`); an
     `E` that `F` already holds is returned unchanged, so every `?` that
     compiled before means what it meant.  No implementation of
     `From<E>` for `F` is E0214 at the `?`, naming the conversion that is
     missing.  The conversion is chosen by the trait's identity and its
     argument, never by a method spelled `from`.  A generic body is
     judged by its bounds (604).  (Jake for the conversion; the error code
     delegated)

602. **The standard library has no one-off helpers.**  A private helper
     that restates an operation is replaced by a general API its callers
     use directly; moving it elsewhere, into a type as a static method or
     another module, is not a replacement.  Where no general API exists,
     one is added and approved first.  Two public APIs for one operation
     keep one: `File`'s `seek_set`, `seek_cur` and `seek_end` are deleted
     for `Seek.seek(SeekFrom)`, and `JsonValue`'s `null_value`,
     `float_value`, `string_value`, `empty_array` and `empty_object` for
     the variants they wrapped.  (Jake)

603. **`String.push` appends any `Display` value; one UTF-8 encoder.**
     `push` and `try_push` take any `T: Display`: text is copied in, every
     other type is formatted.  `push_char(c)` appends one scalar and
     `push_repeated(c, count)` appends `count` copies.  Every `char` is
     encoded by the one private encoder behind `FmtWrite.write_char`, which
     writes a surrogate or an out-of-range value as U+FFFD; `char`'s
     `Display` writes through `write_char`.  The format spec's fill, the
     JSON escape decoder and serializer, the HTTP client's decimal writers
     and the test runner's number text use these instead of their own
     loops.  (Jake)

604. **A `?` in a generic body converts through a bound.**  A generic
     body is typed once, by its bounds, and every instance carries what
     that walk built; nothing walks an instance's copy again.  So in a
     generic function a `?` whose error conversion involves a type
     parameter is the conversion a `where` bound gives the function's
     error type: `F: From<E>` or `HighError: From<E>` exactly, the trait
     by identity and its argument as written, and the `?` returns
     `Err(F::from(e))`, each instance calling the `from` its own types
     select (616).  With no such bound - none at all, or one naming
     another argument - the `?` is E0214 where it is written, whether or
     not anything instantiates the body, and the help names the bound to
     add.  A conversion between concrete types in a generic body is
     judged as anywhere else.  (delegated; the bound on a concrete error
     type Jake)

605. **Addresses, URLs, methods and status codes are `Display`.**  An
     `IpV4Addr` writes dotted decimal; an `IpV6Addr` its canonical text
     (RFC 5952): lowercase hex groups without leading zeros, the longest
     run of two or more zero groups written `::`, the first of equally
     long runs, and no dotted tail; an `IpAddr` writes its address.  A
     `SocketAddr` writes `ip:port`, an IPv6 address in brackets.  A `Url`
     writes `scheme://host[:port]path[?query][#fragment]`: an IPv6 host
     in brackets, the port only when it is not the scheme's default, and
     no userinfo, which the parser does not keep.  A `Method` writes its
     wire form and a `StatusCode` its code in decimal.  The HTTP client
     writes its `Host` header through `SocketAddr`'s, so an IPv6 host is
     sent compressed, and its own address writers are deleted.  (Jake for
     the types and the compressed form; the URL's port and userinfo and
     the bare status code delegated)

606. **A bounded type parameter converts to a trait object.**  In a
     generic body, a value whose type names a parameter becomes a trait
     object where every copy of the body has an implementation of the
     trait for it: a type parameter whose own bound names the trait at
     the object's arguments (`where W: FmtWrite`), `This` in a default
     of that trait itself, and the receiver of an implementation of the
     trait whose methods are checked over parameters - a generic
     implementation, or one written for an instantiation.  The template
     is checked once and the conversion is settled in each copy, which
     takes the table of its own type.  Anything else naming a parameter -
     no bound, a bound naming another trait, `This` in another trait's
     default - is E0315, and for a parameter the help names the bound to
     add.  A supertrait's object, and an instantiation over a parameter
     whose implementation holds for every argument, are not converted
     yet.  (delegated; needed for `FmtWrite.write`, a default that makes
     a `Formatter` over its receiver)

607. **An f-string passed as `fmt::Arguments` is written where it goes.**
     An f-string written directly as an argument whose parameter is
     `fmt::Arguments` is not built into a `String`: it becomes its
     pieces - the literal text, and a borrow of each hole's value - and
     the callee writes them through a `Formatter`, each hole formatted
     straight into the sink when it is written.  Anywhere else - an
     initializer, a return, a field, an argument whose parameter is
     anything else, a generic `T` included - an f-string is a `String`,
     so no stored value keeps a borrow of a hole past the call.
     `FmtWrite.write(arguments)` is a default every sink gets, and
     `Formatter.write(arguments)` writes through a formatter;
     `String.push(f"...")` still builds the text first.  A number in a
     hole with a spec is rendered on the stack.  `Stdout` and `Stderr`
     are no longer `FmtWrite` sinks: they write text through
     `io::Write.write`, whose name the default made ambiguous on them.
     The duration and date-time `Display`s and the JSON control escape
     write through `write`; their own decimal, padding and hex writers
     are deleted.  (Jake for `write` on `FmtWrite`, the name
     `fmt::Arguments` and the argument position; dropping `FmtWrite`
     from `Stdout`/`Stderr` and `Formatter.write` delegated)

608. **A byte classifies itself, by `char`'s rules.**  `u8` has
     `is_digit`, `is_alpha`, `is_hex_digit`, `is_whitespace` and
     `to_ascii_lowercase`, each meaning what `char`'s method of that name
     means, so a byte of a multi-byte UTF-8 sequence answers false and
     folds to itself.  ASCII whitespace is one set everywhere: space,
     tab, LF, VT, FF and CR, so `string.trim` now strips VT and FF as
     `Str.trim` and `char.is_whitespace` already did.  The library's own
     whitespace tests (`str::is_ascii_ws`, the `string` trim's), its
     drive-letter test and its byte case folds and digit range checks use
     these.  (Jake for the names and the whitespace set)

609. **`Str` searches from either end; a `Slice` indexes and compares.**
     `Str` has `rfind`, `find_byte`, `rfind_byte`, `split_once`,
     `rsplit_once` (the text before and after the first or last
     separator, neither half including it) and `equals_case_insensitive`
     (ASCII letters folded, every other byte exact).  `Slice<T>`
     implements `Index` for every integer type, as `Array` does - an
     index outside `[0, length)` panics at the `xs[i]` - and `Eq` where
     `T: Eq`, by length and elements, wherever the two point.  The
     library's own byte readers, slice and string comparisons,
     sentinel-returning searches and the test runner's name split use
     these and are deleted; a read that may fall past the end of input a
     peer controls keeps `get(i).unwrap_or(0)`.  `Path::last_separator`
     answers `Option<i64>` instead of -1, and `Path::byte_or_zero` and
     `Path::last_dot` are deleted.  (Jake for the names; the
     `last_separator` signature delegated)

610. **`Str`, `Slice` and `Path` are marked item by item.**  `Str`,
     `Slice<T>`, `Path` and `PathBuf` and all their methods are public,
     their storage private behind `as_ptr`, `as_bytes`, `as_str` and
     `as_path`.  `Str`'s `SplitIter`, `Chars` and `CharIndices` are
     public, since `String`'s methods name them, with private fields; the
     UTF-8 decoding step `decode_scalar`, its `REPLACEMENT_SCALAR` and the
     integer parser's `I64_MIN_MAGNITUDE` are private.  `SliceIter` is
     internal: `Slice` and `Array` both make one and no public signature
     names it.  `Path`'s syntax constants are private and its static
     queries (`is_separator`, `root_len`, `is_absolute_str`,
     `last_separator`) public.  Base64 looks its alphabet up by indexing
     a `Slice`, its own byte readers deleted.  (delegated)

611. **Integers cross bytes through `ByteReader` and `ByteWriter`.**
     `io::bytes::ByteReader` decodes from a borrowed `Slice<u8>` and
     `ByteWriter` appends to a borrowed `Array<u8>`, each with a read or
     write for `u8`, `i8` and the 16-, 32- and 64-bit integers of both
     signs, in `big_endian` and `little_endian` spelled out.  A read
     needing more bytes than remain is `UnexpectedEof` and does not move;
     the reader has `position`, `set_position` (past the end allowed, a
     negative offset panics) and `remaining`.  A write panics where the
     array cannot grow, as `push` does.  The library's wire headers (HTTP/2
     frames, SETTINGS, WINDOW_UPDATE and GOAWAY, WebSocket lengths, mask
     keys and close codes), the `addrinfo` walk and hints, the socket
     timeout, the sockaddr family and port reads, an IPv6 group and the
     generator's seed words use them; their byte helpers are deleted.
     `u8` also has `to_ascii_uppercase`, which `Str`'s fold uses.  (Jake
     for the shape and names; the module, `set_position` and the panicking
     write delegated)

612. **An OS error becomes an `IoError` in `io::error`.**
     `IoError::from_windows_error(code)` maps a Windows system error code
     on every target - not found, permission denied, already exists, out
     of memory, invalid input or data, broken pipe, end of file and
     unsupported, the rest `Other` - keeping the code in `os_code`, and
     `IoError::last_os_error()` reads the calling thread's last error,
     `errno` on POSIX and `GetLastError` on Windows.  A Windows C-runtime
     call still reports through `errno`, mapped by `from_errno`.  `fs`'s
     own Windows mapper is deleted; Windows process creation, standard
     stream setup, wait and kill report the failing call's error instead
     of `Other` with code 0, and the POSIX-only paths read
     `last_os_error`.  `io::error` is marked item by item: the kinds,
     `IoError`'s fields and methods public, its errno table private.
     (Jake for the two names; the mapped codes and the marking delegated)

613. **A float classifies itself and an integer rotates itself.**  `f64`
     has `is_infinite`, `is_finite`, `is_normal`, `is_sign_negative` and
     `classify` (answering `math::FpCategory`) beside `is_nan`, and the
     free `math::is_nan`, `is_infinite`, `is_finite`, `is_normal`,
     `is_sign_negative` and `classify` are deleted.  `u32` and `u64` have
     `rotate_left` and `rotate_right` by a count taken modulo the width,
     and SHA-1's and the generator's own rotate helpers and the float
     formatter's class tests are deleted.  (Jake for `rotate_left`,
     `is_infinite`, `is_finite` and `classify`; `rotate_right` and moving
     `is_normal` and `is_sign_negative` delegated)

614. **A name in a pattern that names a constant compares against it.**
     A bare name in a pattern, at the top or in a payload position, is
     asked in the value namespace where the pattern is written - the
     question an identifier in an expression asks - and when it names a
     constant, wherever that constant is declared (this module, or another
     brought in by an import), the pattern compares the subject against the
     constant; anything else, a local of the same name included, makes the
     name a fresh binding.  A payload-less path pattern whose leading
     segments name a module or nothing (`codes::NOT_FOUND`, an import
     alias's `C::NOT_FOUND`) is a value path, answered as the same path
     written as a value is and typed as one, so an unreachable constant is
     E0353 and an unknown leaf E0201; one naming a function or a `mut`
     global is E0113.  Leading segments that name a type keep the path an
     enum variant, so a type's own constant (`u32::MAX`) is not a pattern
     yet.  An arm written after an unguarded arm that matches every value
     (`_` or a binding) is W0009, once per `match`.  (Jake for the rule and
     the warning; the variant-first reading of a type-led path, E0113 and
     reusing W0009 delegated)

615. **A writer over a fixed region; Winsock errors map in `io::error`; a
     byte array is a format target.**  `ByteWriter::over(region)` writes
     into a `Slice<u8>` from its first byte and panics on a write past its
     end; `position()` says how many bytes a writer has written, either
     kind.  SHA-1's digest, the generator's `fill`, `pack_sockaddr_in` and
     the IPv6 parser's groups write through one, and their own byte stores
     (and the module `random::bits`) are deleted.  `IoError::from_wsa`
     maps a Winsock code on every target, as `from_windows_error` maps a
     system code, so the `WSAE*` codes it reads are declared on every
     target; the network layer's own mapper is deleted.  `Array<u8>`
     implements `FmtWrite`, appending the text's bytes, and `IoError`
     converts from `FmtError` (its I/O error, or the allocation failure),
     so the HTTP status line is written with `write(f"...")` and `?`, and
     HTTP/2's `:status` and both `set_body`s' Content-Length are formatted
     into a byte array, panicking where the array cannot grow.  (Jake for
     `over`, `from_wsa` and the `FmtWrite` impl; `position`, the
     `From<FmtError>` conversion and the ungated codes delegated)

616. **A `where` bound may be written on any type.**  A bound's subject is
     a written type, stamped and resolved like every annotation: a
     parameter (`T: Ord`), a projection (`This::Item: Ord`), a named type
     (`HighError: From<E>`) or an instantiation (`Wrapper<T>: Describe`).
     Which bound is written on a type is one question, asked by type
     identity - a parameter is the one its declaration binds - and every
     use below asks it.  In a function's or method's body the bound is an
     assumption: a path through its subject whose member the bound's trait
     declares, and whose arguments that member takes at the bound's
     arguments, is the trait's member unless a method of the subject's
     own takes them (617); a `?` into the subject converts through it
     (604); a method call on a value of a subject that still names a
     parameter reaches the bound's trait when the subject has no method
     of its own of the name; the impl-qualified path `(From<E> for
     HighError)::from(e)` is the bound's member.  The path or call records the trait's member and
     the parameters the bound gives it, and each copy of the body binds it
     to the implementation of the trait for the copy's subject whose
     method takes those parameters - two instantiations reach two
     implementations.  A path whose arguments the member does not take is
     the subject's own, as without the bound.  At each call the subject and
     the trait's arguments are resolved with the call's type arguments put
     in, and an implementation binding the trait at those arguments must
     exist, or the call is E0306, naming the trait with its arguments.  On
     an `implement` block or a type declaration such a bound means the
     same (618).  (Jake for the bound, the call-site
     check, and the subject's own member first, reversing a delegated
     choice that put the bound's first)

617. **A type's own method outranks a trait's.**  Where a method call,
     a `Type::member` path or a method value finds both a method the
     type declares itself (inherent) and a trait's method of the same
     name, and the arguments bind to both, it names the inherent one,
     whichever is generic and whichever block is written first; the
     impl-qualified path `(Tr for T)::m` names the trait's.  Two traits'
     methods with no inherent one the arguments bind to remain a tie
     (E0156).  A call through a parameter's bound still names the
     bound's trait, since the parameter has no methods of its own.  The
     `?` conversion is directed through `From`, so a type's own `from`
     is never the conversion.  (Jake)

618. **A bound on a type, written on a type declaration or an
     `implement` block.**  The type layer resolves the subject and the
     trait's arguments once, in the declaration's own parameters; every
     check substitutes an instantiation's arguments into that resolution
     and asks for an implementation binding the trait at the substituted
     arguments, the one question 616's call-site check asks too.  In the
     declaration's methods the bound is an assumption, as on a function.
     A type declaration's instantiation whose types do not satisfy it is
     E0306 at the bound, naming the instantiation and the trait with its
     arguments; the check is made where every instantiation is
     specialised, since the implementations it needs are attached after
     a module's annotations are first resolved.  An `implement` block
     applies to an instantiation only when its bound holds, so each copy
     selects the implementation its own types satisfy; a block with no
     parameters applies when the bound holds as written.  (Jake)

619. **A type path in a pattern means what it means as a value.**  A
     payload-less path whose leading segments name a type (`u32::MAX`,
     `Limits::TOP`, `Color::Red`) is walked by the name layer as the same
     path written as a value, and the type layer answers its leaf: a
     constant the type declares makes it a constant pattern, compared as
     any other; otherwise it is the variant it names.  The type layer is
     the only one that can answer, since an alias reaches its target's
     constants and `implement` blocks in other modules add constants to a
     type.  Through a type that is no enum, a method leaf is E0113 and a
     missing leaf E0201, as for a module path.  A primitive type may head
     a path pattern.  (Jake)

620. **Checked and saturating integer arithmetic, one radix parser,
     one `min`/`max`.**  The arithmetic operators still wrap; every
     integer width also has `checked_add`, `checked_subtract` and
     `checked_multiply` (`None` where the operator would wrap) and the
     matching `saturating_*` (the bound the true result lies past), and
     `i64` has `divide_floor` and `modulo_floor`.  `u64::parse_radix`
     reads a run of digits that must be the whole input, and
     `u64::parse_radix_prefix` the longest leading run that fits, with
     its length; every digit loop over text delegates to them, and the
     wrapping `parse_int` folds the runs so it wraps exactly as before.
     `math::min`/`max` and `mem::align_up`/`align_down` are gone:
     `cmp::min`/`max`/`clamp` order any `Ord` type, `math::align_up`
     aligns a size, and `f32.min`/`max` and `f64.min`/`max` keep the
     NaN-ignoring float `min`/`max`.  `f32` has `f64`'s class methods.
     (Jake)

621. **A constant pattern is held to the subject's type.**  A constant
     pattern - a bare name, a module path or a type path - matches only
     a value of the constant's own type; any other is E0200 at the
     pattern, as a literal of the wrong kind is, wherever the pattern
     stands (a payload position is held to the payload's type).  A
     constant of a type no pattern compares by value - a struct, an
     enum with a payload - is E0113.  A float constant compares as `==`
     does, ordered: a NaN constant matches nothing, a NaN subject
     matches no constant, and `0.0` and `-0.0` match each other.  A
     `string` constant compares by content, as a string literal pattern
     does.  (Jake)

622. **One mutex, one condition variable, one thread creator.**  The
     executor, the reactor, the blocking pool and `mpsc` hold their
     locks as `sync::Mutex` and `sync::CondVar` and start their threads
     through `std::thread` (`Scope`, or `try_spawn` where a refused
     thread is not fatal), with no per-OS copy of their own; a module
     whose own type shares a name with one of `thread`'s imports it
     under another name (`import std::thread::{ JoinHandle as
     ThreadJoinHandle }`).  `thread` creates every thread through one
     per-OS function: on Windows a requested stack size is a
     reservation (`STACK_SIZE_PARAM_IS_A_RESERVATION`), committed as the
     stack is used, and a refused spawn carries the OS's own reason - the
     `pthread` return code, or `GetLastError` mapped to `SpawnError` -
     never a fixed stand-in.  `thread::available_parallelism` is the one
     CPU count.  (Jake)

623. **A NaN constant pattern is reported.**  A float constant pattern
     whose value folds to NaN - `f64::NAN`, `f32::NAN`, a module
     constant written `0.0 / 0.0` - can never match, since it compares
     as `==` does, so it warns (`W0016`), at the top level and in a
     payload position alike.  It is a warning, not a refusal: the arm
     is well formed, only never chosen.  A constant whose value does
     not fold is not known to be NaN and is not reported.  (Jake)

624. **A lock holds the state it guards.**  The executor's ready
     queue, the reactor's registrations and timers, the blocking pool's
     job queue and threads, and an `mpsc` channel's queue and handle
     counts each live inside their `Mutex` (`Mutex<ReadyQueue>`,
     `Mutex<ReactorTable>`, `Mutex<PoolState>`, `Mutex<ChannelState<T>>`)
     and are reached only through its guard, so no field can be read
     without the lock.  Teardown takes the lock too, once the threads
     are gone, unlinking one item per acquisition and releasing it after
     the unlock, because releasing a task or a waker can re-enter the
     same lock.  A lock that guards no data - the executor's completion
     lock, whose predicate is each task's handshake atomic - stays a
     `Mutex<()>`.  Behaviour is unchanged.  (Jake)

625. **One non-blocking switch, one borrowed reader.**
     `io::fd::set_nonblocking(fd, on)` turns a descriptor's
     non-blocking mode on or off, keeping its other file-status flags,
     and returns the OS error on failure; on Windows a C runtime
     descriptor has no such mode and the call fails with `Unsupported`.
     `net::sys`'s POSIX switch delegates to it, and the process module's
     two copies are gone.  `io::fd::BorrowedFileDescriptor` reads a
     descriptor it does not own - dropping it closes nothing - through
     `Read`, so a drain is `read_to_end`: to EOF for a blocking pipe, and
     to `WouldBlock` for a non-blocking one, with every byte read before
     it kept.  The child-output drains, `Command::output`'s collectors on
     both systems and the test runner's capture read through it.  A
     drain whose buffer cannot grow now reports `OutOfMemory` rather
     than stopping the process.  (Jake)

626. **Time converts one way.**  `Duration.as_millis_ceil()` is the
     whole milliseconds a span takes, a partial one rounded up so a wait
     never ends early, saturating at `u64::MAX`; Windows' `Sleep` and the
     reactor's wait timeout both round through it.
     `Instant.saturating_add(dur)` is `checked_add` that answers the
     latest reading instead of `None`, and `Instant.as_nanos()` saturates
     at `i64::MAX`, so a timer's deadline for an over-long sleep lies in
     the far future instead of wrapping into the past.
     `thread::sleep(nanos)` is `time::clock::sleep` and has no copy of
     its own, which also leaves `clock`'s POSIX sleep the only
     `Duration`-to-`timespec` conversion, so it needs none named.
     `SystemTime::from_filetime` (internal) is the one `FILETIME`
     conversion, for the wall clock and for a file's modification time,
     dividing by floor so a time before 1970 keeps its nanoseconds in
     range.  (Jake; the last two delegated)

627. **One way to exit, one way to shell out.**  `env::process_exit` is
     gone: a process exits through `sys::exit`, which on Linux ends it at
     once (`exit_group`) without flushing the C runtime's buffered output
     or running its exit handlers, so a caller that printed through the C
     runtime flushes first (`libc::fflush(null)`), as the compiler does
     before it exits on an internal error.  `process::exec` and its
     `shell` are gone: `Command::shell` is the one shell call.  The test
     runner's `cxx` requirement asks whether `c++ --version` launches by
     spawning it with both streams discarded, not through a shell, and is
     unmet on Windows by name, where linking the C++ helper has a gap of
     its own.  (Jake; the flush and the Windows rule delegated)

628. **The compiler's own text rules stay in the compiler.**  `core`'s
     `string` and `char` no longer carry the compiler's helpers:
     `is_text_literal`, `is_float_literal`, `is_unsigned_literal`,
     `parse_c_uint`, `strip_quotes`, `with_exe_suffix` and
     `is_c_ident_start`/`is_c_ident_char` are gone.  The C importer
     classifies a literal's spelling once, as text, float or integer, and
     reads an integer constant's value and unsigned suffix in one pass
     over `u64::parse_radix_prefix` (C's leading-`0` octal included); the
     command line's and the codegen manifest's numbers parse with
     `parse_int`; an identifier byte is `is_alphanumeric()` or `_`; a
     quoted configuration value and an executable suffix are written out
     where they are used.  `string.is_under` stays until a general path
     API replaces it.  (Jake)

629. **`fs::metadata` and `env` are marked.**  `metadata`,
     `symlink_metadata`, `exists`, `is_file`, `is_dir`, `FileType` and
     `Metadata` with its accessors are public; `Metadata`'s fields, the
     platform record offsets, the synthesized Windows permission bits and
     the decoding steps are private.  `env`'s functions are public, the
     argument and environment vectors it holds are private, and
     `set_args` and `set_env`, the language items the generated entry
     point calls to publish them, are internal, as the f-string builders
     are.  (Jake; internal for the two entry-point items delegated)

630. **`math`, `random` and `sync` are marked.**  Every `math` constant
     and function is public.  `random`'s generators, `RandomSource`,
     `Distribution`, the distributions and `RandomError` are public; a
     distribution that only stores its parameters keeps public fields,
     while `Bernoulli`, which clamps its probability, `WeightedIndex` and
     `Rng`'s state words are private, and the Box-Muller step reads
     `math::TAU` rather than a copy of it.  Every lock, guard, channel
     handle and `Atomic` is public with the pointer or value it holds
     private; the heap-pinned records behind them and the channel's nodes
     and steps are private, and `Mutex.raw_pthread_buf` is internal, the
     condition variable's way to pair with a mutex.  A member of a private
     type is written `private`.  (Jake; each split delegated)

631. **`thread` and `fmt` are marked.**  Spawning (`spawn`,
     `spawn_with_attr`, `try_spawn`, `Builder`, `Scope`), `JoinHandle`,
     `ThreadId`, `Thread`, `ThreadLocal`, the scheduling calls and
     `MAX_THREAD_NAME`, the cap `Builder.name` truncates to, are public;
     every handle's OS id and shared block, `ThreadId`'s word (it is
     opaque, compared through `equals`), the result block, the start
     payloads, their trampolines and the detached-thread reaper are
     private.  `fmt`'s print calls, `Formatter` (its writer private behind
     `get_writer`), `Display`, `Debug`, `FmtWrite`, `FmtError`,
     `Arguments`, `parse_f64` and the to-string calls are public.  The
     f-string `String` builders are internal language items, as the pieces
     builders are.  `fmt::spec` is the f-string spec's plumbing: what the
     spec piece calls is internal, the rest private.  (Jake; each split
     delegated)

632. **`io` and `fs` are marked.**  The I/O traits, `SeekFrom`, the
     buffered readers and writers, `Cursor`, the standard streams with
     their locks, `is_tty`, the descriptor numbers and calls of
     `io::fd`, the newline constants, `read_dir`, the directory and file
     calls, `DirEntry`, `ReadDir`, `OpenOptions` and `File` are public,
     and so are the default buffer size, file mode and directory mode.
     `Transfer` keeps public fields, since every `AsyncTransport`
     implementation builds one.  Every buffer, cursor position, directory
     handle and option switch is private behind the type's methods; a
     buffered writer's drain, a stream's compaction, the options'
     translation to `open` flags, the dirent and Win32 attribute decoding
     and the standard streams' lock buffers are private.  `File`'s
     descriptor is not marked: a caller hands it to a spawn by reading
     and clearing it, and hiding it needs an accessor.  (Jake; each split
     delegated)

633. **`json` is marked, and so are the members a primitive's and a
     private type's blocks hold.**  `parse`, the serializers, `JsonValue`,
     `JsonNumber`, `JsonObject`, `JsonExtract` and the error types are
     public, `JsonError`'s position and message as fields, as its
     documentation promises; `JsonObject`'s parallel arrays and index are
     private behind its methods, and the parser's and serializer's byte
     names, cursor and steps are private.  `Parser` itself, its
     constructor, its value entry and `MAX_DEPTH`, which `JsonErrorKind`'s
     documentation names, are not marked.  The float writers on `f32` and
     `f64` are public, with `F64_MAX_DIGITS`, the buffer size their
     contract names, and `DEFAULT_DECIMALS`.  A member of a private type
     in `alloc` and `net` is written `private`.  (Jake; each split
     delegated)

634. **`process` is marked.**  `Command`, `Stdio`, `Output`, `Child`,
     its pipe handles with `from_fd`, `ExitStatus`'s methods, `SigNo`,
     `Signal` and `id` are public.  A command's settings, an output's
     captures and a signal's number are private behind their methods.
     The spawn, pipe, environment and command-line steps, the Windows
     stdio setup and drains, and the wait and drain futures with their
     pool jobs are private, and `PipeDrain`, which a command's capture
     awaits from another file, is internal.  `ExitStatus`'s fields and
     `decode_wstatus` are not marked, because the test runner, whose
     spawning is parked, builds the one and calls the other; nor are the
     pipe handles' descriptors, which a capture takes by reading and
     clearing, as a caller takes `File`'s.  The POSIX pipe helpers are
     gated to POSIX, the only target that calls them.  (Jake; each split
     delegated)

635. **`future` is marked.**  `Future`, `Poll`, `Waker`, `Context`,
     `block_on`, the sample futures, `Sleep`, the combinators with
     `Futures` and `Elapsed`, `Executor`, `JoinHandle` and `JoinError` are
     public, and so is the blocking pool: `BlockingJob` with its `init`,
     `arm`, `retain` and `release`, the job states, `BlockingPool` and its
     default ceiling, since a caller embeds a job in a block of its own and
     submits it.  A waker's vtable and data, a context's waker (read
     through `waker()`), a sleep's deadline and arming, every combinator's
     children and progress, and a job's header past its methods are
     private; a job's state is read through `arm`.  The pool's queue, its
     threads' loop and a job's completion are private, and
     `BlockingPool::set_current`, which an executor worker calls to publish
     its pool, is internal, as the reactor is.  The executor's tasks,
     queue, shared blocks, thunks and worker are private behind `spawn`,
     `block_on` and the join handle.  (Jake; each split delegated)

636. **`collections` is marked.**  `Array`, `String`, `HashMap`,
     `HashSet`, `Pair`, `RawBuffer` and `from_iter` are public with every
     method.  An array's storage and allocator, a map's buckets, count,
     allocator and seed, and a set's map are private behind their methods;
     a pair keeps public fields, and so does a map's `Entry`, the node
     `get_bucket` hands to a caller walking a map in place.  The sizes an
     array, a string and a map start with, the sort steps, the per-process
     seed generator, a map's seeded hashing and its node allocation are
     private, while `hash_str` and `hash_int` stay public.  The iterator
     adapters are private, with `KeysRefIter` internal for the set built
     on it; `copied` and `cloned`, reached through `iter_ref`'s opaque
     result, are public.  `HashMapIter` is not marked: the compiler names
     it.  JSON reads its arrays through `get_ref` and indexing, and the
     adapters' unused constructors are gone.  (Jake; each split
     delegated)

637. **`test` is marked.**  The assertions, `TestError` with its
     constructors and `message_str`, `Fixture` and the table-case
     prefixes are public; the step `expect_eq` and `expect_ne` share to
     build their message is private.  `TestDescriptor`, whose layout the
     generated test main fills, and `run_all`, the entry it calls, are
     internal language items, as the entry point's are.  Everything else
     in the runner, its options, reporters, workers and child handling,
     is private.  A reporter step and a worker result's take that nothing
     called are gone.  (Jake; each split delegated)

638. **`core`'s traits, sum types, operators and helpers are marked.**
     `panic`, `assert`, `unreachable`, `todo`, the language traits,
     `Ordering`, `min`, `max`, `clamp`, the conversion traits,
     `ConversionError`, the hashers, the FNV parameters and `digest`,
     `Iterator` with its adapters, `mem`'s calls, the operator traits,
     `Range` and `RangeInclusive`, `Option`, `Result`, `PanicInfo`,
     `catch_unwind`, `drop_in_place`, `NonNull`, `VaArg` and `VaArgs` are
     public with their methods.  An adapter is named by the `Iterator`
     method that builds it, so it stays public, with its state private
     and its constructor public, since the trait's default bodies call it
     from the implementing type's module.  `PanicInfo` and a range's
     bounds stay public fields, as plain data.  A hasher's running state,
     a conversion error's reason, a pointer's address in `NonNull` and
     `VaArgs`, and an inclusive range's exhaustion flag are private behind
     accessors, and the unwinding catch's trampoline and its context are
     private.  `taken_panic_info`, the executor's read of a panic its
     `try_catch` caught, is left with `intrinsics`.  (Jake; each split
     delegated)

639. **A path's ancestry is `Path.starts_with`, a descriptor leaves its
     owner by `into_descriptor`, and the JSON parser is private.**
     `Path.starts_with(base)` holds when `base` is the path or one of its
     ancestors, comparing whole components, so `/a/bc` does not start
     with `/a/b`; a `base` that is the path's whole root (`C:` before
     `C:dir`) ends at a component, and the empty path is no one's base.
     `string.is_under` is gone, and the compiler's origin and test
     placement checks ask `Path.starts_with`, the stdlib check included.
     `File`, `ChildStdin`, `ChildStdout` and `ChildStderr` keep their
     descriptor private; `into_descriptor()` hands it to the caller and
     leaves the owner closed, so its drop closes nothing.  A command's
     output capture takes its pipes' descriptors that way and closes them
     itself.  The JSON `Parser`, its `new` and `parse_value` are private
     behind `parse`, whose documentation states the nesting limit; the
     limit is a module constant `MAX_DEPTH`, internal because the
     serializer bounds its recursion by it.  (Jake; the constant's
     placement delegated)

640. **The test runner spawns through `Command`.**  It re-executes the
     test binary with `Command` on every target; POSIX captures through a
     pipe with stderr merged into stdout and keeps the child's own
     watchdog, and Windows captures into a temp file opened sharing read,
     write and deletion and enforces the timeout with
     `Child.wait_timeout`.  `wait_timeout(duration)` answers
     `Ok(Some(status))` once the child has exited and `Ok(None)` when the
     time is up, leaving it running.  `Stdio::SameAsStdout` sends stderr
     wherever stdout goes and is refused, `InvalidInput`, on stdin or
     stdout.  `OpenOptions.share(read, write, deletion)` is enforced on
     Windows only, defaulting to read and write sharing; the parameter
     is `deletion` because `delete` is a keyword.  `ExitStatus`'s fields
     are private behind its accessors, and `decode_wstatus` is private
     to POSIX.  (Jake; the variant's and the parameter's names delegated)

641. **`HashMapIter` is internal.**  It is reached only through
     `HashMap::iter`'s opaque `implement Iterator<Pair<K, V>>`, like the
     other adapters, and stays internal rather than private because
     `HashSetIter` holds one.  The compiler holds those iterators as
     `implement Iterator<Pair<K, V>>` locals.  (Jake)

642. **The intrinsics and the primitives are marked.**  Every intrinsic is
     public: they are the language's primitive operations, the
     allocation, bit, atomic, float, trap, unwinding, directory-entry and
     variadic ones alike.  `taken_panic_info` is public beside
     `try_catch`, as the way a caller catching through it reads the
     payload.  The primitives' constants and methods are public, but for
     the digit-table step behind the integers' hex writers
     (`to_hex_buf_with_table`, `to_hex_padded_buf_with_table`), which is
     private.  An intrinsic declaration takes a visibility like any
     function.  (Jake; the hex step delegated)

643. **`hash_str` is gone.**  A string's hash is `core::hash::digest`,
     which `hash_str` only delegated to; the compiler's resolution map and
     the LSP's diagnostic ids call `digest` directly.  `hash_int` stays
     public.  (Jake)

644. **The compiler and the LSP classify bytes and read numbers through the
     standard library.**  An ASCII digit, letter, hex digit or case fold is
     `u8`'s or `char`'s method of that name, a hex digit's value is
     `char.hex_value`, a case-insensitive comparison is
     `Str.equals_case_insensitive`, and a run of digits is read by
     `u64::parse_radix` or `u64::parse_radix_prefix`, or a literal's text
     by `string.parse_int`.  The literal readers that must see an overflow
     across `_` separators (the parser's, sema's, the constant table's)
     keep their own accumulation, over `char.hex_value`.
     Cryo's identifier rule is answered in one place,
     `Lexer::is_identifier_start` and `Lexer::is_identifier_continue`, which
     the highlighter and the LSP ask too.  The lexer's whitespace stays
     space, tab, line feed and carriage return: a vertical tab or form feed
     in source is an unexpected character, so the lexer does not use
     `char.is_whitespace`.  A tuple index too large for a `u64` is no tuple
     index (E0204); it used to wrap.  `IoError::from_windows_error` and
     `IoError::from_wsa` are `match`es on the `syscall` constants.
     (Jake; the identifier rule's home delegated)

645. **The compiler and the LSP split names and join paths through the
     standard library.**  A qualified name's head, leaf, parent and segment
     count are `string.find`, `rfind`, `substr` and `Str.split`;
     `QualifiedName::leaf_of` is the one leaf operation, which bindgen and
     completion use too.  `::` becomes `/` by `string.replace`, a name's
     segments are `string.split_nonempty` or `Str.split`, and a module's
     first segment below another is `Str.split_once`.  A directory and a
     name are joined by `Path.join`, never `"%s/%s"`, so a directory
     ending in a separator gets no second one and a segment carrying its
     own root replaces the directory.  Where a project root of `.` means
     "here", that test stays at the call site: `Path.join` does not drop a
     `.`.  (Jake)

646. **Completion offers what the completing module may name.**  Member
     completion (`x.`, `T::`) and module completion (`M::`, `import M::`)
     judge each item from the module being edited as the compiler judges
     the access: a private item only in its own module, an internal one in
     any module of its package, a public one everywhere.  A field is
     declared where its type is, a method where its declaration is
     written, and a trait's method reaches wherever its trait does; a
     module hidden from the editor hides every path through it.  The
     extended hover still lists every member of a type.  (Jake)

647. **An integer literal is read in one place, through
     `u128::parse_literal`.**  `u128::parse_literal(text, radix)` reads an
     integer literal's digits - digits of `radix`, with a `_` separator
     allowed anywhere among them - and is `None` for no digit, a byte that
     is neither, a value past `u128::MAX`, or a radix outside 2 to 36;
     `u64::parse_literal` is the same and `None` past `u64::MAX` too.
     Neither reads a sign, radix prefix or type suffix.  In the compiler,
     `Lexer::integer_literal_value` reads a literal from its lexeme and the
     suffix the lexer found, its prefix picking the radix; the parser's
     array sizes, enum discriminants, payload patterns and directive
     arguments, sema's width checks, the constant folder, drop insertion's
     indices and codegen's 128-bit constants all ask it, and the readers
     that 644 left on their own accumulation are gone.  The lexer records
     whether a number is a float (it read a fraction or an exponent), so
     `0xE` and `5usize` are integers.  A float literal where only an
     integer stands - an enum discriminant, a directive argument or
     `key = value` value (E0102), a fixed array's size (E0239) - is
     refused; it used to be read as its integer part.  An integer literal
     past `u128::MAX` is E0010 wherever it stands; it used to wrap.  `u8`
     and `char` have `is_uppercase` and `is_lowercase`, for the ASCII
     letters only, which the module loader's snake-case conversion and the
     highlighter use.  (Jake; the name and shape, and the refusals,
     delegated)

648. **A surrogate escape is refused.**  `\u{D800}` to `\u{DFFF}` in a
     string or a character literal is E0006: a surrogate is no Unicode
     scalar value, so no `char` holds one and UTF-8 has no encoding of
     one.  The lexer refuses it where it refuses a malformed octal escape,
     so the escape decoder only ever meets a scalar value, and the
     compiler encodes a string's `\u{...}` through `String.push_char`; its
     own UTF-8 encoder is gone.  An f-string's `\u{...}` is refused the
     same way (654).  (Jake; the code and wording delegated)

649. **The parser runs on a thread with a large stack.**  The parser
     recurses once per level of nesting and refuses past its limit with
     E0111; the limit and E0111 are unchanged.  It runs on a thread of its
     own, made through `thread::Builder` with a 64 MiB stack, reserved
     rather than committed, and the pass joins it before going on, so the
     limit trips long before the stack runs out however large a parser
     frame or a `Token` grows.  On the main thread's default stack it did
     not: eight more bytes in `Token` overflowed the stack on a 400-deep
     expression before E0111 could be reported.  (Jake; the stack size
     delegated)

650. **The type arena hashes through `DefaultHasher`.**  A canonical type
     key's bucket is chosen by folding the kind, the two scalar
     discriminators, each operand and the operand count into a
     `DefaultHasher`; the arena's own FNV-1a mix is gone.  The hash only
     selects a bucket - entry comparison decides identity.  (Jake)

651. **A byte of a `string` has no address.**  `s[i]` and `*s` on a
     `string` are a `char` read from one byte, widened; there is no `char`
     in memory there, and reading through the byte's address as a `char*`
     took the three bytes after it.  So `&s[i]` and `&*s` are refused,
     E0223 as the address of a literal is, and the help names `s + i`, the
     pointer to the byte.  Reading `s[i]` as a value, and a method called
     on it, are unchanged.  (Jake; the code and wording delegated)

652. **A `?` against its operand is the try operator.**  The postfix `?`
     and the ternary's `?` are one token, and both are kept.  A `?` with
     no space or comment between it and the token before it is the try
     operator whatever follows, so `x?[i]`, `x?.y`, `x?.m()`, `x? - y`,
     `x? * y` and `&x?` apply to `x?`.  A spaced `?` is the try operator
     when what follows cannot begin an expression (`x ?;`), as before, and
     the ternary's otherwise.  `?.` is no longer a token of its own: the
     optional-chaining reservation is dropped, so `x?.y` is `(x?).y`.  A
     ternary written against its condition with a name or a literal after
     the `?` (`flag?a:b`) is E0100, reported at the `?` and parsed as the
     ternary.  (Jake: drop `?.`, keep the ternary; the rule delegated)

653. **A receiver is resolved under no expected type.**  The object a
     member is reached on, or a method called on, is resolved with no type
     wanted of it, and the operand of `&` with the pointee or referent of
     the pointer or reference wanted of the `&`, or none.  A `match`
     answers the type wanted of it, and a `?` is one: under the type
     wanted of `(x?).field` it took that type as its own, and under the
     `T*` wanted of `&f()?` the pointer.  (Jake; the code delegated)

654. **`\u{...}` in an f-string is one escape.**  The lexer and the
     f-string's parse both pass over an escape as literal text and over
     `\u{...}` whole, by the span the escape decoder reads, so its braces
     open no hole: `f"\u{41}"` is `A`, where it was `\u` and a hole
     holding `41`, and a name the hex digits spell (`\u{face}` beside a
     local `face`) is not read.  A surrogate there is E0006 as in a string
     literal.  (Jake)

655. **An implementation's parameter that only its trait's arguments
     write is bound by each call.**  In `implement<T, E, F> trait
     Lift<Result<(), E>> for Outcome<T, F> where F: From<E>`, no
     instantiation of `Outcome` binds `E`, and two calls may pass two
     error types.  So each method whose parameters name such a parameter
     is generic over it: a call binds it from its arguments - through the
     type's path, from a generic function, or through a bound on the
     trait - and has a copy of the method per binding.  It was bound once
     per target, to the first trait argument its where-clause found, and
     every call ran that copy.  A parameter a method names only in its
     return, or that no method names, keeps the binding the where-clause
     derives (`A` in `Iterator<A> for TakeIter<I> where I: Iterator<A>`).
     The trait's declaration of the method writes none of these, so the
     method still conforms.  A generic enum's static method with type
     parameters of its own is resolved through its path as a struct's
     is.  (Jake asked for the converting `FromResidual` impl, which has
     this shape; the rule delegated)

656. **An implementation for every type.**  An implementation whose
     target is a parameter of its own, `implement<T> trait From<T> for T`,
     is filed under its trait alone.  A bound on the trait is satisfied by
     it for any type whose arguments it binds - `From<T> for T` gives a
     type `From` of itself and of nothing else - and its where-clause, if
     any, is checked with the parameter bound to that type.  Monomorphization
     places a copy of the implementation on a type the first time a call
     through the trait needs it, in the type's module (a primitive's in the
     trait's), and only when the call passes what the copy's method takes,
     so no copy is emitted unused.  A call through a type's path reaches
     it when the arguments select none of the type's own members of that
     name, its inherent ones and those its implementations deliver: the
     head's method is one more candidate, and an inherent method that
     takes the arguments outranks it.
     Coherence: such a head overlaps every head of its trait, whatever the
     target, that unifies with it once its parameter is bound to the
     other's target - `From<X> for X` and a second head for every type are
     E0308, `From<i8> for i16` is not - and each pair is reported once, at
     the head checked second.  (Jake chose the blanket `From<T> for T` and
     a sane coherence rule; the mechanism delegated)

657. **`?` desugars through `Try` and `FromResidual`.**  `std::core::ops`
     declares `ControlFlow<Break, Continue>` (variants `Continue` and
     `Break`), `Try { type Output; type Residual; static
     from_output(Output) -> This; branch(this) -> ControlFlow<Residual,
     Output>; }` and `FromResidual<R> { static from_residual(R) -> This;
     }`, each a language item.  `x?` is `match (x.branch()) {
     Continue(v) => { v } Break(r) => { return R::from_residual(r); } }`,
     `R` the enclosing function's return type, the call stamped with
     `Try` and the path directed through `FromResidual`.  A `Result<T, E>`
     breaks with `Result<(), E>` and an `Option<T>` with `Option<()>`:
     `never` is no type argument, so the residual is the enum at `()`.
     One implementation, `FromResidual<Result<(), E>> for Result<T, F>
     where F: From<E>`, covers the same error (through `From<T> for T`)
     and a converted one; `Option<T>` takes `Option<()>`.  Which operands
     and return types `?` accepts is 659.  (Jake: the traits, `ControlFlow`'s name and
     visibility, a single converting implementation; the shapes delegated)

658. **A call's bound on a parameter is checked at the trait's
     arguments.**  `where F: From<E>` at a call binding `F` and `E` holds
     only where an implementation of `From` for `F` binds it at `E`
     (`TraitChecker::unmet_parameter_trait`), as a bound on a named type
     already was; asking only whether `F` implements `From` let every
     call through after `From<T> for T`, and one with no conversion
     stopped at the unbound `F::from` (E0900).  A trait written without
     arguments, and any trait on an object, is still asked of the type
     alone.  Head arguments are compared as the types they denote: one a
     head leaves to its parameter's default is that default (`Array<u8>`
     unifies with `[u8]`, `Array<u8, GlobalAlloc>`), and a name the
     compiler wrote with its type already attached is compared as that
     type.  An implementation's where-clause is checked the same way
     (660).  (the regression fix delegated)

659. **`?` applies to any type that implements `Try`.**  The operand's
     type, through references and pointers, must implement the
     language's `Try` (E0234 otherwise), and the enclosing function's
     return type must have an implementation of `FromResidual` whose head
     binds the trait at the operand's `Residual` (E0235 otherwise).  That
     implementation's where-clause is read at the residual: for a
     `Result` into a `Result` it is the error conversion, refused as
     E0214 naming the `From` that is missing; for any other, E0306 naming
     `FromResidual` at the residual.  A return type or residual that
     still names a parameter is judged by the copy that binds it.
     (Jake)

660. **An implementation's where-clause on a parameter is checked at the
     trait's arguments.**  `implement<E, F> trait Lift for Pair<E, F>
     where F: From<E>` holds for `Pair<Low, High>` only where an
     implementation of `From` for `High` binds it at `Low`, as 658 checks
     a call's bound; asking only whether `High` implements `From` let
     every pair through after `From<T> for T`.  The head's parameters are
     bound by the subject's arguments and, when the trait is asked at
     arguments (`W: Convert<Low>`, a `?`'s residual), by the parts of
     those arguments the head writes them in: `E` in `implement<E, F>
     trait Convert<E> for Wrap<F>` is `Low`.  A bound whose arguments
     still name an unbound parameter is left to the copy that binds it.
     (Jake)

661. **A generic body states the bounds an implementation's where-clause
     needs.**  A method call in a body the user wrote, on an
     instantiation that still names the body's parameters, selects a
     trait implementation's method only when the bounds in scope (the
     function's, its implementation's, its type's declaration's) give
     every bound of that implementation's where-clause, read in the
     head's view of the instantiation: a parameter by a bound naming the
     trait at the same arguments (a bound written without arguments gives
     any), a type naming no parameter by an implementation, and an
     instantiation naming one by an implementation whose own clause the
     bounds give.  Otherwise the call is E0358 naming the bound to add;
     every copy of the body binds the method the body selects, so one the
     clause refutes reached codegen unbound (E0900).  An implementation's
     copy of a trait default and a body the compiler made are checked
     where they are written.  Measured before the change: no body in the
     standard library, the compiler, the tests or the examples relied on
     the gap.  (Jake)

662. **No `ffi::cstr`; a chunked request is 501.**  The standard library
     has no `CStr`/`CString`/`NulError`: a C string is a `string`, read
     with `Str::new` and made with `String::with_null` / `as_cstr`, and
     the specification no longer promises the module.  The HTTP/1 server
     answers a first request framed by `Transfer-Encoding`, which it reads
     but does not implement, with 501 Not Implemented; any other first
     request it cannot read stays 400.  (Jake)

596. **`Arc`'s raw pointer is a pointer to its value.**  `into_raw`
     returns a `T*` to the value inside the shared block, and `from_raw`,
     `increment_strong_count` and `decrement_strong_count` take that
     `T*` and step back to the block, by an offset that rounds past the
     two counts to `T`'s own alignment.  `ArcInner`, the block, is
     private to `alloc::arc`, and the executor holds its tasks through
     `Arc`'s raw API alone.  `Rc` has no raw API, so nothing changes
     there.  `datetime`'s seconds per day, hour and minute are private to
     it, and `Duration` has public `SECS_PER_MINUTE`, `SECS_PER_HOUR` and
     `SECS_PER_DAY` beside `NANOS_PER_SEC`.  The `public`, `internal` and
     `private` keywords on an `extern` block stay the block's default, as
     593 built.  (Jake)

597. **`net::sys` is a public low-level API.**  Its per-OS socket, DNS
     and timeout shims and `ConnectStatus` are public, like `sys`'s; the
     few functions that exist only to implement another one there (the
     Winsock error normaliser, the connect-status probe behind
     `os_connect_status`, the timeout packer) are private.  In
     `net::addr`, `dns` and `http`, plain records keep public fields
     (`IpV4Addr`, `SocketAddr`, `Url`, `Request`, `Response`); a type
     that guards state keeps it private behind its methods
     (`ResolvedAddrs`, `Resolve`, `Router`, `HttpServer`, `StatusCode`);
     parse steps, formatting steps and the URL parser's byte constants
     are private; the limits and defaults a program may name
     (`HTTP_VERSION`, `MAX_BODY`, `DEFAULT_READ_TIMEOUT_SECS`, ...) are
     public.  (Jake for `net::sys`; the rest delegated under 587)

### 2026-10-25

593. **An `extern` block's functions are marked one by one or all at
     once.**  `public`, `internal` or `private` may be written on a
     function or a global inside an `extern` block, and before the block
     itself (`public extern "C" { ... }`), where it is the visibility of
     every function and global in the block that writes none.  One written
     on the function or global decides its own, so a `private function` in
     a `public extern` block is private.  A keyword written on an imported
     global was dropped before, leaving it public.  (Jake; the block's
     keyword as a default its members may override, and the same keywords
     on globals, delegated)

594. **The arena's and pool's default sizes are public; the global
     arena's routing calls are internal.**  `arena::DEFAULT_CHUNK_SIZE`
     and `pool::DEFAULT_SLOTS_PER_BLOCK` are the sizes `Arena::new` and
     `Pool::new` choose, and a program may name them.  `GlobalArena`'s
     `routing`, `take`, `holds` and `grow` are how `GlobalAlloc` serves
     and releases a block from the published arena, and reach the
     standard library alone; `publish` and `set_active` stay public.
     The C bindings of `libc`, `syscall` and `openssl` are each a
     `public extern` block, and the unwind runtime's hooks a `private
     extern` block of `core::panic_unwind`, read only by `catch_unwind`.
     (Jake; the hooks delegated under 587)

595. **A `Duration`, an `Instant` and a `SystemTime` are read through
     their accessors.**  Their seconds and nanoseconds are private fields
     behind `as_secs`, `subsec_nanos`, `as_nanos` and `unix_secs`, so the
     library's own clocks and timers call those; `Duration`'s unit
     constants stay public, and the clocks' conversion constants and the
     calendar algorithm's are private.  A `DateTime`'s fields are its
     public data.  (Delegated under 587)

### 2026-10-24

583. **A value whose type implements a trait by a generic implementation
     converts to a trait object, at the instantiation it has,** which
     answers 425.  The table is the copy of the implementation for that
     instantiation, holding the copies of its supertraits' implementations
     for it, and is emitted with the instantiation's methods, kept once per
     link as they are.  A value of a type parameter still does not convert
     (`E0315`).  A method's object parameter takes a converting argument as
     a function's does.  (Jake; where the table is emitted, delegated)

584. **A `for` loop's update clause is a statement, and every
     control-flow child is walked as one.**  The update is an expression
     statement run at the end of each iteration that reaches the back
     edge; every pass, the async lowering among them, handles it as the
     statement it is, so a value it displaces by assignment and an owning
     result it discards are released there.  A braceless loop body, `if`
     branch or statement arm releases what it displaces or discards
     exactly as it would in braces.  (Jake: the update a statement;
     braceless children, delegated)

585. **How far a module reaches is decided by its package's `module`
     lines, whichever files a build loads.**  A module an `internal
     module` line declares, or that a line declares under such a module,
     is internal to its package even when nothing the program imports
     loads the file holding that line, as when a program imports the
     module directly.  The standard library's internal modules are
     internal by this rule like any dependency's.  (Jake: an internal
     module hides everything in it; the mechanism, delegated)

586. **`cryo raw` is removed,** with the compile mode it ran in; this
     replaces 556.  A file compiled without the standard library is
     `cryo build --no-std`.  (Jake)

587. *The reactor and the heap: replaced by 589.* **The standard library hides only its own plumbing.**  `internal`
     marks what only the library itself uses; a lower-level API a program
     could build on stays public, and that nothing calls an item yet is
     never a reason to hide it.  `collections::raw_buffer` and
     `future::reactor` are public modules: `RawBuffer` and its methods,
     `resize` among them, with its fields private; the reactor's
     `require`, `register`, `cancel`, `register_timer`, `cancel_timer`
     and `INTEREST_READ`/`INTEREST_WRITE`, with `start`, `stop`, `free`
     and `set_current` internal.  `alloc::heap` and the process-spawn
     argument buffers stay internal.  A higher-level collection's own
     grow helpers are private to its module (`Array::reserve_grow` and
     `resize_storage`, `HashMap::resize_buckets`, `Pool`'s `grow`), where
     `Array::resize(new_length, value)` is the user's call.  (Jake;
     `Pool`'s `grow`, delegated)

588. **An HTTP/2 connection codes its header blocks through a
     `HeaderCodec` it is generic over, HPACK by default.**
     `Http2Connection<S, C = HpackCodec>`: `net::http2::codec` holds the
     public `HeaderField`, the trait `HeaderCodec` (`encode_field`,
     `decode`, `limit_encoder_table`) and `HpackCodec`, a public type over
     the internal `hpack` module's encoder and decoder, which stay
     internal with `huffman`.  `new_client` and `new_server` build the
     HPACK connection; `new_client_with_codec` and `new_server_with_codec`
     take any codec.  `build_request` and `build_response` are public again
     and take the decoded fields.  A connection encodes `authorization`,
     `proxy-authorization`, `cookie` and `set-cookie` as sensitive, which
     HPACK writes never-indexed and does not store.  (Jake: a generic
     parameter with a default, and credential headers never indexed; the
     trait's methods and names, delegated)

589. **`future::reactor` is internal, and the heap is public as the
     allocator `NativeHeap`;** this replaces 587 for both.  The reactor
     and everything in it reach the standard library alone, as before
     587.  `alloc::heap` is a public module whose public item is
     `NativeHeap`, a type implementing `Allocator` like `GlobalAlloc`, so
     `Array<T, NativeHeap>` keeps its storage on the mmap-backed heap
     whatever `GlobalAlloc` is backed by; the raw `alloc`, `free` and
     `realloc` stay internal and the segment and lock machinery private.
     (Jake; the name `NativeHeap`, delegated)

590. **E0202's note names only the owners the writer may call.**  A
     bare call nothing binds lists the modules that declare its leaf,
     and a module counts only when its export list offers that function
     to the writing module: not when it is private there, or internal to
     another package or to an internal module, since spelling that owner
     would only trade E0202 for E0353.  (Jake)

591. **A method's receiver is `This`, never the owner's name.**  `&this`
     and `mut &this` are typed as the type the method is declared on: a
     generic type over its own parameters, an `implement` block's target
     as its head writes it.  The owner's bare name denoted the defaults'
     instantiation when every parameter of the owner has a default, so
     each method of `String<A = GlobalAlloc>` and of `implement<A> trait
     Eq for String<A>` was a method of `String<GlobalAlloc>` alone, and
     `String<NativeHeap>` had none.  (Jake asked for the fix; the rule
     follows from 98's `This` naming the enclosing type)

592. **A `Box` is made only by its constructors, and gives its pointer up
     only through `into_raw`.**  Its pointer and allocator are private
     fields; `Box::from_raw_in(ptr, alloc)` re-wraps a pointer from a box
     on any allocator, as `from_raw` does for `GlobalAlloc`'s, and `leak`,
     which did what `into_raw` does, is gone.  `next_capacity`, the growth
     curve `Array` and `RawBuffer` share, is internal; the allocator's
     platform calls are private to it; `Rc`, `Arc`, `Weak`, `Arena` and
     `Pool` keep their state in private fields.  (Jake; the rest of the
     module's marking, delegated under 587)

### 2026-10-23

575. **Every length, position and size in the standard library is an
     `i64`.**  `length()`, `capacity()`, `find`, `index_of` and
     `Iterator::count` answer one, and `get`, `set`, `insert`, `remove`,
     `sub_bytes`, `subslice`, `truncate`, `reserve`, `with_capacity`,
     `take`, `skip` and their kind take one, so a position found by one
     call is passed to the next with no cast.  This answers the question
     ruling 574 left open.  The checking cost of reaching a length through
     its accessor is accepted until the lowering after type checking can
     remove it.  (Jake)

576. **A negative length or position means what the method's kind says,
     never a huge unsigned one.**  A lookup that answers `Option` (`get`,
     `get_ref`, `byte_at`, `at`, `key_at`, `value_at`) answers `None`; a
     method that writes or cuts at a position (`set`, `insert`, `remove`,
     `swap_remove`, `sub_bytes`, `subslice`) panics "out of range";
     `truncate` and `resize` panic "negative length"; `reserve`,
     `reserve_exact` and `with_capacity` treat zero or less as nothing to
     do; `take` of a negative count yields nothing and `skip` of one skips
     nothing.  (Jake)

577. **A brace import records everything the plain import of its path
     does.**  `import std::core::result::{ Result };` binds `result` as
     `import std::core::result;` would, so `result::` paths resolve and a
     path through it, `b::sub::f()`, loads the sub-module it names.  The
     brace line is enough on its own, and a plain line beside it is
     deleted.  (Jake)

578. **A negative byte count, position or width in the I/O layer means
     what the call's kind says.**  A read or seek given a negative count
     or position fails with `InvalidInput`; moving a cursor by a negative
     amount (`Cursor::set_position`, `consume` on a buffered reader)
     panics "out of range"; a width, precision or buffer capacity of zero
     or less writes or buffers nothing extra.  (Jake)

579. **`Cursor`'s length is `length()`, and the hash map's bucket-level
     accessors stay `u64`.**  `Cursor::len` is renamed `length`, as every
     other length in the library is spelled.  `HashMap::bucket_count` and
     `get_bucket` keep `u64`: a bucket index is masked from a `u64` hash,
     and those accessors are internals of the bucket layer rather than
     positions a caller computes.  (Jake)

580. **A local of an `async function` is released where its scope ends,
     exactly as in a function that never suspends.**  A local with a
     destructor lives across every `await` between its declaration and
     the end of its scope whether or not it is mentioned again, so it is
     carried in the future; the end of the block, a `break`, `continue`
     or `return` leaving it releases it, latest declared first.
     Reassigning a carried local releases the value it displaces.  (Jake)

581. **An async local is carried only when a suspension falls between
     its declaration and the end of its scope, never only to keep the
     release order.**  At each exit from a scope, the locals the exit's
     state holds on the stack are released first, by a block wrapping
     that state's part of the scope, and then the carried ones; a
     completion `return` releases the carried values after the returned
     value is produced and after the stack locals it leaves.  (Jake)
582. **`Option` has `replace(mut &this, value: T) -> Option<T>`, public
     API and a language item.**  It stores `Some(value)` and returns the
     previous option, the sole owner of what it held.  An async local
     carried in its future is reassigned through it, wherever the
     assignment stands, so the displaced value is released as an
     assignment over a live local releases it.  (Jake)

### 2026-10-22

553. **A test project can set the compiler's environment and arguments,
     and check its stderr apart from its stdout:** `test.json` takes
     `environment` (`NAME=value` entries) and `arguments`, and `expect`
     takes `stderr_contains` / `stderr_excludes`, line prefixes every
     line of a stream must start with, and a file the command writes.
     The logger's behaviour in a real `cryo` run is tested with these,
     not by a script outside the suite.  (Jake)

554. **LLVM refusing to emit an object, or having no target for the
     triple, is a diagnostic, E0647,** carrying LLVM's own reason and
     failing the build, in place of ruling 549's unnumbered `error:`
     line.  A triple with no target is reported once, not once per
     module.  (Jake)

555. **A module's file is named as its directory spells it,** not by
     the spelling that found it: on a filesystem that ignores case,
     `public module Dumper;` finding `dumper.cryo` makes the module's
     spans, panic locations and log lines name `dumper.cryo`.  Which
     files are found does not change.  (Jake)

556. *Replaced by 586.* **`cryo raw` stays as it is.**  (Jake)

557. **A compile-time integer ranges from `i128::MIN` through
     `u128::MAX`:** a `u128` above `i128::MAX` folds, and so does
     arithmetic crossing `i128::MAX` either way; a cast or a declared
     type wraps the value as the running program does.  (Jake)

558. **An enum whose payload is aligned to 16 is laid out as the front
     end sizes it,** its payload at offset 16, in the LLVM type as in
     `sizeof`.  (Delegated)

559. **Every code generation failure fails the build with a diagnostic.**
     What LLVM will not do - write a module's bitcode, emit an object in
     a worker process, or write the object an emit reported as written -
     is E0647 with LLVM's reason; a module whose IR generation fails
     without reporting why is E0900.  An earlier build's object is
     removed before a module is emitted, so an emit that writes nothing
     cannot leave it to be linked, and worker processes refuse a triple
     with no target once, before any IR is generated.  (Jake; the codes
     Delegated)

560. **An enum whose payload is aligned above 16 is laid out as the front
     end sizes it,** its payload at the payload's own alignment and the
     enum aligned to it, in the LLVM type as in `sizeof`: alone, as an
     array's element and as a struct's field.  (Jake)

561. **The linked IR dump beside an executable is written only under
     `--emit-llvm`** (or cryoconfig's `emit_llvm`), the only build that
     writes the per-module `.ll` files it links; a build without it
     spawns no `llvm-link` and logs nothing about one.  (Jake)

562. **`private` holds for every member and item it can be written on.**
     A generic type's private field is refused outside its module when
     it is read, written, set by a literal or destructured, through an
     instance or inside a generic body, and its private methods and
     statics are refused as a concrete type's are.  A private module
     constant or global, a private alias and a private trait are not
     exported: a path or an import naming one from another module is
     refused.  A destructure reads every field it binds, so it is
     refused as a field read is.  The gate asks the declaration the
     access bound, never a member found again by its name.  (Jake)

563. **A trait's methods are reachable only where the trait is.**  A
     private trait's method is refused outside the trait's module even
     when a public type implements it; a trait's methods carry no
     visibility of their own.  (Jake)

564. **A public signature may not name a private type** (E0506), refused
     where the signature is written: a public function's parameters,
     return and bounds; a public type's public fields and methods; a
     public enum's payloads, alias's target, constant's or global's type,
     and trait's methods.  A value of a private type therefore never
     reaches a module that cannot name it, by inference or otherwise;
     `-> implement Trait` is how a function hides its concrete type.  A
     trait implementation's methods take the trait's signature and are
     judged at the trait.  (Jake; the code Delegated)

565. **A public trait's implementation for a public type is a public
     signature:** the trait's arguments in its head and the associated
     types it binds may not name a private type (E0506, the same code).
     The implementation is seen wherever both the trait and the type are,
     and its methods hand out what those name.  A private trait or a
     private type keeps it unrefused.  (Jake)

566. **A destructure of a generic struct's instance reads the fields
     where a field access does,** off the template for an instance not
     yet minted, so it is accepted outside a generic body as inside one.
     (Jake)

567. **`internal` reaches every module of the declaring package,** one
     cryoconfig project, and is written wherever `private` is.  Another
     package naming one is refused as a private name is (E0503 for a
     type, E0353 otherwise); an `export` keeps the reach a declaration was
     declared with.  E0506 compares reaches: a signature may not name a
     type seen by fewer modules than the signature is, so a public one may
     not name an internal type and an internal one may not name a private
     type.  (Jake; the codes Delegated)

568. **Modules are public; `internal module` reaches its package.**  A
     sub-module is declared `module x;`, or several at once as
     `module { a; b; }`; `internal module x;` and `internal module { a; b;
     }` declare ones that reach only the declaring package, and a module
     declared under an internal one reaches no further.  An import or an
     export naming an internal module from another package is refused
     (E0353), and what such a module declares reaches no further than the
     module, however it is marked, so every door that judges an item's
     reach judges it.  (Jake; the code Delegated)

569. **`public module` is refused** (E0117, the removed-form code), once
     the tree and the pinned compiler are written in the new forms: a
     module is public, so the word tells it from nothing.  A signature in
     an internal module is judged at the module's reach, so its public
     members may name its own types.  (Jake)

570. **A trait's method takes `![language]` and no other directive.**  A
     directive above a method in a trait body attaches to that method; any
     but `![language]` is refused (E0151), since nothing applies its effect
     to a trait's method, and one above an associated type is refused
     (E0151) rather than dropped.  `![language("name")]` names one of the
     compiler's items (E0152 otherwise) on a function, a method, a trait
     method, a struct, an enum or a trait (E0151 otherwise).  (Delegated)

571. **The compiler finds its language items by `![language]` alone.**
     The standard library marks each item's declaration; the compiler
     keeps no table of paths, and a declaration not so marked is never an
     item whatever it is spelled.  An f-string's builder calls name their
     items, so nothing is imported for them, no scope is asked for them,
     and their visibility is never in question; the compiler's own sources
     import none of them.  An item declared twice is refused at both
     declarations (E0152), each naming the other, since the order the two
     are met in is not either one's doing.  The async entry point's
     `Executor::new`/`block_on` are still bound by their method names;
     `a <=> b` is ruling 572's.  (Jake; the code and the item names
     Delegated)

572. **`a <=> b` is the language's `Ord::compare`, by identity.**  The
     call the comparison lowers to names the trait claimed as `ord` and
     its method's position, so a type with an inherent `compare` beside its
     `Ord` implementation compares through the trait's, without ambiguity,
     and a type that does not implement `Ord` is refused at the left
     operand (E0306) whatever `compare` methods it has - as `for` requires
     `Iterator`.  (Jake)

573. **Only the standard library declares language items.**  A
     `![language]` on a declaration outside it is refused (E0151) and
     claims nothing, so the refusal is the only one: the standard
     library's declaration of the same item is not reported as a second
     claim.  The directive's shape is still checked.  (Jake; reusing E0151
     rather than a new code Delegated, and parked as a question)

574. **A collection's `length` and `capacity` are read through its
     accessors.**  The fields behind `length()` and `capacity()` - an
     array's, a string's, a slice's, a map's, a buffer's - are private to
     the collection's module (E0353), and the accessors are `![inline]`,
     so a read through one compiles to the field load.  A fixed array's
     `length` is the language's and stays a field.  Whether every
     `length()` returning `i64` takes the positions and sizes passed to
     the collections with it is open.  (Jake)

### 2026-10-21

501. **The class-removal plan is deleted now,** its slices all built and its
     bridge table empty.  (Jake)

502. **`docs/cryo.md` keeps no section on classes:** the section that
     tabled each refused form is deleted, the sections after it are
     renumbered, and every reference to them follows.  User-facing material
     (the reference, the grammar, the guides, the editor grammar, the
     release notes) reads as if Cryo never had classes; the E0117
     refusal, the reserved keywords, the C++ binding generator's handling
     of C++ classes, these rulings and the roadmap's history stay.  (Jake)

503. **The roadmap marks class removal and borrowed trait objects built,**
     and lists what is left for trait objects: owning boxes, upcasting,
     and generic implementations as objects.  (Jake)

504. **Rulings 249, 253, 258, 291, 294, 295 and 303 are superseded by
     ruling 465:** each governed a class feature that no longer exists.
     (Jake)

505. **The E0306 help for an object does not tell the user to implement
     the trait for the object,** since no implementation can be written
     for one: it says to use a type that implements the trait, or an
     object of a trait whose methods a table can hold.  An object whose
     trait does not reach the bound is told the same first half, and that
     an object implements only its own trait and that trait's
     supertraits; the wording was left to the implementer.  (Jake)

506. **The release notes' 1.0.0 entry has a Trait objects bullet,**
     describing them as built.  (Jake)

507. **`legacy/` is left as it is.**  (Jake)

508. **The report-only previous-commit object comparison is removed from
     CI;** local `make verify` remains the comparison tool.  (Jake)

509. **A struct's or union's constants may be written inline in its body
     or in an `implement struct` (or `implement union`) block; both
     work.**  An enum's and a primitive's constants are written in
     `implement` blocks, where their methods are.  (Jake)

510. **`max_value()` and `min_value()` are removed once the integer types
     have `MAX` and `MIN`,** and every caller is moved to the constants,
     before the freeze.  (Jake)

511. **A type's constant follows the rules of a module-level constant:**
     its initializer is folded where it is a constant expression and
     otherwise computed once before `main`, and the constant has an
     address.  (Jake)

512. **The float types' limits are constants** - `EPSILON`, `MAX`, `MIN`,
     `MIN_POSITIVE`, `INFINITY`, `NEG_INFINITY` and `NAN` on `f32` and
     `f64`.  `math::F64_*` and `math::F32_*`, and `math::infinity()`,
     `math::neg_infinity()` and `math::nan()`, are removed and their
     callers moved to the constants.  `math::PI` and the other
     mathematical constants stay in `std::math`.  (Jake)

513. **A constant on a generic type is refused before v1.0;** allowing
     one is additive and comes later.  (Jake)

514. **`const` in a trait or in a trait implementation is refused for
     now, with a clear error,** so that trait constants can be added
     later without changing what any program means.  (Jake)

515. **A constant named like a field of its own type is refused** (E0205),
     as one named like a method, a variant or another constant of the type
     is.  (Jake)

516. **The standard library's move to type constants covers the type
     limits, the per-width digit counts and the clearly per-type
     constants** (such as `Duration`'s unit counts and the json depth),
     not every module constant.  (Jake)

517. **The E0201 negative test for a constant a primitive does not
     declare stays,** over names that remain undeclared, and the
     constants that become declared get positive tests.  (Jake)

518. **Each new refusal the feature brings gets its own error code,**
     with the codes and wordings put to Jake with the change that builds
     them.  (Jake)

519. **`char` has `MIN` and `MAX`; `boolean` has no constants.**  (Jake)

520. **Every integer type has a `BITS: u32` constant.**  (Jake)

521. **A folded struct constant written out of field order, and a float
     literal with `_` separators in a module constant, are fixed first,**
     on the associated-constants branch, before the feature is built.
     (Jake)

522. **A diagnostic's wording needs no approval:** the one proposed is
     used and reported, not asked about.  Error codes are chosen the same
     way; the question is asked only when reusing an existing code versus
     making a new one is genuinely unclear, and otherwise the choice is
     reported.  This adjusts ruling 518.  (Jake)

523. **A type's constant may leave out its type annotation,** as a
     module constant may; its type is then the initializer's.  (Jake)

524. **The constant folder handles the full unsigned 64-bit range,** so
     that `u64::MAX` can size an array and appear in a `static_assert`;
     this is built before the standard library's limits are declared.
     (Jake)

525. **`char` is 32 bits wide:** a Unicode scalar value, `'\0'` to
     `'\u{10FFFF}'`, which is what `char::MIN` and `char::MAX` state.
     The reference says so where it called the width undecided or 8-bit.
     (Jake)

526. **A folded constant is wrapped to its declared type,** so its
     compile-time value is always the value the program reads at run
     time: `const A: u8 = B + 200` with `B` at 100 folds to 44, not 300,
     wherever `A` is used - an array size, a `static_assert`, another
     constant.  (Jake)

527. **The HTTP request limits and the calendar constants stay at module
     level;** they are not moved onto a type.  (Jake)

528. **The associated-constants branch is not merged yet.**  (Jake)

529. **A negated literal compared with a narrower integer takes the
     operand's type, as a positive literal does:** `x != -128` with
     `x: i8` draws no W0005, and is fixed on the associated-constants
     branch.  (Jake)

530. **A reference to a constant compiles to its value, an immediate,
     wherever the value folds,** not to a load from the constant's global.
     The global and its address remain (ruling 511).  (Jake)

531. **`char` has a `BITS: u32` constant, 32,** and the compiler's own
     copy of the width is replaced by it.  (Jake)

532. **An integer literal above `0x10FFFF` given to a `char` is refused
     with an error.**  (Jake)

533. **HTTP/2's "no limit" settings are written as `u32::MAX`.**  (Jake)

534. **`1 << (u64::BITS - 1)` shifts at the type the result needs,** not
     at the shift amount's narrower type, and is fixed on the
     associated-constants branch.  (Jake)

535. **A shift's amount is exempt from W0005:** its type never changes
     the result, so an amount narrower than the shifted value widens
     silently.  The shifted value widening to a wider amount still
     warns.  (Jake)

536. **The constant folder folds `char` literals,** so a `char` constant
     read as a value compiles to an immediate.  (Jake)

537. **128-bit constants fold and compile to immediates,** as narrower
     integers do, within the folder's range.  (Jake)

538. **The compiler logger has named areas** (`driver`, `resolve`, `sema`,
     `codegen`, `lsp` and the rest), each switched on alone.  (Jake)

539. **Every log line names its source place** - file, line and function -
     taken by `![caller_location]` from `location()`.  (Jake)

540. **The logger is internal to the compiler** and the tools that link it
     (the LSP); it is not standard library API.  (Jake)

541. **A log call in cold code may be unguarded; one on a hot path is
     guarded,** so disabled logging costs nothing there.  (Jake)

542. **Logging is off by default.**  (Jake)

543. **`--debug` is a shorthand for turning logging on.**  (Jake)

544. **Every existing trace (`LOG_DEBUG`, `cdebug`, a debug `eprint`) is
     removed or converted to the logger.**  (Jake)

545. **Log output goes to a sink, a trait object (`virtual LogSink*`):**
     stderr by default, or a file.  (Jake)

546. **In the LSP, log output never reaches the editor's stderr channel
     unless logging is explicitly switched on.**  (Jake)

547. **Logging is switched on by an environment variable and by a
     command-line flag, both.**  (Jake)

548. **`--debug` is exactly `--log=debug`:** it switches logging on and
     nothing else.  The AST dump is `--ast`'s, the IR files are
     `--emit-llvm`'s, and diagnostics flush as they would without it.
     cryoconfig's `debug = true` is the same switch, applied beneath
     the environment and the command line: an area `CRYO_LOG` or a flag
     set keeps that level.  (Delegated)

549. **LLVM's own reasons for a failed object emit or target lookup are
     errors on stderr,** not lines on stdout, and not log lines a
     disabled logger would hide.  (Delegated)

550. **The logger has no `async` area:** nothing logs there, and an area
     with no lines is dead code.  (Delegated)

551. **The language server's smoke test clears `CRYO_LOG` and
     `CRYO_LOG_FILE` for the server it starts,** so a developer's own
     logging cannot turn its quiet-stderr check red.  (Delegated)

552. **Under `--emit-llvm`, a module that fails LLVM verification still
     writes its `.ll` file,** so the IR the verifier refused can be read.
     The failure is still reported as an error and the build still fails;
     the refused module is never optimized or emitted.  (Delegated)

### 2026-10-20

465. **The removed class syntax is refused with E0117,** "`<form>` is not
     part of Cryo", with one help line per form: `type class` - declare a
     struct and implement a trait; a base list - hold the shared part in a
     field, its methods in a trait; `virtual` / `override` - declare the
     method in a trait and implement it; `protected:` - `private:` or
     `public:`; a base-constructor initializer - build the shared field in
     the factory; `implement class` - `implement struct`; a constructor - a
     `static new` factory; a destructor - implement `Drop`.  (Jake)

466. **`()` is only the unit value:** it is not a null pointer, and `()`
     used where a pointer is wanted is a type error.  (Jake)

467. **Assigning an array to an array of a different element type is
     refused.**  (Jake)

468. **`This::missing()` in a trait's default body, and `T::missing()` in a
     generic function, are refused at the trait or the function itself,**
     even when nothing implements the trait or calls the function.  (Jake)

469. **Node type names stay unabbreviated** (`ExpressionNode`, not
     `Expr`): names in the compiler are written out in full, as the keyword
     is `implement` and not a shortening of it.  (Jake)

470. **A test of the class feature is deleted with the feature, not ported
     to a struct,** including one that used a class only incidentally; this
     replaces ruling 386's porting of incidental ones.  (Jake)

471. **What the deleted class tests alone covered gets fresh struct
     tests,** written for structs rather than ported: a second explicit
     `.drop()` of one binding refused (E0452), `p as A` with `p: A*`
     refused (E0208), and `new T[n]` taking its block from `Allocate` with
     `delete` running `Drop` before `release`.  (Jake)

472. **The work-in-progress stash of the class deletion is dropped,** its
     content being committed.  (Jake)

473. **The trait-object refusals keep their wording as written:** E0316's
     "`Circle` is not a trait, so `virtual &Circle` is not a trait object"
     and "`virtual &Shape` is a trait object, which an `extern` function
     `f` cannot take or return"; a field read on an object, "no field
     `width` on `virtual &Shape`: a trait object has no fields"; `delete`
     of an object, "`delete` takes a pointer, and `virtual Shape*` is a
     trait object".  (Jake)

474. **E0313's note reads "a trait object is written `virtual &Shape` or
     `virtual Shape*`",** the object type being built.  (Jake)

475. **An object type mangles as a generic over a reserved full name per
     form,** `VirtualRef`, `VirtualMutRef` and `VirtualPtr`, with the trait
     first, as `Optional` and tuples are encoded; no new type prefix
     letter.  (Jake)

476. **A method call on a function- or tuple-typed value, a field read on a
     function-typed one, and `delete` of an operand that is not a pointer
     are refused in sema,** each fixed on its own before calls through
     objects are built.  (Jake)

477. **E0315's wording stays as built:** "`Square` does not implement
     `Shape`, so `&Square` cannot become `virtual &Shape`", and its forms
     for a generic implementation and a type parameter.  (Jake)

478. **The other trait-object messages stay as built:** "`delete` takes a
     pointer, and `i32` is not a pointer"; a borrowed object "does not
     compare"; a `virtual T*` "compares only with `null`".  (Jake)

479. **`virtual mut &T` accepts either reference for now,** as a `mut &T`
     parameter does.  Reference mutability is enforced for every form
     together when `&this` becomes read-only, in the slice and API pass.
     (Jake)

480. **A trait-object table is emitted on demand,** only for an
     implementation some conversion in the build names.  (Jake)

481. **A table's drop word is the full release code for the type,**
     the type's own `Drop::drop` and the release of every field it owns,
     settled before the ABI freezes so that owning boxes after v1.0 need no
     change to the table.  (Jake)

482. **A table demangles as `<vtable for Main::Shape for Main::Circle>`.**
     (Jake)

483. **E0314 keeps its wording,** "`consume` cannot be called through
     `virtual &Shape`", with the reason as a note: the method is static,
     takes `this` by value, has type parameters of its own, or names
     `This` in a parameter or its return.  (Jake)

484. **The two interim E0358 notes stay until their cases are built,** a
     supertrait's method through an object and a call through a reference
     or pointer to an object (`r: &virtual &Shape; r.area()`), both built
     with the supertrait step; each note is removed in the commit that
     makes its case work.  (Jake)

485. **The release function keeps the internal name
     `__cryo.release.<table symbol>`,** outside the mangling grammar, until
     backtrace symbolization lands.  (Jake)

486. **E0315's supertrait form keeps its wording,** "`Square` does not
     implement `Named`, which `Shape` requires, so `&Square` cannot become
     `virtual &Shape`".  (Jake)

487. **The interim E0315 for an object whose trait's supertrait takes type
     arguments stays until trait arguments are built,** and is removed in
     the commit that builds them.  (Jake)

488. **E0307's count of written type arguments agrees in number,** "1 type
     argument is written", at every site that reports it.  (Jake)

489. **Type arguments written on a call of an ordinary method that takes
     none are refused with E0307,** as on a call through an object.  (Jake)

490. **Implementing a subtrait without implementing each of its
     supertraits is refused where the `implement` block is written,** in
     words that agree with E0315's supertrait form.  (Jake)

491. **E0307 for the count of an object's type arguments keeps its
     wording,** "`Iterator` takes 1 type argument in an object type, and 0
     are written", with the note naming the parameters and associated types
     in declaration order.  (Delegated)

492. **E0314 keeps its note for an associated type an object leaves
     unbound,** "its signature names an associated type of `Pick` that
     `virtual &Twice` does not bind".  (Delegated)

493. **Implementing a subtrait without its supertrait keeps code E0312 and
     its wording,** "`Square` does not implement `Base`, which `Shape`
     requires, so `Square` cannot implement `Shape`".  (Delegated)

494. **A shared object `virtual &Bar` satisfies a bound of a trait with a
     `mut &this` method,** as a call of that method through `virtual &Bar`
     is accepted; mutability through a reference is enforced for every
     form together, when `&this` becomes read-only (ruling 479).
     (Delegated)

495. **The two defects that keep `map` and `filter` from being called on a
     receiver known only by its bound in a generic body are fixed first,
     each in its own change,** and trait-object defaults then land with
     `map` and `filter` through an object in their run test.  (Delegated)

496. **The implementation the compiler makes for an object (its defaults
     for `virtual mut &Iterator<i32>`, and so on) has one definition per
     (trait, object type),** registered in the declaration index under the
     trait, as a specialization is under its template.  Its methods are
     placed and linked by the rule every generic instance follows: emitted
     in the module that owns the first named type among the instance's
     arguments, else in the template's module - for an object, the
     trait's, compiled within the build - with `weak_odr` linkage in a
     COMDAT of its own symbol, which names no requesting module.  A copy
     the prebuilt standard library also holds folds into one at link.  The
     same rule, not a new one, so objects add no placement case.
     (Delegated)

497. **The four open bug reports about class features (an override left
     unchecked, a missing table slot, slots matched by name, a delete
     through a base pointer skipping the derived drop) are closed as
     moot:** the features they describe no longer exist.

498. **The bridge table in the class-removal plan is emptied:** the two
     rows for the per-kind node accessors, which were ruled to stay
     (460), and the two rows naming code that is gone are struck, so the
     final audit's check that the table is empty holds.

499. **A generic type written with the wrong number of type arguments
     (`Box<i32, i64>` for `Box<T>`), and a call of a generic method writing
     more type arguments than the method has type parameters, are refused
     with E0307 in its existing wording,** "`Box` has 1 type parameter,
     and 2 type arguments are written".

500. **The E0306 note shown when an object cannot satisfy a bound of its
     own trait names the method that blocks it and why, in full words,**
     in the form of E0314's reason notes; the wording was left to the
     implementer.  (Delegated)

462. **A path through a bounded type parameter naming a member no bound
     declares (`T::missing()` in a generic function that is instantiated)
     is refused with a diagnostic where it is written,** not left for
     code generation to report as an internal error; a method argument
     the body's copies cannot bind is fixed beside it.  (Delegated)

463. **`Allocate` implemented for a generic struct is fixed before the
     class feature is deleted:** `new Cell<i32> { .. }` uses the
     instance's allocator and links.  (Delegated)

464. **`Display` is not adopted in the compiler for now:** it formats
     through C strings, and the compiler keeps its `to_string` methods as
     the plan records.  (Delegated)

460. **The per-kind node projections stay as checked accessors**
     (`ExpressionNode::call_of`, `DeclarationNode::fn_of` and the rest):
     each answers its kind's payload or null, and a read through one is
     not rewritten as a `match` arm.  (Delegated)

461. **`DeclarationNode` and its subclass `ClassDeclNode` stay classes
     until the class feature is deleted,** as ruling 413 has it; they are
     converted when that deletion starts, not before.  (Delegated)

### 2026-10-19

452. **Indexing a call's fixed-size array result (`pair()[1]`) and an
     `Array` whose elements are fixed-size arrays are fixed after the
     expressions finish converting to payloads,** not before: both fail at
     LLVM verification on the parent compiler too, and neither blocks the
     conversion.  (Delegated)

453. **An array literal whose elements are all literals adapts to the type
     another argument fixes, as a single literal does:**
     `count_eq([1, 2], an_i64)` with `count_eq<T>(xs: [T], y: T)` compiles
     with `T = i64`.  Built as its own change after the expressions finish
     converting to payloads.  (Delegated)

454. **A variable a lambda captures is not reported unused, and `await` in a
     function that is not `async` is a diagnostic at the `await` rather than
     an internal error;** both are fixed after the expressions finish
     converting to payloads, beside the two fixed-size-array defects of
     ruling 452.  (Delegated)

455. **`This` names the self type wherever there is one:** `This::member`
     resolves inside a generic struct's own body and inside a trait impl
     as it does inside a non-generic struct's body.  Refusing it there
     (E0203) is a defect, fixed after the expressions finish converting to
     payloads, with the defects of rulings 452 and 454.  (Jake)

456. **The expression codemod (`scripts/expr-kind-codemod.py`) is kept
     through the member-access and call conversions,** and deleted in the
     first change after the call conversion lands.  (Jake)

457. **Code a change leaves dead is deleted in that same change,** even
     when the work is otherwise cut one kind per commit: the call
     conversion deleting `ExprKind::Legacy`, which it orphaned, was right.
     (Jake)

458. **`This::member` inside a trait's own default body is covered by
     ruling 455:** it resolves against the trait's members, and each
     implementation's copy of the default reads `This` as the implementing
     type.  Fixed as a change of its own after the "any node" handle
     lands.  (Delegated)

459. **`await` outside an `async` body keeps E0600 and its wording**
     ("`await` is only allowed in the body of an `async` function or
     method"), the spec table's code for a misplaced `await`.  (Delegated)

### 2026-10-18

438. **A `Location` constant keeps its pointers, and the growth of the
     position-independent Linux pin from relocating them is accepted for
     now.**  Making a `Location` relocation-free (offsets instead of
     pointers, an ABI change for the panic funnel and the check handlers)
     or linking the Linux pin as a fixed-address executable is not decided.
     (Dispatch, delegated)

439. **A `match` arm on a declaration's shape binds the payload in its
     pattern** (`DeclKind::Function(f) =>`) instead of projecting the
     matched node a second time inside the arm; the change compiles every
     program to the same output.  (Dispatch, delegated)

440. **A pointer argument is judged the same way at every kind of call.**
     A free function, a static function, a function reached by its path, a
     generic one and a class constructor refuse a pointer to a type the
     parameter's pointer does not convert from without a cast, as a method
     call does; an `if` or conditional expression with a `null` arm has its
     other arm's type, so a `null` first arm does not let an unrelated
     pointer through.  Existing code the check refuses is fixed first, then
     the check lands, with compile-fail tests proved against the parent.

441. **A function signature sees an alias's target.**  Alias targets are
     recorded at the start of the signature pass, so a parameter or return
     type written as an alias is the type it names (a call through a
     function-type alias, an operator on an alias-typed call result).  Two
     overloads that differ only by an alias and its target have one
     signature and are refused (E0206).

442. **A Linux-hosted cross build to windows-gnu links with clang and lld
     by default,** as the pin does; `CRYO_CC` still overrides the driver.

443. **A thread-local's initializer is static data or it is refused.**
     First, struct literals nested in a global's initializer (in a struct
     or an array) fold into static data too; then a `![thread_local]`
     whose initializer still cannot fold is refused with its own error
     code.

444. **A struct literal through a generic alias keeps the E0233 wording**
     ("cannot build a `K` literal: `K` is a generic type alias, and a
     struct literal through one is not supported").

445. **A struct literal whose type name is refused has the error type,**
     not `void`, so nothing that uses its value reports a second error.

446. **A pointer converts only to a pointer at the same type.**  A pointer
     to a type that merely converts to the wanted pointee (`i32*` for
     `i64*`, `f32*` for `f64*`, `i32**` for `i64**` or `void**`) is
     refused wherever it meets a declared type and at every kind of call,
     a method call and a value bound to a pointer parameter by its address
     included.  What stays implicit is what the language already states:
     the same pointee, `const` added, `void*` on either side, `null`, and a
     class to its base.  Existing code the check refuses is fixed first,
     then the check lands with compile-fail tests proved against the
     parent.

447. **A thread-local whose initializer cannot fold keeps the E0646
     wording** ("thread-local `x` is initialized by a value computed at
     program start").

448. **The libc bindings are aligned with their C headers later,** at low
     priority (`void*` and `char*` where C has them, not `u8*`); recorded
     in the roadmap, not done with the pointer check.

449. **A generic call whose type parameter only an array literal argument
     fixes is inferred from the literal's element type** (`generic_len([true,
     false])` for `generic_len<T>(xs: [T])` binds `T = boolean`), fixed as a
     unit of its own after the value-producing control forms convert, with a
     project the parent compiler fails.

450. **`()` is a value.**  `const unit: () = ();` being refused ("expected
     `()`, found `void*`") is a defect, logged with a repro and fixed only if
     the fix turns out trivial and safe.

451. **A literal is built by one factory,** `ExpressionNode::literal(value,
     kind, span)`, not one per literal kind.

### 2026-10-17

405. **A `return` inside an await-free `unsafe` block or `static match`
     arm of an `async function` compiles,** fixed in its own change ahead
     of the remaining declaration kinds: every walk of the async lowering
     descends into both forms as it does into a block or a `match` arm.
     (Dispatch, delegated)

406. **`intrinsic const` is left as it is, parked for Jake:** reading one
     emits invalid IR, nothing is built on the form, and it is neither
     deleted nor given a value source until he decides.  (Dispatch,
     delegated)

407. **The error code for the removed class syntax (ruling 388) and the
     trait-object symbol spelling (ruling 391) are chosen by whoever
     implements those slices,** and reported.  How verify treats a moved
     project object is likewise the implementer's call if it blocks the
     work, and otherwise stays as it is.  (Dispatch, delegated)

408. **A `break` or `continue` inside an `unsafe` block or a `static
     match` arm that leaves a loop containing an `await` stays refused
     with E0600,** as built; the edge is not lowered.  (Dispatch,
     delegated)

409. **The generic registry records each template's kind as a kind of its
     own** (`TemplateKind`: function, struct, union, class, enum, trait,
     type alias) rather than the declaration's `NodeKind`, converted in a
     change of its own that moves no compiled output, before any type
     declaration converts to a payload.  (Dispatch, delegated)

410. **The cloner's uncopied fields on intrinsics, imports and extern
     blocks are low priority:** how often each of those kinds is cloned is
     counted first; if any is, every field is copied and a test added; if
     none is, that is recorded and the defect stays open.  (Dispatch,
     delegated)

411. **The three places a type alias is not transparent** - calling a
     value typed by a function-type alias, an operator on a call returning
     an alias, and a struct literal through a generic alias - are fixed as
     one change after every declaration kind but classes has converted to
     a payload, not before.  (Dispatch, delegated)

412. **A comparison between unrelated types is refused with E0200:** two
     values of different enum types, or two pointers neither of which
     converts to the other (pointers to unrelated structs among them).
     Built as a change of its own, with a compile-fail test for each.
     (Jake)

413. **Class declarations are not converted to a declaration payload:** the
     class feature is deleted outright, so a class stays a subclass behind
     the declaration bridge until then.  (Jake)

414. **`intrinsic const` is replaced by the declared intrinsic function
     `location() -> Location`** in `std::core::intrinsics`.  A call
     evaluates to its own place - file, line, column and the readable
     qualified name of the enclosing function - and in a
     `![caller_location]` body to its caller's, the same `Location` that
     function is passed.  `FILE` and `LINE` migrate to it and are
     deleted; the `intrinsic const` form is deleted and the parser
     refuses it.  `Location` is read-only to programs: the standard
     library's visibility pass closes construction.  (Jake)

415. **Borrowed trait objects are built after the class feature is
     deleted,** not before it as ruling 384 had it.  The table emission
     copies the few dozen lines of the class vtable code it needs rather
     than sharing code with the class path, which is deleted.  (Jake, on
     the trait-object audit)

416. **A trait object's type is a type kind of its own,** not a pointer or
     reference to a trait.  (Jake, on the trait-object audit)

417. **A method is callable on a trait object only if `This`, the
     concrete type, appears in its signature as the receiver alone.**  Any
     other use of `This` - a parameter, a return, a type argument - makes
     that method unavailable on the object; the trait is still usable as an
     object through its other methods.  (Jake, on the trait-object audit)

418. **Every trait-object table starts with three reserved words: drop,
     size and alignment,** before any method slot, so owning boxes after
     v1.0 need no change to the table's shape.  (Jake, on the trait-object
     audit)

419. **A table holds the supertraits' slots first,** then the trait's own
     methods.  (Jake, on the trait-object audit)

420. **A table is emitted in the module that holds the implementation** of
     the trait for the type.  (Jake, on the trait-object audit)

421. **The trait-object codes and symbol:** E0314, a method unavailable on
     an object; E0315, a conversion to an object whose type does not
     implement the trait, or implements it generically; E0316, `virtual`
     written on something that is not a trait, or an object in an
     `extern` signature.  A table's symbol is `C$vt$<trait>$f<type>`.
     (Jake, on the trait-object audit)

422. **A trait's associated types are bound positionally in an object
     type,** as type arguments in declaration order: `virtual mut
     &Iterator<i32>`.  (Jake, on the trait-object audit)

423. **Printing goes through objects by a non-generic formatting method**
     taking a `virtual mut &FmtWrite` formatter, added in the standard
     library's visibility pass.  (Jake, on the trait-object audit)

424. **A trait that declares two methods with the same name is refused**
     where the trait is declared.  (Jake, on the trait-object audit)

425. **Converting a value whose trait implementation is generic to an
     object is deferred,** and revisited before the language freeze.
     (Jake, on the trait-object audit)

426. **A raw object pointer `virtual T*` accepts `null` and compares only
     with `== null` and `!= null`;** a borrowed object never compares.
     (Jake, on the trait-object audit)

427. **A trait object satisfies its own trait wherever the trait is
     required.**  The compiler makes the implementation: a method without a
     default calls through the table, and a default method is compiled
     with `This` as the object type.  (Jake, on the trait-object audit)

428. **The concrete type behind `implement Trait` in type position is
     hidden:** only the trait's methods are visible through it.  Built in
     the slice and API pass before v1.0.  (Jake, on the trait-object audit)

429. **A function's `-> implement Trait` promise is checked at the
     function itself,** not at its callers.  Built in the slice and API
     pass.  (Jake, on the trait-object audit)

430. **`implement Trait` in a parameter, a field, an alias or a type
     argument is refused with a message of its own** suggesting a bounded
     type parameter or `virtual &Trait`, instead of an unrelated error or
     `void`.  Built in the slice and API pass.  (Jake, on the trait-object
     audit)

431. **`panic` takes a `Location` instead of separate file and line
     arguments,** so the column and the function name reach the panic
     message.  Built as a change of its own with a pin refresh.  (Jake, on
     class-removal session 9's questions)

432. **`Location.function` is the full qualified path of the function**
     (`App::Main::Probe::own`); a lambda, closure or async body is named for
     the function it is written in, a specialization for its generic.
     (Jake, on class-removal session 9's questions)

433. **The unrelated-types refusal (E0200, ruling 412) applies to every
     comparison operator,** not only `==` and `!=`, as built.  (Jake, on
     class-removal session 9's questions)

434. **The Windows pin is linked so that it carries no base relocation
     over a thread-local's offset,** and a gate scans the built binary for
     one so the defect cannot come back silently.  Fixed in the pin build,
     as a change of its own, proven by the gate refusing the old pin and
     passing the fixed one, and by the fixed pin printing a deliberate
     panic's report.  (Jake, on class-removal session 10's questions)

435. **`null` and string literals in a global's initializer fold into
     static data,** so a thread-local initialized by a struct literal holding
     them is not written by a startup constructor on the main thread only.
     Whether a thread-local whose initializer still cannot be folded is
     refused is NOT decided.  (Jake, on class-removal session 10's
     questions)

436. **The runtime panic entry point is named `__cryo_panic` again** (it was
     `__cryo_panic_at` while the old symbol lived beside it), through the
     two-step pin refresh.  (Jake, on class-removal session 10's questions)

437. **The panic report keeps the form `panicked at
     <file>:<line>:<column> in <function>: <msg>`.**  (Jake, on
     class-removal session 10's questions)

### 2026-10-16

379. **A fixed-size array does not convert implicitly to a dynamic `[T]`:**
     `const d: [i32] = buf;` with `buf: [i32; 3]` stays refused with E0200.
     (Dispatch, delegated)

380. **The demangler prints the full `Array<...>` type a `[T]` symbol
     names** - `std::collections::array::Array<i64,
     std::alloc::allocator::GlobalAlloc>` - rather than sugaring it back to
     `[i64]`.  (Dispatch, delegated)

381. **A call-site argument error spells a dynamic array `[T]`;** only
     declaration-site text follows the written spelling (ruling 369).
     (Dispatch, delegated)

382. **An AST node is a header struct of the fields every kind shares plus
     a `kind` enum of pointers to per-kind structs** (`Expr { span, ...,
     kind: ExprKind }`), not a bare enum value as the handle: identity stays
     the address, an absent child stays `null`, and the header is a field.
     (Jake)

383. **Each node struct carries its own small `Allocate` implementation**
     over the AST arena, about 75 of them, rather than factories that
     allocate from the arena directly.  (Jake)

384. **Borrowed trait objects are built inside the class removal:** after
     the compiler is free of classes and before the class feature is
     deleted, reusing the class vtable emission rather than deleting and
     rewriting it.  (Jake)

385. **Declarations and expressions convert kind by kind under a temporary
     bridge variant; patterns and statements convert all at once.**  The
     bridge is temporary in fact: each category ends with its bridge
     deleted, every bridge piece is tracked until it is, and the final
     audit finds none left.  (Jake)

386. **Tests of the class feature are deleted with it; tests that use a
     class incidentally are ported to structs.**  (Jake)

387. **Constructors and destructors written in a struct body are removed
     along with classes;** a struct is built by a `static` factory and
     released by `Drop`.  Their use is measured before they go.  (Jake)

388. **The removed class syntax is refused with one new error code,** with
     a help line per form saying what to write instead.  (Jake)

389. **The visitors' side channels become return values in a commit of
     their own,** after the dispatch change and apart from it.  (Jake)

390. **`TypeKind` is derived from the type's data as a follow-up** to the
     type conversion, not in it.  (Jake)

391. **The trait-object error codes and the vtable symbol's spelling are
     chosen by the implementation** and reported.  (Jake)

392. **The language server's yield hook becomes a `virtual YieldHook*`**
     once trait objects exist, as the compiler's own use of them.  (Jake)

393. **A supertrait's methods are callable directly on a subtrait
     object;** the subtrait's table holds the supertrait's slots.  (Jake)

394. **`protected` is removed** with classes.  (Jake)

395. **`![caller_location]` on a variadic function or method is refused
     (E0151)** where the directive is written.  The hidden location
     follows the written arguments, and a variadic call's written
     arguments have no fixed end.  (Dispatch, delegated)

396. **`delete p` releases `*p` as a scope exit releases a value of the
     pointee type:** its `Drop`, if it has one, then the values its fields
     own; then the storage is returned.  (Dispatch, delegated)

397. **A struct's or union's own methods, static and instance, are written
     inline in its body.**  An `implement` block is for implementing a trait
     on a type, for an enum's methods (an enum body holds only variants),
     and for the cases the language requires one.  A type whose methods
     are split across several files (the parser's `Parser`) keeps
     `implement struct` blocks in the other files, rather than growing one
     huge file; the methods in its own file are inline.  A bound only some
     methods need is a `where` on those methods, which works inline on a
     generic struct.  (Jake)

398. **A `match` whose arms test constants dispatches through one
     `switch`,** built as its own change ahead of deriving `TypeKind`
     (ruling 390), which then follows.  Every object with such a `match`
     moves, so verify is re-baselined on it.  (Jake)

399. **A method's name read as a field is refused by the type check with
     E0204,** with a help line naming the call: "`kind` is a method; call
     it: `t.kind()`".  (Jake)

400. **The stamp checker's planting test switch plants a name of any
     kind,** not only an identifier, and the enum-pattern check has a
     committed test through it.  (Jake)

401. **`??` and `for` accept only the language's `Option`;** a program's
     own enum spelled like it is refused (E0200).  (Jake)

402. **Statements convert as the class-removal plan designs them,** with
     the node split's memory cost (about 3% of the arena) accepted.
     (Jake)

403. **`Type` keeps its stored `kind` field.**  Deriving it from the
     payload (ruling 390) waits until the compiler itself is built with
     LLVM optimisation, which folds a `match` yielding constants into a
     table load; built unoptimised, the derived read measured 5.5% slower
     with `![inline]`.  The roadmap carries the two steps.  (Jake)

404. **A method's name read as a field is fixed by a suggestion showing
     the corrected line** (`t.kind()` under the source line), with no
     separate plain help line.  This settles the wording ruling 399 left
     open.  (Jake)

### 2026-10-15

347. **Fixed-size arrays change syntax with dynamic ones:** `T[N]` is
     written `[T; N]`, so the language has one array syntax.  (Jake)

348. **The postfix array forms are refused once the tree is migrated,**
     with no period in which both are accepted.  (Jake)

349. **Diagnostics, the language server, the demangler and generated
     bindings print array types in the circumfix form,** `[T]` and
     `[T; N]`.  (Jake)

350. **`[T]` means exactly the type `T[]` names today,** so the change is
     pure syntax, provable with no compiled output changed.  Unifying the
     builtin dynamic array with `Array<T>` is a separate step, done before
     the class removal.  (Jake)

351. **`new u8[n]` keeps its spelling:** it is an allocation expression
     yielding a pointer, not an array type.  (Jake)

352. **A postfix array type is refused with "array types are written
     `[T]`: write `[i32]`"** (filled in with the type actually written) and
     an automatic fix, under a code of the existing parser error family
     rather than a new one.  (Jake approved the wording; the code choice was
     delegated)

353. **`[i32, i32]` keeps its message asking for `(T, U)`** once `[` starts
     an array type.  (Jake)

354. **The rulings stay as written records:** an old spelling inside a past
     ruling is not respelled.  (Jake)

355. **The pins are refreshed after a change to what the compiler accepts or
     what it compiles a program to.**  A change only to text the compiler
     prints needs no repin.  (Jake)

356. **The refused postfix array type is E0104** ("expected type"), worded
     as 352 states, with the type actually written filled in, and carries
     its bracketed spelling as an automatic fix.  (Jake)

357. **Code comments that spell an array type postfix are respelled** in
     the brackets once the postfix form is refused.  (Jake)

358. **A struct field passed by value inside an `if` condition is not
     released twice:** the defect is fixed on the branch that changes the
     array syntax, where it was found.  (Jake)

359. **The demangler's output is pinned by a test.**  (Jake)

360. **Trait objects come before v1.0, borrowed only:** `virtual &T`
     (285, 288, 289), right after or folded into the class removal, built
     on the class vtable machinery.  Owning boxes, upcasting and generic
     implementations coerced to an object come after v1.0.  (Jake)

361. **The refused postfix fixed array keeps the wording of 352 exactly:**
     `u8[4]` reads "array types are written `[T]`: write `[u8; 4]`".
     (Jake)

362. **A value moved in a loop's condition, or in any expression the loop
     evaluates again at its back edge, is refused** with the existing
     loop-carried report ("moved inside a loop is used again on the next
     iteration").  (Jake)

363. **`[T]` means exactly `Array<T>` with the default allocator.**  An
     array with another allocator is written `Array<T, A>`; `[T]` never
     names one.  (Jake)

364. **`Array<T>` is indexed through `Index` implementations in the
     standard library,** not by extending the compiler's built-in
     indexing: `xs[i]` on an `Array<T>` is `*(xs.index(i))`.  (Jake)

365. **The `length` field of an array stays readable for now;** whether it
     stays public is decided in the standard library's visibility pass.
     (Jake)

366. **`&[T]` stays a reference to the owning array,** and a borrowed view
     stays `Slice<T>`.  (Jake)

367. **The change that renames the dynamic array's symbols is proved by a
     one-off comparison** of the compiled output with old symbol names
     mapped to new ones; the comparison script is deleted in the next
     commit.  (Jake)

368. **The compiler and the standard library use Cryo's own language
     features wherever one fits** - `Eq`, `Index`, `Drop`, `Allocate` and
     the other traits - rather than an ad-hoc method of the same purpose,
     so that the compiler exercises the whole language in live code.
     (Jake)

369. **Editor and diagnostic text spells a type the way the user wrote
     it:** an annotation written `[i32]` displays as `[i32]`, one written
     `Array<i32>` as `Array<i32>`.  The spelling is remembered per
     annotation, for display only; it never takes part in identity, keys
     or resolution.  A type with no written spelling displays as `[T]`
     when its allocator is the default and as `Array<T, A>` otherwise.
     (Jake)

370. **A function marked `![inline]` is copied into every module that
     calls it,** so the call can be inlined there; `Array`'s `index` and
     the like are marked `![inline]`.  (Jake)

371. **`p[i]` on a pointer to an array is pointer arithmetic,** as on any
     other pointer; indexing does not look through a pointer to an
     array.  (Jake)

372. **An attribute makes a function report its caller's source location
     in a panic,** and the standard library's index bounds check carries
     it, so an out-of-range index reports the `xs[i]` that wrote it.  The
     attribute's name is proposed with the change and confirmed by Jake.
     (Jake)

373. **Emitting only the methods of an instantiated generic that are
     used** is scouted: built if the work is moderate, recorded as an
     accepted cost otherwise.  (Jake)

374. **A fill literal `[v; n]` is refused unless its element type is a
     plain-copy type,** with an error saying so: the one value would
     otherwise be released `n` times.  (Jake)

375. **`Array`'s `length` and `capacity` are `i64`,** the type the
     built-in `[T]`'s `length` had and every array in the tree was written
     against.  The change is confined to `array.cryo` and the code that
     reads those two values.  (Jake)

376. **Taking a `![caller_location]` function as a value is refused,** and
     the refusal is made early enough that the editor and `cryo check`
     report it, not only a build.  (Jake)

377. **A refused fill literal keeps code E0453 and its wording.**  (Jake)

378. **The attribute that reports the caller's location is named
     `![caller_location]`.**  (Jake)

### 2026-10-14

333. **One project's analysis covers every target of its cryoconfig, and
     the target whose view it takes is chosen by the file opened or saved
     last.**  This stays until a real project needs a separate view per
     target.  (Jake)

334. **The editor's identity lookups come before its definition and hover
     rebuild,** and their work starts by measuring that the name layer's
     stamps are filled in when the editor runs the compiler.  (Jake)

335. **Hover on a name a pattern binds labels it a binding** - a match arm
     `Ready` that binds the matched value hovers as `binding Ready: i64`, at
     the arm and at every use - not as a `const`.  (Jake)

336. **The editor's workspace symbol search may match the typed query
     against declaration names.**  It is a text search the user asked for,
     not name resolution: nothing it finds is used as a binding, so it is
     not a door and breaks no rule above.  (Jake)

337. **Find-references builds its index of uses per request,** with no
     cache, until a measurement on the compiler project shows that it needs
     one.  (Jake)

338. **Work on a branch does not run or wait on CI;** the local gates are
     its proof, and CI is watched on `main` only.  (Jake)

339. **The editor's completion goes only through the compiler's lookups.**
     The typed prefix is not matched as text against module names; the
     text search ruling 336 allows stays limited to workspace symbol
     search.  So `import Probe::` with `Probe` only a prefix of real
     modules offers nothing, until the module graph can list a module
     path's children by identity through a door.  (Jake)

340. **The compiler fields `ASTNode.generated` and
     `BuildOptions.yield_hook` keep their names.**  (Jake)

341. **The names inside an import's braces get their positions and the
     definitions they name before the editor's rename is built,** so a
     rename reaches every import of what it renames.  (Jake)

342. **The suggestion for a module that is already imported reads
     "`Probe::A` is already imported; write `A::Item`"** and is not applied
     automatically.  (Jake)

343. **The language server's `serverInfo.version` from the build stamp
     stays on the roadmap;** it needs a matching change in the VS Code
     extension.  (Jake)

344. **Signature help reads the typed text only for its structure** - which
     call the cursor is in, and which argument.  It resolves the callee
     through the compiler's doors in the scope at the cursor, as completion
     resolves a path, and takes the signature from the callee's identity;
     it never matches the callee's text against names.  It shows every
     overload and highlights the one that fits the argument count so far.
     (Jake)

345. **Ruling 329 is amended: the editor analyses the standard library
     once per server process** and reuses it as a frozen base layer, and in
     the editor's mode the passes the editor does not need are skipped.
     Whole-module reuse between saves stays on the roadmap, folded into the
     intermediate representation work with stable identities.  (Jake)

346. **The editor's text prints names relative to the cursor's scope, by
     identity, and leaves out defaulted type arguments.**  Hover, signature
     help, completion details and inlay hints write each definition and
     type in the shortest form that names the same identity from the
     cursor, decided through the doors and compared by identity, never by
     trimming text; a short name that would name something else there, or
     nothing, keeps enough of its path.  A type argument equal to the
     default its template declares is left out.  The expanded hover keeps
     every path in full.  (Dispatch, delegated, at Jake's request)

### 2026-10-13

299. **Before the merge, the scratch directories are deleted:** `.objcmp/`,
     `compiler/build-b2`, `compiler/.selfprobe`, `.cross-check`, `scratch`,
     `dist` and `.verify` - only after an inventory confirms each is
     untracked, regenerable scratch.  (Jake)

300. **The compiler's warning count is reduced before the merge.**  (Jake)

301. **Calling an overloaded generic static method on a non-generic owner
     (`Plain::conv(7, 9)`), which crashes with internal error E0900, is
     fixed before the merge,** like the generic static call of ruling 250.
     (Dispatch, delegated)

302. **E0313's code and wording stay:** "`Shape` is a trait, so `&Shape` is
     not a type", with the `virtual &Shape` note and the
     bounded-type-parameter help.  (Dispatch, delegated)

303. *Superseded by ruling 465.* **Ruling 294 stays as built:** no class with a base class may
     implement `Allocate`.  (Dispatch, delegated)

304. **A bare trait used as a value type (`p: Shape`) is refused with E0313
     where it is written.**  (Dispatch, delegated)

305. **E0458's wording stays:** "a capturing closure cannot be held by a
     variable of function type ...".  (Dispatch, delegated)

306. **The standard library's explicit destructor calls that release a
     field or a match binding inside a destructor are removed;** each field
     is then released when the destructor finishes.  Every site is checked on
     its own, and a site whose release order is observable is kept and
     reported instead.  (Dispatch, delegated)

307. **The explicit-destructor-call warning and the language document say
     what the compiler does for a local:** `x.drop()` on a local stops its
     release at scope exit and stops later use of it.  (Dispatch, delegated)

308. **The editor's purely mechanical warnings are fixed when there is
     room** - an explicit drop of a local becomes `mem::drop(x)`, and a
     literal suffix the declared type already gives is removed - proved as
     the standard library's were.  This is not the editor's rebuild.
     (Dispatch, delegated)

309. **Moving a field of `this` out inside a destructor is refused** - for
     example `mem::drop(this.inner)` in `drop` - because field glue releases
     the field again when the destructor returns.  Full move checking, which
     refuses every move out from behind a reference, waits until after the
     merge.  (Jake)

310. **A free-function call that a generic and a non-generic overload both
     accept is refused with E0154,** the same as a method or a static call;
     the non-generic overload is not chosen silently.  (Jake)

311. **The remaining explicit `.drop()` calls in the standard library and the
     editor are fixed before the merge,** each site checked on its own as
     before.  (Jake)

312. **`Child::wait` and `Child::join` close stdin without moving the field
     out of `this`,** leaving `None` behind, and are fixed together with 309.
     (Jake)

313. **No more fixes before the merge.**  Known bugs are logged in
     `.bug-report/` and fixed on later branches.  (Jake)

314. **`.bug-report/` at the repo root is gitignored.**  Each bug gets its
     own markdown file named `NNNN-short-name.md`
     (`0001-generic-class-allocate-link-error.md`), numbered in order.  A
     file is deleted when its bug is resolved.  (Jake)

315. **Retention, written into `CLAUDE.md` as rules for agents:** anything
     temporary (debug scripts, probes, scratch builds, logs, codemods) has
     a stated deletion point and does not litter the repo; scratch lives
     outside the repo or in an ignored scratch location and is deleted when
     the work ends; a one-off codemod is deleted in the very next commit
     (ruling 268); a bug file is deleted when its bug is fixed; no dead code
     or dead scripts are left behind.  (Jake)

316. **The first project after the merge is the editor's rewrite,** on a
     new branch `lsp-rewrite`, before the type-syntax update and the class
     removal.  (Jake)

317. **The last fix session's five open questions are logged, not acted on
     now:** assignment through a reference releasing nothing, adding
     `mem::replace` / `Option::take`, making W0015 an error, a bare call
     only the generic overload accepts being refused, and static-method
     ties ignoring `where` clauses - as bugs in `.bug-report/` or items in
     `docs/roadmap.md`.  (Dispatch, delegated, per ruling 313)

318. **A vendored library's module is named `vendor::<Name>`,** the path
     its import writes, whatever namespace the generated binding declares.
     `import vendor::RayLib;` names it whole and `RayLib::InitWindow`
     reaches its items, as with any import.  The binding generator and the
     written import are unchanged; the loader names the module it reached
     through a `vendor::` import.  (Jake, choosing this over making the
     generator declare `namespace vendor::<Name>` or changing how a vendor
     import is written)

319. **The Windows release zip ships a Windows stdlib archive, built on the
     Linux release runner** for the windows triple.  The release's wine
     smoke test links with llvm-mingw's `clang.exe`, which the job already
     fetches and selfhost-check's wine arm already uses.  (Jake)

320. **selfhost-check's wine arm is made to run** by putting `LLVM-C.dll`,
     and whatever else it needs, where wine finds it, and is then measured
     again on CI; ruling 270 had the arm measured on CI before its fate is
     decided.  (Jake)

321. *Superseded by ruling 508.* **On CI, the object comparison runs against the previous commit and
     reports its differences without failing the job.**  (Jake)

322. **A compiler flag that stops a build at object files,** so cross-check
     can run on a Linux host, goes on the roadmap; it is not built now.
     (Jake)

323. **The release smoke test stays wine-only for now;** a test of the
     Windows zip on a real Windows host is added before the first tagged
     release.  (Jake)

324. **The editor's behavioural harness grows with each step of its
     rewrite,** and each step's commit shows the previous server failing
     the checks it adds.  (Jake)

325. **The editor's new lookups by identity are named
     `DefTable::span_of`, `DeclarationIndex::declaration_of`,
     `Resolver::scope_at` and `Resolver::visible_bindings`.**  (Jake)

326. **The index of every use of a definition lives in the editor,** built
     from the stamps the name layer writes.  (Jake)

327. **An `ImportDeclNode` records the module its import resolved to.**
     (Jake)

328. **A qualified type name's span covers its whole path,** and every
     negative test whose annotated columns move is re-annotated.  (Jake)

329. **In the editor's first release, module caching is a design only:**
     each save analyses the whole project, and a hook in the compiler lets
     the editor answer from its last analysis while a long one runs.
     (Jake)

330. **The editor gets parsing that continues past an error only after its
     first release,** and only if the last good analysis proves not to be
     enough.  (Jake)

331. **A file outside any project is analysed as its own entry point.**
     (Jake)

332. **The suggestion to import a module that is already imported stops
     being machine-applicable,** and suggests the qualified name instead.
     (Jake)

### 2026-10-12

247. **The compiler's `![allow]`s that named the deleted lint, and the
     door doc-comment markers, are deleted now.**  `![allow]` stays a
     language feature; the editor's allows wait for its rebuild after the
     merge (ruling 184).  (Jake)

248. **`x as boolean` is refused for a float as for an integer:** nothing
     but a `boolean` converts to `boolean`, so a test on a float is
     written `x != 0.0`.  (Jake)

249. *Superseded by ruling 465.* **Allocation goes through a trait.**  `new T(...)` is sugar for an
     `Allocate` trait, as `a[i]` is for `Index` and `*p` for `Deref`.  A
     class that implements `Allocate` gets its memory from it, and its
     subclasses inherit it: the nearest ancestor's implementation wins.
     The trait carries a matching release method, so `delete` hands the
     memory back the same way; for an arena it is typically a no-op.
     `Allocate` replaces the `![arena(f)]` directive entirely, and
     `ASTNode` implements it over the AST arena.  The allocator's
     signature is checked by the type checker, as any trait method's is.
     A context allocator (an allocator scope) may come later; a class's
     own `Allocate` wins over it.  Not built now.  (Jake)

250. **Calling a generic type's generic method with explicit type
     arguments, `Wrap::<i32>::conv::<u8>(7)`, is fixed before the merge,**
     in the spirit of ruling 243: known defects are fixed before the merge,
     as the last tasks.  (Dispatch, delegated)

251. **The ledger (`docs/name-resolution.md`) is retired now,** with its
     status check, its commit hook, `install-hooks`, and the Makefile and
     verify wiring that served only them, as ruling 106 required before
     the merge.  (Jake)

252. **Every trait implementation is checked against its trait before the
     merge:** a missing method, a method the trait does not declare, and a
     parameter, receiver or return type that differs from the trait's are
     each refused, under an error code of their own.  A method the trait
     gives a default body is not missing.  (Jake)

253. *Superseded by ruling 465.* **Casting a class value to a pointer (`av as void*`) is refused;**
     the address is written `&av`.  (Jake)

254. **A type alias reaches its type's static methods:** with `type PI =
     Pl<i32>;`, `PI::plain(8)` is `Pl<i32>::plain(8)`.  Fixed before the
     merge.  (Jake)

255. **A method call with explicit type arguments inside a generic
     function works when the method is declared in an `implement` block,**
     as it does when the method is in the type's body: `w.conv::<u32>(5)`
     inside `function thn<A>(w: Wrap<A>)`.  Fixed before the merge.  (Jake)

256. **`Allocate`'s release method keeps its default body, which does
     nothing.**  (Jake)

257. **A struct may implement `Allocate`;** with no inheritance, only that
     struct's own `new` goes through it.  (Jake)

258. *Superseded by ruling 465.* **Casting a class value up or down its hierarchy stays refused;**
     casting a pointer along the hierarchy is allowed.  (Jake)

259. **The ledger's status rows go with it,** the ones expecting zero
     included; none is ported to another check.  (Jake)

260. **What `import std::future;` brings into scope is settled by what the
     compiler does, checked first, and `docs/cryo.md` is made to match the
     rule** that a plain import binds no names (D7).  (Jake)

261. **The wording of the cast errors stays as it is, and E0307 stays the
     error for too many type arguments.**  (Jake)

262. **E0312, the trait-implementation check, keeps its number and
     wording.**  (Jake)

263. *Superseded by rulings 509, 510 and 517.* **`const a: i32 = i32::MAX;` storing nothing, so the local is read
     uninitialized, is fixed before the merge.**  (Jake)  No type declares
     constants, so the path names no value: it is refused where it is
     written (E0201), as any path naming nothing is; a primitive's bounds
     are its `min_value()` and `max_value()`.

264. **A generic impl method with a parameter typed `&This` is found when
     called,** as it is with the parameter written `&Wrap<T>`.  Fixed
     before the merge.  (Jake)

265. **A generic alias used as a path (`type Stack<T> = Array<T>;
     Stack::<i32>::new()`) is refused with a clear error for now;** full
     support is on the roadmap.  (Jake)

266. **CI is aligned with `make verify`'s gate list first thing after the
     merge.**  Until then the difference is written down in the merge notes
     (`docs/roadmap.md`).  (Jake)

267. **Session scratch under `.objcmp/` is never tracked.**  The files that
     were committed there are untracked and stay on disk.  (Jake)

268. **A one-off codemod is deleted in the commit right after the one that
     ran it.**  The commit that ran it carries the generator; the history
     keeps it from then on.  (Jake)

269. **`tools/CryoLSP/tests/smoke.py` stays, and is wired into the LSP
     check after the merge.**  (Jake)

270. **selfhost-check's wine path is measured on the CI runner after the
     merge, before its fate is decided.**  (Jake)

271. **A project test may compare a program's output against a file,**
     through a `test.json` field named `stdout_file`.  After the merge.
     (Jake)

272. **Right after the merge, `cryo test` prints each suite's count and
     lists every suite,** so the census and roster scripts can retire.
     (Jake)

273. **`scripts/` is restructured into `gates/`, `pin/`, `release/`,
     `toolchain/` and `dev/` after the merge,** not before.  (Jake)

274. **`make install-lsp` stays in the Makefile,** and its help line says
     that it replaces the installed VS Code extension.  (Jake)

275. *Superseded by ruling 510.* **Primitives keep `max_value()` and `min_value()` for now;**
     associated constants such as `i32::MAX` are roadmap work for after the
     merge.  (Jake)

276. **The wordings of the class-value cast error and the generic-alias
     path error stay as they are.**  (Jake)

277. **A plain import reaches a submodule its target re-exports whole:**
     after `import std::future;`, `waker::Waker` names the re-exported
     module's type, as `docs/cryo.md` states.  (Jake)

278. **The release build stamps the static Linux binary it ships.**  (Jake)

279. **`make verify-pin` also checks that each pin's stamped commit is the
     commit its sidecar names.**  (Jake)

280. **On Windows, `make clean` fails, naming what it could not delete,
     when a running program holds a file.**  (Jake)

281. **The Makefile keeps its short list of targets that do not need the
     host triple.**  (Jake)

282. **On Windows, the targets that only refuse (`examples-golden`,
     `valgrind-check`, `verify-freestanding`) do not build the compiler
     first.**  (Jake)

283. **The merge notes stay at the top of `docs/roadmap.md`.**  (Jake)

284. **`.objcmp/` is deleted entirely as the last step before the merge,**
     by Jake.  (Jake)

285. **Trait objects, when they come, are spelled `virtual &Shape` and
     `virtual Shape*`:** a prefix, as `mut &this` is.  There is no `dyn`.
     (Jake)

286. **Trait objects are built after the class removal,** possibly after
     v1.0.  (Jake)

287. **A trait used as a pointer or reference type (`p: &Shape`,
     `p: Shape*` with `Shape` a trait) is refused where it is written.**
     (Jake)

288. **Object safety is per method:** a method that cannot go in a vtable
     is unavailable on the object, and the rest of the trait still works.
     (Jake)

289. **Trait objects start with borrowed pointers only;** owning boxes come
     later.  (Jake)

290. **The class removal converts the AST straight to enums and `match`.**
     (Jake)

291. *Superseded by ruling 465.* **The known class defects are left alone, since classes are going:**
     `override` is not checked, a missing vtable slot is not reported, and
     vtable slots are matched by name.  (Jake)

292. **Before the merge: an overloaded generic static method taken as a
     value is chosen like a function's or refused; type arguments written
     on an alias with no parameters are refused; an `Allocate`
     implementation for an enum is refused.**  `Allocate` on a generic
     class failing to link is left for the roadmap.  (Jake)

293. **A capturing closure stored in a function-typed variable, field or
     return value is refused (E0458),** where it compiled and crashed.
     Supporting it is roadmap work.  (Jake)

294. *Superseded by ruling 465.* **`Allocate` on a class whose base class gets its memory from another
     allocator is refused,** so `delete` through a base pointer never
     releases through an allocator that did not hand out the memory.
     (Jake)

295. *Superseded by ruling 465.* **`delete` through a base-class pointer runs the static type's `Drop`,
     not the derived class's.**  The spec says so; it is not fixed, since
     classes are going.  (Jake)

296. **The merge into `main` is a `--no-ff` merge commit,** when Jake
     authorizes it.  (Jake)

297. **Old commit messages are left as they are.**  (Jake)

298. **`make verify --baseline` against a commit from before the version
     stamp, `main` included, is fixed after the merge.**  (Jake)

### 2026-10-11

227. **The cleanup that removes the Python gates also removes
     `--emit=facts`, the compiler-internal `![allow]`s and the internal
     lint layer they serve.**  The structural locks stay: private fields,
     typed keys and `DeclName`.  Recorded only; a later change plans and
     does it.  (Dispatch, delegated)

228. **The check that follows what an override answers to a call made
     through the base lands, and the lane gate's pinned `LOOKUP_OTHER`
     rises from 35 to 36 for its one `member-function` door call.**
     (Dispatch, delegated)

229. **`LangModule::is_written` keeps the `lang-item` door id,** as the
     fixed tables ruling 225 names do.  (Dispatch, delegated)

230. **Name resolution is called complete only after the two known holes
     are closed** - what an override answers through the base, and
     `GenericRegistry::annotation_unifies` comparing a definition's path
     as text - **and a final read-only audit passes.**
     (Dispatch, delegated)

231. **Five defects the final audit found are fixed before any slice of
     the cleanup ruling 227 names:** overloaded `async` functions all
     calling one body; `a[i]` and a `Deref` field read finding their
     method by name rather than through their trait; a string `+`
     finding `append` by name in code generation; the `printf` format
     check deciding by the name a call is written with; and the type
     objects' path held as open text.  Sealing that path is the
     structural replacement for the identity-text lint once the lint
     is removed.  (Dispatch, delegated)

232. **An enum converts implicitly to `boolean` only when it is a simple
     enum:** no payload on any variant and no type parameters.  A variant
     whose type arguments cannot be inferred is refused in the type
     check, not left for code generation.  (Jake)

233. **A string literal keeps all its bytes and its length end to end,**
     so `"a\0b"` is three bytes.  (Jake)

234. **The link-symbol rename that puts a trait's arguments into its
     methods' symbols stays.**  (Jake)

235. **Name resolution is called complete** now that the final audit's
     fixes and the type-path seal have landed.  (Jake)

236. **The self-checks are deleted with the lint layer:** E0157 (its
     number stays reserved), the typed-key lint, and the door-id and
     store-key lints.  The E0900 stamp check is not a lint and stays
     always on, with no flag.  The wording question about the refused
     link-symbol message is moot.  (Jake)

237. **AST classes are routed into their arena by a marker on the AST
     base class,** not by the `compiler::` path, and no cryoconfig flag is
     added.  After this nothing treats the `compiler::` namespace
     specially.  (Jake)

238. **The door doc-comment markers are deleted along with the
     allows.**  (Jake)

239. **The 15 test projects that exist only for the lints are deleted
     along with the lints.**  (Jake)

240. **`\a \b \f \v` are supported in string literals as well as
     characters,** and the language spec documents them.  (Jake)

241. **The language spec documents `\u{...}` escapes,** noting that what a
     `char` holds above 255 depends on the open decision on `char`'s
     width.  (Jake)

Noted with no ruling: the identity-text check does not follow a number
taken from `.spelling().id` used as a key.  It is left as it is, since the
check is being deleted and the seals are what hold.  (Jake)

242. **Enums are strict: no enum ever converts implicitly to `boolean`,**
     simple or not.  A test on one is written `c == Color::Red`.  This
     amends the conversion half of 232; its other half stands - a variant
     whose type arguments cannot be inferred is refused in the type check
     (E0307).  (Jake)

243. **Five known defects are fixed before the merge, as the last tasks
     after the cleanup:** an overloaded generic used as a value takes the
     first overload; `p as SomeClass` passes the type check and then fails
     in LLVM; arrays of function values do not work and
     `((i32) -> i32)[]` does not parse; a generic method cannot be used as
     a value; and the closure synthesizer's leftover inherent `drop`
     (ruling 78).  Recorded only.  (Jake)

244. **`const n = Option::None;`, with no type arguments determinable
     anywhere, is refused** (the E0307 family).  (Jake)

245. **A variant's payload is checked against the type arguments written
     on it:** `Result::<i32, i32>::Ok("x")` and
     `Result::Ok::<i32, i32>("x")` are refused.  (Jake)

246. **Booleans are strict.**  An `if`, `while` or loop condition, the
     operands of `&&`, `||` and `!`, and anything assigned or passed to a
     `boolean` must be exactly `boolean`.  No integer, `char`, pointer,
     enum, `Option` or `Result` is ever truthy.  `boolean as` an integer
     type stays allowed (0 or 1); an integer `as boolean` is refused
     (write `n != 0`); a pointer needs an explicit null comparison.  The
     type checker refuses each case with a clear error rather than leaving
     it to fail in LLVM.  (Jake)

### 2026-10-10

211. **Before `docs/name-resolution.md` is deleted, its decisions D1-D77
     and its open-issues list fold into this appendix, and the plans for
     after the merge go into a new `docs/roadmap.md`.**  A cleanup of its
     own, after the code work.

212. **Codegen's table of local values is keyed by each local's binding
     identity, not by its name (rule two).**  Registering a drop flag that
     outlives its block (`ValueTable::set_persistent`) then needs no
     allow.

213. **`TraitDeclNode::lookup_method`, which only the editor calls, stays
     in the compiler with a written reason** until the editor's own
     migration after the merge.

214. **`member-export` stays a door kind of its own** (a module's public
     declarations, by the module and leaf, read off its export list).

215. **The module graph holds each file's import list, so the question
     "which modules does this written path name, as this file sees them"
     (`modules_written_as`) is the graph's to answer through its
     `module-by-path` door,** not a match of written paths against module
     text outside the graph.

216. **An expression made only of literals that would silently narrow
     where it is stored - a constant's or a variable's initializer, an
     assignment (`const c: u8 = 200 + 100;`, which wraps to 44) - is
     refused,** with the "would silently truncate" wording a call's
     argument already gets.

217. **A type declared after a function of the same name in one module
     must not go missing from that module's scope while its export list
     holds it.**  A defect: measured, then fixed.

218. **The two refusals' wordings stand:** a `reason` opening `pending:`
     on a compiler allow, and "`<id>` is not a door".

219. **The Python gates (`lane-gate.py`, `spelling-flow.py`) are not
     taught anything new** - a module path's comparison counted as an
     identity's, or any other refinement: they are to be removed.  Where
     the compiler's own check and a Python list disagree only because of
     what the Python cannot see, that is recorded, not fixed in the
     Python.

220. **The refusal of a literal-only value its integer type does not hold
     is E0010,** the code a single out-of-range literal in the same place
     already gets.

221. **The wordings stand:** the narrowing refusal's "this value is 300,
     which `u8` does not hold: it narrows the value and would silently
     truncate; add an explicit `as` cast", and the identity-text
     refusals' "`==` is handed text read off a definition, which is shown
     and never looked up by", its label "computed from a `DeclName`, and
     the operand of a comparison", and the note "the definition's text
     leaves the function that read it here".

222. **Every lookup the Python spelling-flow list sees that the
     compiler's own checks do not is brought under the checks, module
     discovery included:** text derived from a definition that is not
     typed `DeclName` (a type's display name, a link symbol), discovery's
     matching of written module paths and file paths as plain strings, a
     written name handed as plain text to a fixed table, and a duplicate
     check that records a spelling after the comparison that reads it.
     Each real site found is routed through a door or an identity, or
     says why it is no lookup.

223. **A generic's compiled copies are registered and found by the
     template's identity and their type arguments** (rule four), not by a
     key spelled from the template's name.

224. **The wordings of the plain-text spelling refusal stand:** "`f` looks
     up a spelling it is handed as plain text", its label "looked up here,
     outside a door: the operand of a comparison" (or "the subject of a
     `match`", "the key of a hash table"), the note "the spelling leaves
     the function that holds it here, as text", and the identity-text
     refusal's "the subject of a `match`".

225. **A fixed table the language declares nowhere reuses an existing
     door id rather than getting a kind of its own:** the intrinsic table
     (`IntrinsicKind::from_name`) is a `lang-item` door, and the
     source-location pseudo-constants `FILE` and `LINE`
     (`SourceLoc::of_spelling`) are a `primitive` door.

226. **The Python spelling-flow list is deleted once ruling 223 has
     landed.**  The deletion itself belongs to the cleanup that removes
     the Python gates, not to the change that lands ruling 223.

### 2026-10-09

188. **The comparison primitives (`SymbolStr::equals` and the like) carry
     written reasons on their allows**: they are the comparison itself, and
     whether a comparison is a lookup is judged where it is called.  There
     is no door kind for them.

189. **The scope table's own methods (`Scope::find`, `insert`,
     `get_overloads` and the rest) are made private to the resolver's
     module or modules first, and then carry written reasons.**  This is the
     first step of rule three's structural check.

190. **In `A(v) | B(v)` the two `v`s are one binding.**  The name layer
     declares one binding per name across an arm's alternatives.

191. **`QualifiedName::equals`, `starts_with` and `ends_with` are deleted**:
     nothing calls them (ruling 185).

192. **In a string or character literal, `\0` followed by a digit is a
     compile error with a code of its own** (`"\033"` would otherwise be read
     as a NUL and then the digits).  The help points to the hex escape
     (`\x1b`).

193. **`ModuleGraph::register_c_import_module` carries a written reason**:
     the module graph creating the path of a module that only it makes.

194. **An arm's alternatives must bind the same names, with the same types
     and the same mutability**; otherwise the arm is refused, because its
     body could read a name that the alternative which matched never set.

195. **Rule three for names on syntax nodes and type objects is held by the
     spelling lint (E0157), which follows a spelling read off a FIELD** -
     `decls[i].name.equals(&node.name)` - as it follows a parameter today.
     The compiler's own tables still follow ruling 150: private name
     fields, the door in the store's file.  No non-comparable getter type
     is built.

196. **The list of valid doors moves into the compiler**, as a fixed table
     of door ids, so a `/// Door` marker naming an unknown door is refused
     there before the Python that checks it today is deleted.

197. **verify's census and examples scripts stay** until verify itself
     shrinks at the very end.

198. **Ruling 109's last clause (refuse an allow on a function that takes no
     spelling) is superseded by ruling 179** - the lint refuses only real
     lookups - and is not built.

199. **The type-name door (`NameResolver::type_spelling_res`) is split** into
     a scope-side door beside the rib and a name-layer caller.

200. **A duplicate-declaration check - two declared names compared with
     each other - is not a rule-three lookup.**  Each carries a written
     reason.

201. **E0013 (`\0` followed by a digit) and E0407 (an arm's alternatives
     binding differently) are the codes for rulings 192 and 194, with the
     wording they were built with.**  E0013 may yet give way to real octal
     escapes.

202. **Order of work: the generic-body miscompile (a parameter accepted
     where `Option` of it was written), then the method-binding scans,
     then ruling 195's field-following lint.**

203. **Octal escapes replace E0013's refusal (amends 192 and 201).**  In a
     string or character literal, `\` and one to three octal digits is the
     byte they spell, read as C reads it: at most three digits, ending at
     the first non-octal character; `\033` is ESC, `\0` is NUL, `\177` is
     127.  A value above `\377`, and `\8` or `\9`, is an error.

204. **A generic type's own methods are registered with the
     member-function door under the template itself**, so the door
     answers a lookup made inside the generic body (`b.map::<i64>(g)` on
     a `Box<T>`).  There is no second door over a declaration's method
     list.

205. **The four editor sites the field-following lint flags (`this`
     keyword checks) are fixed when the lint lands**, although the editor
     is otherwise left until after the merge.

206. **E0157's wording for a spelling read off a field is accepted**:
     "`f` looks up a spelling read off a field", with the help "ask a
     door, or say why this is no lookup".

207. **E0013's octal-escape wording is accepted**: "`\8` is not an
     escape: an octal escape's digits are `0` to `7`" and "`\400` is out
     of range: an octal escape is one byte, `\0` to `\377`".

208. **The program's entry point is recorded once by the name layer, as
     an identity.**  The places that compared a function's name with
     `"main"` compare its definition with that identity instead.  There is
     no door for it and no written reasons.

209. **E0358's help for a type parameter's unmet bound is accepted**:
     "add `T: Copy` to the bounds in scope here".

210. **An expression made only of integer literals (`8 * 10000 + 64 *
     100`) is converted to the parameter's type the same way a single
     literal is**: `take_u32(5)` compiles, and so does
     `take_u32(8 * 10000 + 64 * 100)`.

### 2026-10-08

171. *(amended)* **The `Text` type is not deleted yet.**  Deleting it now
     would weaken the Python gates' counts of functions that take a name,
     which read a `string` parameter as one; `Text` is how message text drops
     out of those counts.  It is deleted together with those gates, once
     nothing treats a `string` parameter as a name.

172. **The rule-4 self-check honours `![allow(lookup_by_spelling, reason =
     "...")]` for now**, as the rule-2 check does.  The pending allows that
     also exempt a body from it go when ruling 149's conversion deletes
     them.

173. **A shell redirect writes a job's log and nothing else** (`> log`, as
     the long-job pattern in `CLAUDE.md` does).  A source, test, ledger or
     scratch-script file is written with an editor, never through a
     redirect.

174. **A module-level constant whose initializer calls something is
     computed once, before `main`**, as `docs/cryo.md` §3 says.  The
     start-up initialization the compiler has is kept.

175. **Ruling 170 governs loading only.**  A module must be imported to be
     loaded; a module that is already loaded can still be reached by its
     full path without an import of its own.

176. **`const c = Cell(4);` infers the class's type arguments from the
     constructor's arguments**, as a call of a generic function infers its
     own.

177. **The rule-4 refusal's wording is accepted**: "`intern` is handed text
     read off a definition, which is shown and never looked up by", with its
     help line.

178. **The compiler is re-pinned now**, so the allows on functions carrying
     a door marker can be deleted.

179. **The spelling lint (E0157) refuses a spelling only where it reaches a
     lookup outside a door.**  A function that carries a written name toward
     a door - a pass-along helper, a syntax-tree constructor or setter -
     needs no allow, and its pending allow is deleted.  Rule three's
     structural guarantee (a store's name fields private, its doors in the
     store's own file) follows this.

180. **Entering a closure does not forget the enclosing function's
     parameters.**  `return &param` after a closure in the same function is
     judged as it would be with no closure there.

181. **The async lowering's pending allows are converted next**: the locals
     it tracks and renames are keyed by their binding, not their spelling.

182. **The E0157 second label's wording is accepted**: "looked up here,
     outside a door: compared" / "...: the key of a hash table" / "...: the
     subject of a `match`".

183. **The spelling lint keeps counting any comparison or hash-table insert
     of a spelling as a lookup**, at least until rule three's structural
     check lands.

184. **The LSP's pending allows that the lint no longer needs are deleted
     during the LSP's own work after the merge**, not before.

185. **Dead code is deleted, not converted.**  A name-lookup function or
     helper with no live callers is removed - not given an allow, not
     routed through a door.  This holds for everything a change touches.

186. **The dead-code sweep covers the compiler only.**  A standard-library
     function is public API and is not swept, even when nothing in the
     corpus calls it.

187. **Under rule three's structural check (ruling 150), the worker places
     each door with the store it reads most.**  A store and its doors move
     to a file of their own only when one grows egregiously big; file size
     is otherwise not a concern.

### 2026-10-07

160. **The `()` unit type (ruling 103) is built after the merge, before the
     v1.0 freeze.** `docs/cryo.md` already describes it.

161. **The rule-2 self-check reuses E0157 and its allow-with-reason
     marker**, `![allow(lookup_by_spelling, reason = "...")]`; it gets no
     code of its own.

162. **Rulings 4 and 65 are each built with their fallout fixed in the same
     change**: standard-library code or tests that stop compiling are
     corrected alongside, not left for later.

163. **CI is out of scope until after the merge into `main`.**

164. **The rule-2 table-key check applies to the compiler's own modules
     only**: those whose path starts with `compiler`.

165. **A path loads the sub-module it names.** `json::Inner::deep()`, or
     `J::Inner::deep()` through an alias, loads `json::Inner` when nothing
     imports it; the loader does not wait for an import.

166. **The wordings chosen for rulings 4, 65, 113 and the rule-2 check are
     accepted**: E0157 "`S.f` keys a table by `u32`, not by an identity";
     E0205 "... it is imported"; E0154 "`put` is ambiguous: a non-generic and
     a generic method of `Box2` both accept these arguments"; E0244's help
     "rename the item `Json` or the module `A::Json`".

167. **The loader matches a path's head against the file's imports through
     a door of its own**, and the sub-module the path continues into
     through the same door; the text matching the loader gained for ruling
     165 is not left as outstanding lookups.

168. **A plain import carries reachability into the imported module's
     sub-modules**, as an alias does.  With `import A::Toml;`, the path
     `Toml::Sub::f()` loads `A::Toml::Sub` and resolves to `f` in it,
     rather than E0240.

169. **The wording of E0307 is accepted**: "cannot infer `T` for `idf`
     used as a value", labelled "generic, and nothing here says which
     instance this names", with the help "write the type arguments,
     `idf::<...>`, or give the binding a function type".

170. **A fully spelled path that no import leads to loads nothing.**
     `json::Inner::deep()` in a file with no `import json;` does not load
     `json::Inner`; a module must be imported to be visible.

171. *Amended by ruling 171 of 2026-10-08.* **The `Text` type is deleted if
     that is safe.**  It wraps a string and `to_string()` unwraps it, so it
     seals nothing.

149. **Ruling 109 is narrowed: the spelling lint (E0157) is door-only for
     LOOKUPS.**  A declaration's own name - registering it, printing it,
     mangling it, passing it along - is carried by a separate sealed type
     the lint accepts.  The pending allows go by converting each lookup to
     a door and moving the rest onto that type.

150. **The rule-3 self-check is enforced by making each store's name fields
     `private` and moving its door into the store's own file**, so a
     hand-written `.name` loop outside the door is refused.

151. **Rulings 85 and 97 (instances that appear only through substitution)
     come after the merge**, with a test that would catch a miss.

152. **The calls recorded as "owner + name", which code generation looks
     up again, are retired before the merge**, alongside ruling 125's last
     slice.

153. **The editor's (LSP's) by-name lookups are fixed on their own branch
     after the merge.**

154. **A test-only compiler switch gives the rule-1 self-check a committed
     test** that plants an unresolved name in a correct program.

155. **The lookup gate counts only spelling-driven reads**: a call that asks
     by identity (a `DefId`, a `TypeRef`) and hands a name back is not
     counted.

156. **Ruling 73's help names the pointer as written**: `(*q).drop()` gets
     "use `std::core::ptr::drop_in_place(q)`", not a literal `p`.

157. **Store #2's leftover name-keyed instance registration is finished
     together with store #4.**

158. **`plain_opt(Option::Some(3))`, with the parameter `d: Option<T>`,
     compiles**, as it does through a local; the internal error E0900 it
     gave is a correct program refused.

     ```cryo
     function plain_opt<T>(d: Option<T>) -> Option<T> { return d; }
     const p: Option<i32> = plain_opt(Option::Some(3));
     ```

159. **One CI run of this branch happens before the merge**, so its
     surprises show early.

### 2026-10-06

145. **Calling a value that is not a function keeps E0213**, declared as
     `NON_CALLABLE` and used by ruling 142, rather than a new number.

146. **The two type displays rulings 139 and 142 print in diagnostic text
     are accepted as name-keyed reads**: the lane's LOOKUP_ROUTED count
     going from 25 to 27 for them stands.

147. **A call through a local or a field whose type is no function is
     E0213 too**, "expected function, found `i32`", rather than "cannot find
     function" or "no method named".

     ```cryo
     const x: i32 = 3;
     const a: i32 = x(3);     // expected function, found `i32`
     const b: i32 = h.n(4);   // `n: i32` a field, no method `n`: the same
     ```

148. **A projection through a trait resolves through the traits it
     inherits**: `I::Item` under `I: DoubleEnded`, where
     `DoubleEnded` extends `Iterator`, is `Iterator`'s `Item`.  A projection
     nothing declares stays E0203.

     ```cryo
     type trait DoubleEnded : Iterator { next_back(mut &this) -> Option<This::Item>; }
     function last<I>(it: mut &I) -> Option<I::Item> where I: DoubleEnded { .. }
     ```

139. **E0154 on an overloaded function used as a value with no expected
     type keeps its wording and gains a help line showing how to pick one**
     by typing the variable, with the value's own name and one of its
     overloads' signatures.

     ```cryo
     const f = twice;
     // help: give the variable a function type to pick one: `const f: (i32) -> i32 = twice;`
     ```

140. **Ruling 138's refusal covers a field read through any type parameter,
     bounded or not.**

141. **Codegen's `GlobalRegistry` is keyed by `namespace::name` text, not by
     the linker symbol ruling 126 assumed.** It stays as it is for now and
     is re-keyed when the module-globals table is converted.

142. **Calling a value that is not a function is refused with its own code
     and the wording** "expected function, found `i32`".

     ```cryo
     function one() -> i32 { return 1; }
     const a: i32 = one()(3);   // expected function, found `i32`
     ```

143. **A projection whose trait is unknown, or whose trait declares no such
     associated type, is refused where it is written**, rather than kept as a
     projection interned by its spelling.

     ```cryo
     function b<I>(it: &I) -> I::Nope where I: Seq { ... }   // `Seq` declares no `Nope`
     function c<I>(it: &I) -> I::Item { ... }                // no bound of `I` declares `Item`
     ```

144. **No dedicated check after monomorphization for unbound calls.** The
     monomorphizer visits every call in every copy and is the one place an
     unbindable call is reported (E0900); code generation's own refusal is
     an internal assertion.

### 2026-10-05

131. **A synthesizer that lays out a hidden struct records the pins of what
     it builds.**  The async lowering (a future's state struct) and the
     closure synthesizer (a closure's capture struct) give each field access,
     struct-literal field and variant they build its position as they build
     it, since a node built with its type is never walked again.  Where a
     synthesizer only names a type that already exists, the type check
     checks the lowered body as it checks ordinary code.

132. **A generic function's destructure is checked for its shape on the
     generic body**: a field bound twice, a field the type does not have and
     a field left out are reported there, not only in a copy, so a broken
     generic nothing instantiates is refused too.

     ```cryo
     function first<T>(p: Two<T>) -> T {
         const {a, a}: Two<T> = p;   // refused on the generic body: `a` bound twice
         return a;
     }
     ```

133. **`&T == &T` compares the referents in every copy**, a primitive one
     included: equality through a reference is the equality of what it
     refers to, so no copy compares the addresses.

     ```cryo
     function same<T>(a: &T, b: &T) -> boolean where T: Eq { return a == b; }
     same::<i64>(&m, &n)   // true when m == n
     ```

134. **A float's `abs` clears the sign bit, so `-0.0` and a negative NaN
     come back positive, and `fabs` is removed**; its callers use `abs`.

135. **An overloaded function used as a value is the overload the expected
     function type selects.**

     ```cryo
     const f: (i32) -> i32 = Conv::conv;   // the `conv(i32) -> i32` overload
     ```

136. **Ruling 124's refusal keeps E0214.**

137. **`make verify`'s `tests/started-passing` may name a unit test as well
     as a negative test file**, extending ruling 129, so a change that
     deliberately moves a unit test's object is verified in one run.

138. **A field read through a type parameter is refused on the generic
     body**, as ruling 1 refuses a method call: a bound
     gives a parameter methods, never fields, so what a copy's type
     argument declares is not consulted.

     ```cryo
     implement struct Cell<T, A> {
         tagged(&this) -> i32 { return this.tag.n; }    // E0204: no field `n` on type `A`
         tag_ref(&this) -> &A { return &this.tag; }     // fine; `c.tag_ref().n` reads it where `A` is known
     }
     ```

### 2026-10-04

119. **`module_named` is private to `module_graph.cryo`.** Callers outside
     the module graph reach a module through the graph, not through the
     door, so ruling 108's privacy is expressed by ordinary visibility.

120. *Superseded by ruling 620.* **`math::min` and `math::max` take `where T: Ord`**, so their
     comparisons are licensed by the bound under ruling 82.

     ```cryo
     function min<T>(a: T, b: T) -> T where T: Ord { .. }
     ```

121. **The tests that apply an operator to an unbounded type parameter get
     the bound the operator needs**; they do not become negative tests of
     ruling 82's refusal.

122. **Ruling 82's refusal is E0229, with a message naming the type
     parameter and its bounds**, reported on the generic body.

     ```cryo
     function add<T>(a: T, b: T) -> T { return a + b; }   // error[E0229], names `T` and its bounds
     ```

123. *Superseded by ruling 620.* **The standard library's "frozen, no `checked_*`" overflow contract
     exempts `checked_abs`**: it is the one checked operation ruling 112
     names, and the contract says so.

124. **After ruling 82, an unbounded type parameter passed where a concrete
     type is expected is refused.** `math::sqrt` and `math::fabs` then take
     a bound or become per-type functions.

     ```cryo
     function f<T>(x: T) -> f64 { return takes_f64(x); }   // refused: `T` is not `f64`
     ```

125. **A generic function's instances are filed under the template's
     definition**, keyed by that definition plus the type arguments: an
     instance is the template applied to its arguments, never a name.

126. **Codegen's `GlobalRegistry`, keyed by linker symbol, stays as it is**;
     it is outside rule two.

127. **`LangItem::AstArenaAlloc` stays.**

128. **`math::sqrt` and `math::fabs` become methods of each float type**, as
     ruling 112 made `abs` one of each numeric primitive; the generic
     functions are removed.

     ```cryo
     const r: f64 = (2.0).sqrt();
     ```

129. **`make verify`'s `tests/started-passing` may name a negative test
     file whose report changes deliberately**, so a change to a refusal's
     message is verified in one run rather than composed by hand from two.

130. **`This::` stamps the owner's definition directly** (and the owner's
     parameters as its arguments) instead of writing the owner's name into
     the path and resolving it again.

### 2026-10-03

95. *Superseded by ruling 112.* **`math::abs<T>` and `math::clamp<T>` are bounded by the standard
    library's existing ordering and negation traits**: `abs` takes
    `where T: Ord + Neg<T>` and `clamp` takes `where T: Ord`
    (`std::core::cmp::Ord`, `std::core::ops::Neg<Output>`). With the bounds
    written, ruling 82 applies to their bodies.

    ```cryo
    function clamp<T>(value: T, lo: T, hi: T) -> T where T: Ord { .. }
    ```

96. **In a monomorphized copy, a trait-bound call passes its arguments the
    way monomorphization bound the call**: `b` in `a.equals(b)` is
    borrowed, not moved. The second pass's answer is the wrong one. The
    change is `.objcmp/s100/args-a1.patch`, and the 105 objects it moves are
    accepted with that explanation.

97. **Ruling 85's demand comes after only reachable methods are emitted**,
    so emitting is driven by what is reached: an instantiation that appears when
    monomorphization substitutes types is demanded once emitting is driven
    by reachability, so it does not emit methods nothing calls.

98. **A generic type's bare name inside its own body infers its type
    arguments from the call**, as it does outside its body. `This::...` names the enclosing
    type.

    ```cryo
    implement struct Array<T, A> {
        grow(&this) -> .. {
            const a = Array::try_with_capacity_in(n, alloc);  // arguments inferred from the call
            const b = This::try_with_capacity_in(n, alloc);   // the enclosing Array<T, A>
        }
    }
    ```

99. **When the second pass after monomorphization is deleted, the literal
    widths it was wrongly deciding move** (about 50 objects: `r.take(5)`'s
    argument becomes `u64`), and those movers are accepted with that
    explanation.

100. **Ruling 94's help on `append` reads**: "`String` owns resources, so it
     isn't `Copy`, and `append` is only available when `T` is".

101. **A never-instantiated generic function holding an ambiguous call on a
     concrete receiver is an error**, as commit b6770551 made it.

102. **A `for` loop's initializer that does not fit its declared type is
     E0200**, as for any declaration.

     ```cryo
     for (mut j: i32 = s.length(); j > 0; j--) { }   // error[E0200]
     ```

103. **`()` becomes a real unit type**, as a task of its own after the
     merge. Until then the type checker keeps typing it `void*`.

104. **The lane gate's `LOOKUP_OTHER` count going from 45 to 46** for the
     member-function door call that replaces the template registry's
     composed key, **and the spelling-flow check counting a map method's key
     passed as an `arg`** as it counts one passed as a `sarg`, are accepted.

105. **The Python paperwork is removed before the merge**, replaced where
     a guarantee must survive by a check inside the compiler. This amends
     ruling 10, which kept the ledger until name resolution was complete,
     and ruling 18, which kept the scripts until after the merge.

106. **The ledger (`docs/name-resolution.md`) goes before the merge too**,
     with its status check and its commit hook, as the last paperwork
     slice.

107. **"The Python" means the migration paperwork and the test plumbing.**
     General tooling stays: the pin, the self-host check, the release
     scripts and verify.

108. **Rule three outlives the flow tracer by structure**: the methods of
     a name-keyed store that take a spelling are private to the module of
     the door that owns the store, so a spelling reaching the store from
     anywhere else is a visibility error.

109. **The spelling lint (E0157) becomes "only a door may take a
     spelling".** The pending allows are deleted, and an allow on a
     function that takes no spelling is refused.

110. **`--emit=facts` and its readers are deleted.**

111. **`make verify --require-identical` stays until the last
     name-resolution slice has landed**, and then shrinks to a short make
     target.

112. **`abs`, `clamp` and `checked_abs` are per-primitive functions**
     (`i32::abs`, `f64::abs`), not generic functions with trait bounds: no
     trait says "a literal converts to `T`", which their bodies need. With
     them, ruling 82 applies to the standard library. Replaces ruling 95.

     ```cryo
     const a: i32 = (-5 as i32).abs();
     ```

113. **Every braced import of a name that is both an item and a module is
     an error**, aliased or not (`import A::{ B }`, `import A::{ B as C }`),
     settling rulings 57 and 63 together. E0244's help suggests renaming
     one of the two rather than the braced form.

114. **The 148 command-line flag entries stay on the spelling-flow list**
     until the list itself is deleted.

115. **The residue self-test's fixture supplying its own classes is
     accepted**, and so is the conversion of the method return-type table:
     each reader reads the return of the method it selects, and a
     trait-declared method's return comes through the `trait-method` door.

116. **A static call that is refused has no type**: when the selection picks
     no method (two methods tie, or no overload fits the arguments), the call
     is not typed with the return of the method registered last.

     ```cryo
     const v: i32 = Both::go(&b);   // error[E0156], and no second error on `v`
     ```

117. **The function-template placeholders are converted before
     `module_named` is made private**, the order the change converting
     them took.

118. **Two call-resolution defects are fixed on this branch**: a call to
     overloaded generic free functions selects the overload its arguments
     fit, and a generic owner's method returning a bare type parameter,
     called through a path that writes the owner's arguments, has that
     parameter substituted.

     ```cryo
     function pick<T>(a: T) -> T { return a; }
     function pick<T>(a: T, b: T) -> T { return b; }
     const y: i32 = pick(4, 5);             // the two-argument `pick`

     const c: i32 = Box2::<i32>::get(&bx);  // `get` returns `T`, here `i32`
     ```

### 2026-10-02

80. **The internal check after monomorphization names the specialization's
    owner by its short display name** (`SliceIter<u8>`), as in "internal:
    call to `next` in `SliceIter<u8>::for_each` reached codegen with no
    resolved method (written at iter.cryo:74)". The lane gate's ARENA_READ
    count rising by one for that rendering is accepted.

81. **An enum constructor over an unminted argument infers its
    instantiation** under ruling 35: `const o = Option::Some(String::from_string("hi"));`
    compiles with `o: Option<String>`. It was refused with E0200.

82. **An operator the trait bounds do not license is refused on the
    generic template**, before monomorphization, where the bounds are all
    that is known; it is not checked per instance. `a + b` under a bound on a user trait that is
    merely named `Add` is an error in the generic body.

    ```cryo
    type trait Add<R, O> { add(&this, r: R) -> O; }
    function sum<T>(a: T, b: T) -> T where T: Add<T, T> {
        return a + b;   // error: the bound is a user trait, not the language's `+`
    }
    ```

83. **`static match (T)` checks every arm before monomorphization.** A
    single-type arm (`string =>`) narrows `T` to that type inside the arm;
    a multi-type arm (`i32 | i64 =>`) must be valid for each listed type,
    checked once per listed type; the wildcard arm `_` knows only `T`'s
    declared bounds, like ordinary generic code. How many existing sites the
    wildcard rule rejects is measured before it is switched on, and the
    bounds they need are added in the same change.

    ```cryo
    function show<T>(x: T) -> i32 {
        return static match (T) {
            string    => { x.length() as i32 }   // `x: string` here
            i32 | i64 => { x as i32 }            // checked as i32, then as i64
            _         => { 0 }                    // only `T`'s bounds
        };
    }
    ```

84. **The post-monomorphization write probe is a committed compiler
    instrument**, an `--emit=` mode or a debug mode, not a Python script.
    It gates the slices that remove the pass after monomorphization.

85. **Instantiations that come into existence only when monomorphization
    substitutes types into a copy are demanded and emitted**: a type
    appearing in a body counts as a use.

86. **The internal check after monomorphization words a free function's
    unbound call like a method's**: "internal: call to `panic` in ...
    reached codegen with no resolved function (written at mem.cryo:74)" -
    ruling 71's wording, with "function" for "method".

87. **This ends ruling 79.** An ordinary method merely named `drop`, called
    explicitly, gets no field release after it; its fields are released at
    scope exit.

88. **The count of functions that take a name going from 699 to 701**, for
    the `member_entries` and `trait_slot_of` helpers, is accepted.

89. **The lane gate's ARENA_READ count going from 64 to 65** for the
    `--emit=postmono-writes` report, which renders type names into its
    text, is accepted.

90. **The three spelling-flow rows the `--emit=postmono-writes` flag
    adds** (772 to 775: the `--emit` flag's read and the
    `"postmono-writes"` match) are accepted. They are the same shape as
    the existing flag rows, and under ruling 18 the gate is not changed to
    exempt command-line flags.

91. **A call left with no instance after monomorphization is the internal
    E0900**, not the user-facing E0214 ("no overload accepts these
    arguments"). A user-facing error comes from the check before
    monomorphization; a test that reaches this E0900 marks a gap in that
    check, which is closed there.

92. **A method whose `where` clause the receiver's arguments do not meet is
    refused before monomorphization** with the wording a minted receiver
    already gets: "the trait bound `String: Copy` is not satisfied", note
    "method `get` requires `V: Copy`". It replaces "no method named `get`
    found on type ...".

93. **A deliberate wording change in a negative test's expected report is
    accepted as a verify mismatch**, explained in the commit body and the
    ledger entry, as with ruling 19; `scripts/verify.py` is not changed
    (ruling 18). The test's expected output changes in the same commit.

94. **The help on a `Copy`-gated method's bound error is correct for the
    method it names.** "borrow the element in place with `get_ref` instead
    of copying it out with `append`" is wrong advice for `append`.

### 2026-10-01

70. **The pass after monomorphization does not re-walk generic template
    bodies that are never emitted**, once it is measured that no diagnostic
    depends on that walk.

71. **The internal check after monomorphization uses plain wording** that
    names the method, the specialization and the template call site:
    "internal: call to `push` in `String<GlobalAlloc>::from_str` reached
    codegen with no resolved method (written at string.cryo:120)".

72. **The temporary identity-first, by-name fallback for the calls whose
    receiver monomorphization could not type is accepted until the receiver
    fix**, which must remove it before the by-name finders are deleted.

73. **E0358 on `(*p).drop()` gets a help of its own**: "use
    `std::core::ptr::drop_in_place(p)`".

74. **The spelling-flow entry for `IntrinsicKind::from_name` is accepted.**

75. **`CalleePin::intrinsic_kind` may live in the AST file.**

76. **`Array` keeps `where T: Drop` until the compiler's use-after-drop is
    fixed.** That fix is queued.

77. **Inside a generic template, a call keeps the method it was written
    against** (rule 1). `Bx::<i32>{..}.run()` returning 1 is confirmed.

78. **The closure synthesizer's inherent `drop` becomes a real `Drop`
    implementation**, not deleted.

79. *Ended by ruling 87.* **An explicit inherent `x.drop()` that runs field release is accepted
    until the call pin's method node and codegen's `.drop()` name branch are
    removed.**

### 2026-09-30

40. **The explicit-path piece of the monomorphizer plan lands ahead of the
    plan's order**: a pin on `(Trait for Concrete)::m` survives
    substitution, as built before the plan's earlier slices.

41. **A trait method called on a concrete receiver reached through a field
    inside a generic body is specialized now**, not left for the
    monomorphizer's conversion. The receiver's type is re-derived in the
    clone, and only a receiver that is a generic type's specialization is
    left to the placement machinery.

    ```cryo
    type trait Bb { go<W>(&this, w: W) -> i32; }
    type struct S { v: i32; }
    implement trait Bb for S { go<W>(&this, w: W) -> i32 { return 2; } }
    type struct T2 { s: S; }
    function run<W>(t: &T2, w: W) -> i32 { return t.s.go(w); }   // S's go<W>, specialized
    ```

42. **A trait-method door** (`trait-method` in the table above) takes a
    trait's identity and a method's leaf and returns the method's position
    among the trait's declared methods. It is the one place an
    implementation's method is matched to a trait's method by name; the
    implementation's position table is filled through it.

43. **The internal E0900 wording for a disagreement between the slot table
    and the selection by name is approved**: "`m` was selected by name as a
    trait's method, and the method table of the implementation the trait and
    the receiver select answers a different one" (or "no method").

44. **Only `implement trait Drop for X` defines a destructor.** An inherent
    `drop` is not a destructor. This completes ruling 32. Both land with the
    `drop_in_place` intrinsic (ruling 14) and never before it: applied
    alone, `Box` would stop releasing what it holds, silently.

    ```cryo
    type struct H { p: i32*; }
    implement struct H { drop(mut &this) -> void { .. } }            // an ordinary method
    implement trait Drop for H { drop(mut &this) -> void { .. } }    // the destructor
    ```

45. **Ruling 30's suppression stays broad**: any error caused only by an
    unknown trait is suppressed once the unknown trait is reported,
    including in type positions and after E0155 or E0240.

46. **Ruling 26 covers method values.** `const f = IgTn::go;` is the E0156
    ambiguity when a trait implemented for `IgTn` also provides `go`.

    ```cryo
    const f = IgTn::go;                // error[E0156]: both `go`s apply
    const g = (Beta for IgTn)::go;     // Beta's
    ```

47. **A second import bringing in an already-imported name is refused at
    the import line itself**, with an error of its own. The
    code is **E0243**, primary line "`parse` is imported twice", a label
    on the second import ("`parse` is imported again here") and a note on
    the first ("first imported here"), with the help "remove one of the
    imports, or import one under another name with `as`". The code and
    wording were chosen by the worker recording this ruling, as the ruling
    asked; ruling 3's qualified use is unchanged.

    ```cryo
    import Json::{ parse };
    import Toml::{ parse };   // error[E0243]: `parse` is imported twice
    ```

48. **The explicit call of a generic trait method on a generic owner,
    `(BetaG for GgTf<i32>)::go(&e, 7)`, is handled by the monomorphizer's
    conversion** (the plan's slices 6 and 7), not fixed separately. Until
    then it is E0636.

49. **Methods get `DefId`s, registered by the name layer like other
    items.** Code the compiler generates names the methods it calls through
    **method language items** (for example `FuturePoll`, `OptionTake`); it
    never builds a name to look up.

    ```cryo
    // what the async lowering writes at `x.await`, named by identity
    fut.poll(cx)      // LangItem::FuturePoll, not the leaf `poll`
    slot.take()       // LangItem::OptionTake, not the leaf `take`
    ```

50. **Ruling 47's error is approved as E0243**, "`parse` is imported
    twice".

51. **A method with a receiver, named by path, is a function value whose
    type takes the receiver first**. Ruling 46's ambiguity
    (E0156) applies to it when a trait implemented for the owner also
    provides the method.

    ```cryo
    const f = IgTn::go;                // type `(&IgTn) -> i32`
    ```

52. **Cryo gets import aliasing**, on this branch, despite the 2026-09-28
    freeze: an imported item may be bound under another name, in a brace
    entry and in a single-item import. The alias names the same
    definition. E0243's help then suggests it.

    ```cryo
    import Toml::{ parse as toml_parse };
    import Toml::parse as toml_parse;
    ```

53. **After E0243, a bare use binds to the first import with no further
    error**, so one mistake is reported once. This supersedes ruling 3's "the bare use is
    refused".

54. **A run project checks that a qualified path (`Toml::parse(...)`) calls
    that module's function when a single bare import of another module's
    `parse` exists.**

55. **`ResBase::primitive_position` as a third row of the `primitive`
    door is confirmed.**

56. **Types and values are separate namespaces for import collisions.** A
    type `Tag` from one module and a function `Tag` from another, both
    imported, do not collide.

    ```cryo
    import Shapes::{ Tag };     // a type
    import Labels::{ Tag };     // a function: no collision
    ```

57. **`import A::B as C;` where `A` has an item `B` and a module `A::B`
    exists is an ambiguity error**, neither the item nor the module
    winning. The writer uses the braced form for the item or names the
    module by its path. The code is **E0244**, primary line "`A::B` names
    both an item of `A` and a module", a label on the import ("`B` is
    declared in `A`, and `A::B` is a module") and the help "import the item
    with `import A::{ B as C };`, or the module with `import A::B;`". The
    code and wording were chosen by the worker recording this ruling, as
    the ruling asked.

    ```cryo
    import Shapes::Json as J;        // error[E0244]
    import Shapes::{ Json as J };    // the item
    ```

58. **Whole-module aliases work.** `import M as N;` binds `N` to the
    module's identity, and `N::f()`, `N::Type` and the rest resolve in that
    module.

    ```cryo
    import Json as J;
    const n: i32 = J::twice(3);
    ```

59. **`export` accepts `as` on an entry**, as `import` does.

    ```cryo
    export std::fmt::{ printf as print };
    ```

60. **A qualified call to an overloaded module function,
    `Json::conv(1 as i32)`, compiles and calls the overload its arguments
    select**, where it was E0900. Fixed on this branch.

61. **An integer literal argument defaults to `i32` in overload
    selection**, one fixed rule for an unsuffixed literal: with
    `conv(i32)` and `conv(i64)`, `conv(1)` calls `conv(i32)`.

62. **Under E0244's item-and-module tie there is no way to alias the
    module, and that is accepted**: the writer renames one of them.

63. **A braced import `import A::{ B }` where `B` is both an item of `A`
    and a module `A::B` is an error too**, consistent with E0244; neither
    the item nor the module wins.

    ```cryo
    import Shapes::{ Json };   // error, as `import Shapes::Json as J;` is (E0244)
    ```

64. **Brace exports restrict**: `export A::{ a };` exposes `a` and nothing
    else of `A`, as `docs/cryo.md` section 13.5 says. The compiler is
    fixed to match, with the breakage measured first.

65. **A local declaration and an import of the same name in the same
    namespace is E0205.** Neither the local winning silently nor an alias
    being dropped silently is allowed.

    ```cryo
    import M::{ Tag };
    type struct Tag {}        // error[E0205]
    import Json as Loc;
    type struct Loc {}        // error[E0205]
    ```

66. **A path continues through a module alias into its sub-modules.**

    ```cryo
    import Json as J;
    const n: i32 = J::Inner::deep();   // Json::Inner::deep
    ```

67. **The E0214 help line for a free-function overload no signature
    accepts is approved**: "an integer literal is `i32` where the overloads
    differ in its type; write the type you mean with `as`".

68. **A generic function or method used as a value infers its type
    arguments from its use**: `const h = idf;`, `G::pick`, and
    a generic owner's `W::get`. Today `const h = idf;` fails LLVM
    verification.

    ```cryo
    function idf<T>(x: T) -> T { return x; }
    const h: (i32) -> i32 = idf;   // idf::<i32>
    ```

69. **A float literal argument defaults to `f64` in overload selection
    where the overloads differ**, mirroring ruling 61.

### 2026-09-29

1. **A method call on a type parameter with no bound is an error.** Inside
   a generic body a parameter has no methods except those a trait bound
   gives it. The standard library's calls that relied on the
   opposite get bounds.

   ```cryo
   type trait Write { write_some(&this, n: u64) -> u64; }

   type struct Sink<W> {
       w: W;
       push(&this, n: u64) -> u64 { return this.w.write_some(n); }   // error: `W` has no bound
   }

   type struct Sink2<W> where W: Write {
       w: W;
       push(&this, n: u64) -> u64 { return this.w.write_some(n); }   // Write::write_some
   }
   ```

2. **Calling a private function-typed field from another module is
   refused**, as reading it already is.

   ```cryo
   namespace A;
   type struct Hook { private run: (i32) -> i32; }

   namespace B;
   import A::{ Hook };
   function fire(h: &Hook) -> i32 { return h.run(1); }   // error: `run` is private to `A`
   ```

3. **After an import collision is refused, a later qualified use binds to
   the item its qualified path names.** The collision refuses the bare
   name only. *The bare use's refusal is superseded by ruling 53.*

   ```cryo
   import Json::{ parse };
   import Toml::{ parse };              // error: `parse` imported twice
   const a: i32 = parse("1");           // refused: the bare name is the collision
   const b: i32 = Toml::parse("1");     // binds to Toml's `parse`
   ```

4. **When a generic and a non-generic method of the same name both apply
   to a call, the call is ambiguous and is an error.** Neither wins by
   being non-generic.

   ```cryo
   type struct Box2 { v: i32; }
   implement Box2 {
       put(&this, x: i32) -> i32 { return x; }
       put<T>(&this, x: T) -> i32 { return 0; }
   }
   const n: i32 = b.put(1);   // error: both `put`s apply
   ```

5. **The command-line flag lookups are out of scope for the rules.** A
   flag or subcommand string (`"--emit"`, `"build"`) is an option string,
   not a name in a program, so matching it is not a lookup rule three
   governs.

6. **Rulings live in this appendix** once `docs/name-resolution.md` is
   deleted.

7. **Dropping a generic value in place goes through a compiler intrinsic**,
   which emits each type's drop code
   after monomorphization: a user `drop`, the drop glue, or nothing for a
   `Copy` value. It is how the standard library releases a value of an
   unbounded type parameter behind a pointer, instead of calling `.drop()`
   on it.

   ```cryo
   function release<T>(p: T*) -> void {
       (*p).drop();                      // a method call on an unbounded `T`
   }
   function release<T>(p: T*) -> void {
       drop_in_place::<T>(p);            // the intrinsic; `T` needs no bound
   }
   ```

   The intrinsic's name and module are not ruled; `drop_in_place` above is
   illustrative.

8. **The old finish-line decisions are superseded by the five rules**: the
   count finish line, and the four conditions with the decisions that serve
   them.

9. **The 2026-09-28 freeze defers the language-feature work it moved off
   this branch; it does not kill it.** Explicit receivers, primitive
   keywords, the raw subcommand, the editor's no-project fallback, the
   deprecated attribute and reference mutability each become a branch of
   their own after the merge.

10. **The ledger (`docs/name-resolution.md`), its status rows and its commit
    hook stay until name resolution is complete.** The paperwork is removed
    after that, not before.

11. **Method selection takes its candidates through the member-function
    door** (`member-function` in the table above). It gets no finder of its
    own.

12. **The generic template registry is re-keyed by identity (`DefId`)**, as
    a unit of its own. Today a generic type and a generic function of the
    same name collide in it.

13. *Superseded by ruling 22.* **An inherent method wins over a trait
    method**, as `docs/cryo.md` ("Which Method a Call Names") said - whether
    either is generic. Today a non-generic trait method beats a generic
    inherent one; that changes, measured before it does.

    ```cryo
    type trait Show { show(&this, x: i32) -> i32; }
    type struct P { v: i32; }
    implement P { show<T>(&this, x: T) -> i32 { return 1; } }
    implement trait Show for P { show(&this, x: i32) -> i32 { return 2; } }
    const n: i32 = p.show(5);   // the inherent `show`: 1
    ```

14. **The drop-in-place intrinsic is `drop_in_place<T>(p: T*)` in a new
    module `std::core::ptr`**, beside the pointer operations. It does not go in
    `std::core::intrinsics`. This names the intrinsic ruling 7 left
    unnamed.

    ```cryo
    import std::core::ptr;
    function release<T>(p: T*) -> void {
        ptr::drop_in_place::<T>(p);
    }
    ```

15. **`BufStream::drop`'s explicit `this.inner.drop()` is deleted**; the
    field glue releases `inner` after the body. Explicit drop calls already
    warn, and they become an error at the v1.0 freeze.

    ```cryo
    implement<S> Drop for BufStream<S> {
        drop(mut &this) -> void {
            this.inner.drop();   // deleted: `inner` is released by field glue
        }
    }
    ```

16. **A method call on a bounded type parameter whose bounds do not declare
    the method is an error (E0358)**, as it is for an unbounded one.

    ```cryo
    type trait Counter { count(&this) -> u64; }
    function f<T>(x: &T) -> void where T: Counter {
        x.count();     // Counter::count
        x.missing();   // error: no bound of `T` declares `missing`
    }
    ```

17. **The E0358 note and help wording for a call on an unbounded parameter,
    and the lane gate's ARENA_READ count moving from 64 to 65, are
    approved.**

18. **No new permanent Python instruments.** Workers build no new gates,
    self-tests or tracking files; throwaway probes for measuring and
    running the existing scripts are fine. New enforcement goes into the
    compiler, as a lint or an internal check. The existing scripts stay as
    they are until they are removed after the merge.

19. **An object that `make verify --require-identical` reports moved, whose
    functions and bodies are identical but emitted in another order, is
    accepted with a written explanation in the commit message.** The verify
    script is not changed to ignore emission order.

20. **verify does not see a change to the standard library alone, and that
    is accepted**: both of its runs compile against the working tree's
    standard library. A library change that alters behaviour is proven by a
    test project of its own, the way a counting transport proves that a
    `BufStream` releases what it wraps once. verify is not extended.

21. *Superseded by ruling 22 the same day.* **A generic inherent method and
    a non-generic trait method of the same name that both apply to a call
    make the call ambiguous.**

22. *Superseded by ruling 617.* **Whenever an inherent method and a trait method of the same name both
    apply to a call, the call is an ambiguity error**, in every combination
    of generic and non-generic receiver and method. There is no precedence
    rule. The caller writes `(Trait for Owner)::member(args)` or renames one
    of them. This supersedes ruling 13.

    ```cryo
    type trait Beta { go(&this, u: i32) -> i64; }
    type struct IgTn { v: i32; }
    implement trait Beta for IgTn { go(&this, u: i32) -> i64 { return 2; } }
    implement struct IgTn { go<U>(&this, u: U) -> i64 { return 1; } }

    const n: i64 = ig.go(7);                     // error: both `go`s apply
    const m: i64 = (Beta for IgTn)::go(&ig, 7);  // Beta's: 2
    ```

23. **An unknown supertrait, or an unknown trait in a `where` clause, is
    refused where it is written**, with wording that says trait, "cannot
    find trait `Foo` in this scope" - not E0203's "cannot find type". The code is E0242, new for this; an impl head's
    unknown trait, which already said "trait", takes it too.

    ```cryo
    type trait Sub : Missing { go(&this) -> i32; }             // error[E0242]: cannot find trait `Missing` in this scope
    function f<T>(x: &T) -> i32 where T: Nowhere { return 1; } // error[E0242]: cannot find trait `Nowhere` in this scope
    ```

24. *Superseded by ruling 617.* **Ruling 22's ambiguity is E0156**, the error for a name more than one
    trait provides, whose help already writes
    `(Trait for Owner)::member(args)`, with a primary line that names the
    inherent method.

    ```
    error[E0156]: `go` is both an inherent method of `IgTn` and provided by a trait implemented for it
     note: candidate #1: the inherent method `IgTn::go`
     note: candidate #2: `Beta::go`, from `implement trait Beta for IgTn`
     help: neither outranks the other: call the trait's method as `(Beta for IgTn)::go(&ig, 7)`, or rename one of them
    ```

25. **A call written on a bounded type parameter means the bound's method,
    full stop**. Generic code sees only its bounds, so an
    instantiation never makes such a call ambiguous or redirects it to an
    inherent method the instantiated type also has.

    ```cryo
    function same<T>(x: &T, y: &T) -> boolean where T: Eq {
        return x.equals(y);   // Eq::equals, even for a T with an inherent `equals`
    }
    ```

26. *Superseded by ruling 617.* **Ruling 22 covers static paths too.** When a trait implemented for the
    type also provides the member, `IgTn::go(&ig, 7)` is the E0156
    ambiguity, as `ig.go(7)` is; so are `T::m()` for a static method and
    `T::m` as a value. The explicit forms are `(Beta for IgTn)::go(...)` for
    the trait's method, or renaming.

    ```cryo
    const n: i64 = IgTn::go(&ig, 7);              // error[E0156]: both `go`s apply
    const m: i64 = (Beta for IgTn)::go(&ig, 7);   // Beta's: 2
    ```

27. *Superseded by ruling 617.* **The E0156 wording for an inherent method beside a trait's is
    approved**: the primary line "`go` is both an inherent method of `IgTn`
    and provided by a trait implemented for it", the note "candidate #1: the
    inherent method `IgTn::go`", and the help "call the trait's method as
    `(Beta for IgTn)::go(&ig, 7)`, or rename one of them".

28. **E0242 ("cannot find trait") is approved**, including an unknown trait
    in `implement trait X for ...` moving from E0203 to E0242.

29. **The explicit call of a generic trait method, `(BetaG for NgTf)::go(&g,
    7)`, compiles and runs**, since E0156's help sends people to that form.

    ```cryo
    type trait BetaG { go<U>(&this, u: U) -> i64; }
    type struct NgTf { v: i32; }
    implement trait BetaG for NgTf { go<U>(&this, u: U) -> i64 { return 2; } }
    implement struct NgTf { go<U>(&this, u: U) -> i64 { return 1; } }

    const n: i64 = (BetaG for NgTf)::go(&g, 7);   // 2
    ```

30. **Under `where T: Nowhere`, a call does not add E0306 after the E0242**
    the bound already reported. The E0306 is suppressed only when the
    E0242 was actually reported.

    ```cryo
    function f<T>(x: &T) -> i32 where T: Nowhere { return 1; }  // error[E0242]
    const r: i32 = f::<i32>(&a);                               // no second error
    ```

31. **A method two traits provide, called inside one of those traits' impls
    on a concrete receiver, is E0156.** There is no "the impl's own trait
    wins" rule; the call names the trait it means.

    ```cryo
    implement trait Debug for struct String<GlobalAlloc> {
        fmt<W>(&this, f: mut &Formatter<W>) -> Result<(), FmtError> where W: FmtWrite {
            const view: Str = this.as_str();
            return view.fmt(f);                    // error[E0156]: Display::fmt and Debug::fmt
            return (Debug for Str)::fmt(&view, f); // the one meant
        }
    }
    ```

32. **An inherent `drop` does not satisfy `where T: Drop`.** A type
    implements the trait with `implement trait Drop for ...`.

33. **A call on a bounded type parameter records its bound where it is
    written, from every `where` level (the owner's and the method's) and
    from associated-type bounds, by identity**, however the receiver is
    reached (an index, a field, a projection). Such a call is never
    re-selected by name after monomorphization, so ruling 22's ambiguity
    cannot meet it there.

    ```cryo
    type struct Q { v: i32; }
    implement struct Q { equals(&this, o: &Q) -> boolean { return false; } }
    implement trait Eq for struct Q { equals(&this, o: &Q) -> boolean { return this.v == o.v; } }
    a.index_of(&Q { v: 2 })   // Eq::equals inside `index_of` (`where T: Eq`); was E0156
    ```

34. **The crash of a generic overload sharing a name is fixed by
    construction in the monomorphizer's conversion**, not by patching the
    finder that crashes.

35. **A call sema cannot type before monomorphization gets its typing
    completed**; it is not looked up during monomorphization instead.

36. **The async lowering pins the calls it generates itself.**

37. **A call's final pin stays `CalleePin::Decl`**, with an instance table
    in the monomorphizer; there is no new instance pin kind.

38. **A specialization's entry is registered without a name key**, so no
    name can reach an instance.

39. **The post-monomorphization check for an unpinned call (E0900) also
    names the template call site it came from.**

### 2026-09-28

Relayed in plain text. Done means the five rules above; the definition and
the language's features are frozen for this work. `ModuleGraph::module_named`
becomes a door private to the name layer and the module loader. These were
dropped, and are not to be started:

- a type of its own for file paths used as lookup keys;
- command-line flags taking `Keyword`;
- a written exception list for functions that turn raw text into names;
- a pointer to a name (`SymbolStr*`) counting as taking a name;
- writing the reasons for the pending spelling-lint allows;
- the lane gate no longer counting calls that pass only identities.

The pending allows, the residue file and the approved-names file are
retired after the flow conversions, and are left alone until then.

### Earlier decisions (D1-D77)

Decided before the numbered rulings began, and numbered apart from them.
Each is stated as the behaviour it requires.  A decision since replaced,
dropped or deferred says so; a deferred one is listed in
`docs/roadmap.md`.

- **D1.** A name's stamp (`Res`) is authoritative: what the name layer
  bound is what the name means, not a hint a later pass may override.
- **D2.** There is no separate function namespace.  The value namespace
  holds every value-bearing declaration: variables, parameters, constants,
  enum variants, functions, methods and intrinsics.
- **D3, D4.** *Replaced by D5.* Modules binding in the type namespace, and
  `A::B` preferring the type.
- **D5.** Modules and types share one namespace.  In a file's scope a name
  is a module or a type, never both; the pair is a redeclaration (E0205)
  at the declaration or import that completed it.
- **D6.** Intrinsics are namespaced.  A bare name always means a
  declaration in scope, never an intrinsic reached around it; intrinsics
  are reached through `std::core::intrinsics`.
- **D7.** Imports do not glob.  A plain `import M;` binds the module and no
  names from it.  The globs that remain are the written `M::*`, the prelude
  and the formatted-string runtime the compiler imports for a program.
- **D8.** There is no inline `<T: Bound>` form; bounds are written in
  `where`.
- **D9.** Type declarations take `where` between the head and the body,
  and an instantiation whose argument does not satisfy a bound is E0306 at
  the annotation that wrote it.
- **D10.** A leaf that names two declarations is E0155, never a bind in
  directory order - and so is a leaf two children of one facade declare,
  reached by a qualified path, in a call as in an annotation.
- **D11.** The resolution counter and its audit streams are removed from
  the compiler entirely.
- **D12.** *Ended by the five rules.* A public name-keyed lookup guarded by
  a gate instead of privacy.
- **D13.** The parser records a `new` path whole; resolution classifies
  it.  A type owning the tail means a variant; any other answer means the
  path names the type.
- **D14.** A re-exported name is reachable through the facade that
  re-exports it: one item, many paths, one identity.  A name two of the
  facade's children declare is refused, not picked.
- **D15.** *Stopped.* Requalifying the tree to `import M;` plus `M::Thing`
  is not resumed without a new decision.
- **D16.** An `implement` head writes every parameter of the template it
  names, as its own parameters or as a concrete instantiation; one left
  out is E0302.  `implement<A> trait Display for String<A>`, or `for
  String<GlobalAlloc>`.  The concrete spelling is honoured by selection:
  `for Wrap<T, Alpha>` is not selected for `Wrap<i32, Beta>`.
- **D17.** An `extern "C"` function is public unless written `private`.
- **D18.** The primitive type names stop being keywords, and the alias
  keywords `int`, `uint`, `float`, `double` fold to their primitive at the
  name layer (`int` is `i32`).  The alias fold is part of the language; the
  keyword half is deferred to a branch of its own after the merge.
- **D19.** A bare `malloc`, `free` or `realloc` with no declaration in the
  writing module is an error (E0202), whose note names the modules that
  declare the leaf: write `libc::free` or `allocator::free`.
- **D20.** `import Unknown;` naming no module is E0502 at the import, in
  every form; the help names the module the path abbreviates.
- **D21.** An unknown type in expression or type-argument position
  (`sizeof(Nope)`, `1 as Nope`, `String<Nope>`) is an error.  A defaulted
  type parameter resolves in the module where the type is declared, so
  `String` fills in `GlobalAlloc` without the user importing it; a user who
  writes the argument needs it in scope.
- **D22.** A trait head's trailing type list declares nothing: a name there
  declared nowhere is E0302, and the head is written `implement<T> trait
  Atomic<T> for ...`.
- **D23.** A consumer that meets an unstamped node treats it as an error,
  never as a cue to look the name up another way.
- **D24.** The trait-implementation registry is keyed by the trait's
  identity and the target, a list per key, with one selector by
  unification.  Two traits of one leaf implemented for one type are two
  implementations.
- **D25.** `(Beta for P)::make()` is the impl-qualified call: the method
  `Beta`'s implementation for `P` delivers.  It covers static and receiver
  methods (`(Beta for P)::go(&p)`), values, and generic bodies (`(Beta for
  T)::make()`).
- **D26.** Two traits providing one method for a type is an error (E0156)
  in both call forms, `recv.m(...)` and `T::m(recv, ...)`, with each
  candidate named; never a pick by declaration or import order.  The
  trait-qualified `Tr::m(&recv, ...)` or the impl-qualified call chooses.
- **D27.** *Rejected.* Requiring a trait to be in scope before its methods
  can be called.  Trait methods are called with the trait imported nowhere,
  and scope would not settle a tie; D25 gives a tie its spelling.  Not to
  be proposed again without new measurements.
- **D28.** Overlapping implementations - two heads of one trait that could
  both accept one type - are refused at the later head (E0308), not
  resolved by specialization or declaration order.
- **D29.** `implement trait Tick for struct Holder<S>` with `S` declared
  nowhere is E0302, whose message says the head names `S`, declared
  nowhere.  A nested `Holder<Vec<S>>` is E0203 from the general refusal.
- **D30.** A generic parameter that shadows an enclosing one is refused
  (E0311): a method's `<T>` inside `Box<T>`, a trait method's, an impl
  method's, and `<T, T>` in one list.  Each parameter is its own identity,
  so substitution never merges two parameters of one name.
- **D31.** In expression position `<` opens type arguments only
  immediately after `::`; anywhere else it is a comparison.
  `foo::<i32>(y)`, `Array::<int>::new()`.
- **D32.** *Replaced by the five rules.* A completion criterion counted in
  name-keyed reads.
- **D33.** A `DefId` is a position in a `DefTable` only its own module can
  extend.  `register` always appends and never looks anything up; no
  accessor returns the number, which never reaches output and is never
  persisted or compared across builds.  Instantiations carry their own
  handle rather than a definition.
- **D34.** Intrinsic functions are exported through their module: `import`
  brings them in and the module path reaches them.  A bare `panic` is
  `std::core`'s function.
- **D35.** Function and method overloading are both kept.  A family is
  keyed by its owner (a module or a type) and the written leaf; a lookup
  returns the set and its caller chooses.
- **D36.** Every non-static method writes its receiver explicitly; a method
  the compiler generates still gets one synthesized.  Deferred to a branch
  of its own after the merge.
- **D37.** A check of reference mutability runs before the async lowering,
  which turns an async method's receiver into a raw pointer.  Deferred to
  a branch of its own after the merge.
- **D38.** The editor's bare-name finders live in the editor, not the
  compiler, until the editor's own work after the merge.
- **D39.** An implement block's parameters bind through its head, and a
  parameter's type is its declaration's: every comparison of parameters is
  by identity.
- **D40.** An incremental build loses the warnings of a module reused from
  cache.  Filed, not fixed: a warning total is evidence only from a clean
  build, and warnings compare as sets.
- **D41.** *Replaced by the five rules.* An approved list of name-taking
  functions.
- **D42.** *Removed with the lint layer.* The compiler reporting its own
  lookups through `--emit=facts`.
- **D43.** *Removed with the lane gate.* The lane counts' re-pins.
- **D44.** An import that binds nothing in the requested namespace keeps
  its answer in the other; it is neither an error nor a cue to walk on.
- **D45.** Nothing stores a namespace as text.  A type's, template's or
  symbol's module is a module identity, made once where source text enters;
  `ModuleGraph::module_named` is a door private to the name layer and the
  module loader.
- **D46.** `TypeRef` is sealed: its number and constructor are private to
  its module, read through `key`, compared through `equals`, and made only
  by the arena's allocator.
- **D47.** Identities carry a generation tag checked where they are read: a
  `DefId`, `SymbolID`, `TypeRef` or `OverloadId` another table made, or one
  made before a rebuild, is refused by the table rather than answered.
- **D48.** *Replaced by D47.* One allocator per identity kind.  A second
  table still compiles; nothing it mints is answered.
- **D49.** Both a `deprecated` attribute and a deny-by-default lint whose
  allow carries a reason, for their separate jobs.  Deferred to a branch of
  its own after the merge; the attribute is reconsidered once the lint
  exists (D64).
- **D50.** Two records that disagree are re-measured, not reconciled to
  either document.
- **D51.** A literal or range pattern whose kind is not the subject's type
  is E0200 at the pattern, not an arm that silently never matches.
- **D52-D55, D57, D58.** *Removed with the lint layer.* The spelling-type
  list, the spelling lint's site, E0157, its spec text, the `Keyword` type
  and the lane counts' re-pins.
- **D56.** A link symbol is a `MangledName`, a type of its own, not a
  spelling with a written reason.
- **D59.** A bare identifier in expression position may not name a type:
  it is asked in the value namespace alone, and a name that is only a type
  is E0201 as a value and E0231 as a callee.
- **D60.** The `raw` subcommand builds a module graph as `check` and
  `build` do.  Deferred to a branch of its own after the merge.
- **D61.** A dynamic array's method arguments are type-checked before code
  generation, against `Array<T>`'s method (E0214).
- **D62.** *Removed with the facts emitter.* Declaration records for the
  move checker's count.
- **D63.** The editor drops its fallback for a file with no project.
  Deferred to the editor's work after the merge.
- **D64.** The `deprecated` attribute is reconsidered once the lint of D49
  exists.  Deferred with it.
- **D65.** Explicit receivers on every non-static function, as D36.
  Deferred to a branch of its own after the merge.
- **D66.** With the primitive keywords (D18), a cast followed by a
  comparison without parentheses (`x as u32 < y`) is refused and the
  parentheses asked for.  Deferred with D18.
- **D67, D75.** *Replaced by the five rules.* The audit rounds and the
  third condition of the old finish line.
- **D68.** A class that declares a constructor binds it in the value
  namespace beside the class in the type namespace: `Node(1)` calls the
  constructor.
- **D69-D72, D76.** *Dropped* (2026-09-28, above).
- **D73.** `new C(args)` and the by-value `C(args)` are checked against the
  constructor of their arity.
- **D74.** The monomorphizer matches type arguments by identity: a type
  argument's shape is its kind in the arena, and a written instantiation is
  compared as the type it denotes, never by its display text.
- **D77.** The fast check catches what the full build does for a struct
  compared with `null`, `printf`-style call arguments, and `Point(1, 2)` on
  a struct (closed by D59).

### Open questions carried from the ledger

- **Q1.** *Answered by measurement.* Per-item `public` was not needed at a
  scale that blocked the work.
- **Q2.** *Answered.* An `extern module` alias (`extern module libc as
  cit;`) is a real module in the graph, with a stamp, not a namespace
  symbol resolution special-cases.
- **Q3.** A module cycle is E0501, which names the modules in the cycle.
  Whether it prints the cycle's path, in order, is open; see
  `docs/roadmap.md`.
- **Q4.** Whether `std::Range` is a name: nothing re-exports `Range` at the
  root of `std` today, so the path is refused.  Open; see
  `docs/roadmap.md`.
- **Q13.** *Answered by D68.*
