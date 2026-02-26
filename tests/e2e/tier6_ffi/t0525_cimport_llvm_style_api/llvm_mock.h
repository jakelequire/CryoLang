#ifndef LLVM_MOCK_H
#define LLVM_MOCK_H

typedef struct OpaqueModule *ModuleRef;
typedef struct OpaqueBuilder *BuilderRef;

typedef enum { TypeVoid = 0, TypeInt = 1, TypeFloat = 2 } TypeKind;

ModuleRef module_create(const char *name);
void module_dispose(ModuleRef m);
const char *module_get_name(ModuleRef m);

BuilderRef builder_create(void);
void builder_dispose(BuilderRef b);

const char *type_kind_name(TypeKind kind);

#endif
