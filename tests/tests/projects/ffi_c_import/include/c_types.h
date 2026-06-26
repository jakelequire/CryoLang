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
 * Covers decimal, hex, negative, and float.  The function-like and string macros
 * below must be reported-and-skipped, never bound. */
#define CIT_MAX  256
#define CIT_MASK 0xFF
#define CIT_NEG  -3
#define CIT_HALF 3.5
#define CIT_SQUARE(x) ((x) * (x))   /* function-like: unbindable, skipped */
#define CIT_NAME "cryo"             /* string: not yet bound, skipped */
