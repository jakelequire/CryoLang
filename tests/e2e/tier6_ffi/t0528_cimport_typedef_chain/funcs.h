#ifndef FUNCS_H
#define FUNCS_H

typedef int BaseType;
typedef BaseType MiddleType;
typedef MiddleType FinalType;

FinalType chain_add(FinalType a, FinalType b);

typedef const char *CString;

CString chain_greet(CString name);

#endif
