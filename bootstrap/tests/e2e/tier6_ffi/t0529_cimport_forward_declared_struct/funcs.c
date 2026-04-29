#include "funcs.h"
#include <stdlib.h>
#include <string.h>

struct Connection {
    char addr[256];
};

struct Connection *conn_open(const char *addr) {
    struct Connection *c = (struct Connection *)malloc(sizeof(struct Connection));
    strncpy(c->addr, addr, sizeof(c->addr) - 1);
    c->addr[sizeof(c->addr) - 1] = '\0';
    return c;
}

void conn_close(struct Connection *c) {
    free(c);
}

const char *conn_address(const struct Connection *c) {
    return c->addr;
}
