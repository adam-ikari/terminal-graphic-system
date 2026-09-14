/*
 * TGS Terminal Output
 * Raw mode, alt screen, mouse tracking setup/teardown.
 */
#ifndef TGS_OUTPUT_H
#define TGS_OUTPUT_H

int output_get_size(int *cols, int *rows, int *pix_w, int *pix_h);
int output_init(void);
void output_cleanup(void);

#endif /* TGS_OUTPUT_H */
