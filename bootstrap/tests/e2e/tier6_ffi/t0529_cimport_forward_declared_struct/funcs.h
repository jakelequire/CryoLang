#ifndef FUNCS_H
#define FUNCS_H

struct Connection;

struct Connection *conn_open(const char *addr);
void conn_close(struct Connection *c);
const char *conn_address(const struct Connection *c);

#endif
