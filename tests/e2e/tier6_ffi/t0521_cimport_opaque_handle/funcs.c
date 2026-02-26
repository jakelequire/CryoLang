#include "funcs.h"
#include <stdlib.h>

struct OpaqueCtx {
    int id;
};

CtxRef create_ctx(int id) {
    CtxRef ctx = (CtxRef)malloc(sizeof(struct OpaqueCtx));
    ctx->id = id;
    return ctx;
}

int ctx_get_id(CtxRef ctx) {
    return ctx->id;
}

void destroy_ctx(CtxRef ctx) {
    free(ctx);
}
