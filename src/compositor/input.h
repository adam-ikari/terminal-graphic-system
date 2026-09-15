/*
 * TGS Input Abstraction
 * Polls platform input (SDL events) and injects into backend.
 */
#ifndef TGS_INPUT_H
#define TGS_INPUT_H

#include "tgs_backend.h"

/* A program that has not spoken TGS is a character program: its keys must
 * reach it exactly as a terminal would send them. The sink is the compositor's
 * hook for that — the platform backends stay unaware of who consumes keys. */
typedef void (*tgs_input_key_sink)(int key, int mods, int pressed, void *ud);
void input_set_key_sink(tgs_input_key_sink sink, void *ud);

int  input_init(tgs_backend *backend);
void input_poll(void);
void input_cleanup(void);

#endif /* TGS_INPUT_H */
