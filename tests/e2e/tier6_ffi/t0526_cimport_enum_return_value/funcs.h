#ifndef FUNCS_H
#define FUNCS_H

typedef enum { STATUS_OK = 0, STATUS_ERROR = 1, STATUS_PENDING = 2 } Status;

Status do_operation(int succeed);
const char *status_string(Status s);

#endif
