#ifndef FUNCS_H
#define FUNCS_H

void *my_alloc(int size);
void my_free(void *ptr);
void store_int(void *ptr, int value);
int load_int(const void *ptr);

#endif
