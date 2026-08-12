/* C header for the FFI / C-import test project.  Dependency-free (no #includes)
 * so libclang parses exactly these declarations.  No functions => no C symbols
 * to link; the project exercises the imported types and constants in pure Cryo. */

#include "c_inner.h"                       /* its CIT_INNER must bind too */

/* A macro whose body is another macro's INVOCATION.  This is how portable C
 * pins a constant's width, and it is what most current C APIs use for flags
 * (SDL's `SDL_UINT32_C`, Vulkan's version macros).  No token walk can fold it
 * without becoming a preprocessor, so it is folded by clang instead.
 * CIT_FLAG_WIDE's VALUE fits 32 bits while its TYPE is 64-bit: the width is the
 * thing the macro exists to state, so it must survive. */
#define CIT_U32(x) x ## u
#define CIT_U64(x) x ## ull
#define CIT_FLAG_VIDEO CIT_U32(0x20)
#define CIT_FLAG_WIDE  CIT_U64(0x10000000)
#define CIT_FLAG_BOTH  (CIT_FLAG_VIDEO | CIT_U32(0x40))

typedef int Celsius;                       /* scalar typedef        -> i32 alias */
struct Vec2 { int x; int y; };             /* record                -> type struct */
enum Mode { MODE_OFF = 0, MODE_ON = 7 };   /* named enum w/ explicit discriminants */

/* anonymous enum -> one alias-namespaced `const` per constant (cit::FLAG_A, ...);
 * the negative member exercises the two's-complement width-truncation path. */
enum { FLAG_A = 1, FLAG_B = 2, FLAG_NEG = -3 };

/* object-like #define constants -> alias-namespaced `const`s (cit::CIT_MAX, ...).
 * Covers decimal, hex, negative, float, string, and char.  The function-like
 * macro must be reported-and-skipped, never bound. */
#define CIT_MAX  256
#define CIT_MASK 0xFF
#define CIT_NEG  -3
#define CIT_HALF 3.5
#define CIT_NAME "cry\to"            /* string -> `string` const (escape decoded) */
#define CIT_NL   '\n'               /* char   -> `char` const (code point 10)    */
#define CIT_LET  'A'                /* char   -> `char` const (code point 65)    */
#define CIT_SQUARE(x) ((x) * (x))   /* function-like: unbindable, reported-skipped */

/* Bitfields share a storage unit: a per-field map would inflate the layout, so
 * the run collapses to one opaque storage blob.  sizeof must stay 8 (two `unsigned`
 * bitfields packed into the first 4-byte unit, then `c`). */
struct Bits { unsigned a : 3; unsigned b : 5; int c; };

/* A C11 anonymous union MEMBER (no field name): contributes 4 bytes of storage
 * with no FieldDecl, so a naive field walk would undersize it.  sizeof == 8. */
struct Tagged { int tag; union { int i; float f; }; };

/* A named field of anonymous struct type: mapped to a layout-faithful blob, not
 * void*.  sizeof == 12 (kind + an 8-byte {x,y}). */
struct Named { int kind; struct { int x; int y; } pt; };

/* A top-level named union -> a native Cryo `type union` (one field per member,
 * direct `v.i`/`v.f` access; a literal initializes exactly one member).  Members
 * overlay at offset 0, so sizeof == 4 (the largest member). */
union Value { int i; float f; };

/* `extern` globals: storage the linked library owns.  Binding them is what a
 * loader-shaped C API needs - volk and its kind expose every entry point as a
 * global function pointer rather than a prototype.  These are declared and
 * never referenced by the tests on purpose: an unreferenced imported global
 * emits no symbol, so this project keeps linking with no C library behind it,
 * while a regression that made globals emit unparseable or ill-typed Cryo would
 * fail the whole project's build.  A `static` global is skipped and reported -
 * it has no symbol outside the translation unit that defines it. */
extern int cit_counter;
extern int (*cit_hook)(int);
static int cit_private = 3;

/* A `static const` is not a symbol either, but unlike the above it carries a
 * compile-time VALUE - and it is how a C API states a constant too wide for an
 * enum, which is exactly how Vulkan spells its 64-bit flag bits.  Bound by
 * value, at the width the C type gives it (not the width the value would fit).
 * `cit_private` above is the contrast: `static` but not `const`, so there is no
 * constant to bind and it is reported instead. */
static const unsigned long long CIT_WIDE_BIT = 0x100000000ULL;

/* Tags declared but never DEFINED anywhere in this translation unit.  Xlib's
 * `_XEvent` is the real case: SDL declares `typedef union _XEvent XEvent;` on
 * every platform, and off X11 nothing completes it.  Such a tag has no members
 * and no size, so it binds as a zero-field opaque record - all C permits is a
 * pointer to it.  Dropping it instead leaves the typedef, and any parameter or
 * return using it, naming a record that was never emitted. */
typedef union  _NeverU NeverU;
typedef struct _NeverS NeverS;
