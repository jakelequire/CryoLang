# Intrinsics exported, the member stamped by namespace, visibility by identity

Every script lives beside this file and runs from the repository root
(`python scripts/ns-migration/8.358/<script>`), on the parent commit.

1. `git apply --3way scripts/ns-migration/8.358/unit3-ready.patch` - the
   intrinsics unit as measured before (intrinsic functions exported, the
   prelude's `core::intrinsics` line gone, `ScopeResolutionNode.member_def`
   stamped by the name layer and read by sema).  One conflict:
   `python scripts/ns-migration/8.358/resolve.py compiler/src/compiler/decl_index.cryo c`
   keeps `hidden_from` beside the functions the tip deleted.
2. `u2_rekey.py` - `DefTable` records each definition's visibility
   (`DeclVisibility`), the path-keyed `decl_visibility` map is deleted, and
   `hidden_from` compares the declaring module's definition with the use
   site's.
3. `u2_writers.py` - the three writers in type resolution pass the
   definition; `IntrinsicDeclNode.def` is stamped as a function's is; the
   gate turns the use site's namespace into its module.  (Plus, by hand, the
   `ModulePath` import in `call_resolver.cryo`.)
4. At this point the build is the patch with the store re-keyed:
   `shapes/run.sh <that compiler> v1 v2 v3 v4` shows the hole - v1 and v4
   compile and call a private function.
5. `u2_stamp.py` - the member is looked up in the namespace the path is
   written in (`Resolver::exports_named`, `private_declarations`;
   `NameResolution::export_in`, `private_in`), and by hand the extern-module
   alias branch asks `export_in(.., Namespace::Value)`.  v1 and v4 are E0353
   again; v2 and v3 run.
6. `sec0.py` - the section 0 rows the change moved.
