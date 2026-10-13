/* Fixture for scripts/vendor-consts-gate.py: the constant shapes a C API
 * publishes, one per way the importer can arrive at a value.  A constant the
 * importer binds and the serializer then fails to write has no trace anywhere
 * downstream - the use site fails with "cannot find value" and nothing
 * connects that to generation - so the gate consumes every name below and a
 * drop turns into a build failure.
 *
 * Values are distinctive and pairwise distinct so an assertion cannot pass by
 * matching a different constant's value. */

#define VC_MAKE_VERSION(major, minor) (((major) << 8) | (minor))

/* A single literal: the shape the simple token pass binds on its own. */
#define VC_PLAIN_INT   4097

/* An object-like macro whose body INVOKES a function-like macro.  Neither a
 * literal nor an operator chain over literals, so no token walk folds it
 * without becoming a preprocessor; it binds only through the synthesized
 * constant-folding probe TU.  This is how a C API publishes a version or a
 * flag family whose width it wants pinned. */
#define VC_TWO_STEP    VC_MAKE_VERSION(1, 3)

/* Constant arithmetic over an earlier macro const. */
#define VC_ARITH       (VC_PLAIN_INT + 3)

/* A NEGATIVE constant is carried as its unsigned two's-complement decimal, a
 * 64-bit pattern that is out of range for the narrower declared type as a bare
 * literal.  The cast in the emitted source is what says "take the low bits",
 * so this is the case that fails if the cast is ever dropped rather than
 * rendered. */
#define VC_NEGATIVE    (-4098)

#define VC_HEX_U       0x00002004u
#define VC_CHAR        'Q'
#define VC_FLOAT       3.5f
#define VC_STRING      "vendor-consts"

/* The wide-flag shape: a value too wide for a C enum is spelled `static const`,
 * which has no linker symbol outside the translation unit defining it, so it
 * binds as a folded VALUE or not at all.  Vulkan's 64-bit stage flags are this
 * shape.  The width comes from the declared C type, not the magnitude. */
static const unsigned long long VC_WIDE_FLAG = 0x100000000ull;

enum VcKind { VC_KIND_NONE = 0, VC_KIND_SOME = 7 };

int vc_touch(int x);
