/*
 * TGS Backend Registry
 * Simple static pointer: register sets it, get returns it.
 */
#include <stddef.h>
#include "tgs_backend.h"

static tgs_backend *registered_backend = NULL;

void tgs_backend_register(tgs_backend *backend)
{
    registered_backend = backend;
}

tgs_backend *tgs_backend_get(void)
{
    return registered_backend;
}
