/*
 * TGS Frame encode/decode API.
 * Fixed-buffer, no dynamic allocation.
 */
#ifndef TGS_FRAME_H
#define TGS_FRAME_H

#define TGS_MAX_ARGS    16
#define TGS_MAX_ARG_LEN 128

typedef struct {
    int stream_id;
    int frame_id;
    int command;
    char args[TGS_MAX_ARGS][TGS_MAX_ARG_LEN];
    int num_args;
} tgs_frame;

/*
 * Encode payload only (without ESC_ / ST delimiters).
 * out buffer receives: "TGS;<stream_id>;<frame_id>;<command>;<arg1>;..."
 * Returns bytes written, -1 on error.
 */
int tgs_frame_encode(int stream_id, int frame_id, int command,
                     const char *args[], int num_args,
                     char *out, int out_size);

/*
 * Decode payload (text between ESC_ and ST).
 * Returns 0 on success, -1 on error.
 */
int tgs_frame_decode(const char *data, int len, tgs_frame *frame);

/*
 * Write complete APC frame to fd: ESC _ <payload> ESC \
 * Returns total bytes written, -1 on error.
 */
int tgs_frame_write(int fd, int stream_id, int frame_id, int command,
                    const char *args[], int num_args);

#endif /* TGS_FRAME_H */
