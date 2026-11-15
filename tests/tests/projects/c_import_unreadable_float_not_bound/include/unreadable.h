/* A floating constant the importer cannot read: a hexadecimal constant needs
 * a binary exponent, so this is not one C accepts.  It must not be bound -
 * any value given it would be made up. */
#define CIU_NO_EXPONENT 0x1.8
