/* Constants whose value is an infinity: no float literal spells one, and the
 * C runtime's text for it reads back as another number, so none is bound.
 * CIF_DOUBLED is finite and is bound. */
#define CIF_HUGE 1e300
#define CIF_DOUBLED (CIF_HUGE * 2.0)
#define CIF_OVERFLOW (CIF_HUGE * CIF_HUGE)
static const double cif_infinity = 1e300 * 1e300;
