/* C header for the FFI / C-import test project.  Dependency-free (no #includes)
 * so libclang parses exactly these declarations.  No functions => no C symbols
 * to link; the project exercises the imported types and constants in pure Cryo. */

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
