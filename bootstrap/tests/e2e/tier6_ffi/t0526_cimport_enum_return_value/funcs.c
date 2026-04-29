#include "funcs.h"

Status do_operation(int succeed) {
    if (succeed != 0) {
        return STATUS_OK;
    }
    return STATUS_ERROR;
}

const char *status_string(Status s) {
    switch (s) {
        case STATUS_OK:      return "ok";
        case STATUS_ERROR:   return "error";
        case STATUS_PENDING: return "pending";
        default:             return "unknown";
    }
}
