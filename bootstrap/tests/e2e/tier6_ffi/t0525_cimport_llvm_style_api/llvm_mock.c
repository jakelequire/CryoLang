#include "llvm_mock.h"
#include <stdlib.h>
#include <string.h>

struct OpaqueModule {
    char *name;
};

struct OpaqueBuilder {
    int dummy;
};

ModuleRef module_create(const char *name) {
    ModuleRef m = (ModuleRef)malloc(sizeof(struct OpaqueModule));
    m->name = strdup(name);
    return m;
}

void module_dispose(ModuleRef m) {
    free(m->name);
    free(m);
}

const char *module_get_name(ModuleRef m) {
    return m->name;
}

BuilderRef builder_create(void) {
    BuilderRef b = (BuilderRef)malloc(sizeof(struct OpaqueBuilder));
    b->dummy = 1;
    return b;
}

void builder_dispose(BuilderRef b) {
    free(b);
}

const char *type_kind_name(TypeKind kind) {
    static const char *names[] = { "Void", "Int", "Float" };
    if (kind >= 0 && kind <= 2) return names[kind];
    return "Unknown";
}
