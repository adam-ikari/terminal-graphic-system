/*
 * TGS Input Abstraction
 * Polls platform input (SDL events) and injects into backend.
 */
#ifndef TGS_INPUT_H
#define TGS_INPUT_H

#include "tgs_backend.h"

int  input_init(tgs_backend *backend);
void input_poll(void);
void input_cleanup(void);

#endif /* TGS_INPUT_H */
