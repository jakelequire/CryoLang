#ifndef FUNCS_H
#define FUNCS_H

typedef struct OpaqueCtx *CtxRef;

CtxRef create_ctx(int id);
int ctx_get_id(CtxRef ctx);
void destroy_ctx(CtxRef ctx);

#endif
