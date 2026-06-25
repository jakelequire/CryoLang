/* Local header for the libclang C-import regression test
 * (tests/tests/lang/c_import_libclang.cryo).  Dependency-free (no #includes)
 * so libclang parses exactly this one prototype.  The symbol is defined in
 * abi_helpers.c and linked via libabihelpers.a. */
int bindgen_probe_add3(int a, int b, int c);
