/* Header for the Phase-2 C-import type-emission regression test
 * (tests/tests/lang/c_import_types.cryo).  Dependency-free (no #includes) so
 * libclang parses exactly these declarations.  No functions => no C symbols to
 * link; the test exercises emitted struct/enum/typedef types in pure Cryo. */
typedef int Celsius;            /* scalar typedef -> i32 alias */
struct Vec2 { int x; int y; };  /* record -> type struct */
enum Mode { MODE_OFF = 0, MODE_ON = 7 };  /* named enum w/ explicit values */
/* anonymous enum -> alias-namespaced global consts (cit::FLAG_A, ...); the
 * negative member exercises the two's-complement width-truncation path. */
enum { FLAG_A = 1, FLAG_B = 2, FLAG_NEG = -3 };
