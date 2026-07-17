/* Local header for the libclang C-import regression test
 * (tests/tests/lang/c_import_libclang.cryo).  The only non-system include is
 * <stdarg.h>, needed for the `va_list` probes; libclang's system-header filter
 * drops stdarg's own decls, so the imported surface stays exactly these
 * prototypes.  All symbols are defined in abi_helpers.c and linked via the
 * per-OS libabihelpers archive (see the top-level Makefile). */
#include <stdarg.h>

int bindgen_probe_add3(int a, int b, int c);

/* va_list probes: exercise the first-class `va_list` type end-to-end - the
 * importer maps each `va_list` parameter to `va_list`, and the tests forward a
 * Cryo variadic function's `args` into them.  Spread across the SysV arg
 * classes so the va_list register-save / overflow areas are all exercised:
 *   _vsum     n integer varargs (10+ spills past the GP registers)
 *   _vsum_d   n double varargs  (SSE class / fp_offset)
 *   _vmix     ni ints then nd doubles (both offsets advance)
 *   _vstrlen  n `const char*` varargs (pointer class)
 *   _vfmt     forwards the va_list a SECOND time into real libc vsnprintf */
int    bindgen_probe_vsum(int n, va_list ap);
double bindgen_probe_vsum_d(int n, va_list ap);
double bindgen_probe_vmix(int ni, int nd, va_list ap);
int    bindgen_probe_vstrlen(int n, va_list ap);
int    bindgen_probe_vfmt(char *buf, unsigned long size, const char *fmt, va_list ap);
