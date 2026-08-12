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
/* Object-like macro constants, in the shapes a C API actually uses.
 *
 * A flag family is normally written as a macro INVOCATION that pins the
 * constant's width - `SDL_UINT32_C(0x20)`, `VK_MAKE_VERSION(...)` - which is
 * neither a literal nor an operator chain over literals, so no token walk can
 * fold it without reimplementing the preprocessor. Those bind by way of the
 * synthesized constant-folding probe TU (Importer::probe_macro_constants); with
 * the probe absent an entire flag family is dropped SILENTLY, and the first
 * symptom is a runtime call with a zero flag.
 *
 * `_PLAIN` and `_ARITH` are the controls: bodies the token walk folds on its
 * own, which must keep binding to the same values.
 *
 * These are `#define`s only - no new symbol for abi_helpers.c to define. */
#define BINDGEN_PROBE_UINT32_C(x)  ((unsigned int)(x))
#define BINDGEN_PROBE_FLAG_VIDEO   BINDGEN_PROBE_UINT32_C(0x00000020)
#define BINDGEN_PROBE_FLAG_AUDIO   BINDGEN_PROBE_UINT32_C(0x00000040)
#define BINDGEN_PROBE_PLAIN_CONST  7
#define BINDGEN_PROBE_ARITH_CONST  (BINDGEN_PROBE_PLAIN_CONST + 1)

int    bindgen_probe_vsum(int n, va_list ap);
double bindgen_probe_vsum_d(int n, va_list ap);
double bindgen_probe_vmix(int ni, int nd, va_list ap);
int    bindgen_probe_vstrlen(int n, va_list ap);
int    bindgen_probe_vfmt(char *buf, unsigned long size, const char *fmt, va_list ap);
