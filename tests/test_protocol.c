/*
 * tests/test_protocol.c — Protocol constant validation tests.
 * C99, no external dependencies.
 */
#include "tgs_protocol.h"
#include <stdio.h>

int main(void) {
    int fail = 0;
    printf("test_protocol:\n");

    /* Stream IDs are contiguous and correct */
    if (TGS_STREAM_HANDSHAKE != 0) { printf("  FAIL: handshake != 0\n"); fail++; }
    if (TGS_STREAM_COMMAND != 1)   { printf("  FAIL: command != 1\n");   fail++; }
    if (TGS_STREAM_EVENT != 4)     { printf("  FAIL: event != 4\n");     fail++; }

    /* Command ID ranges don't overlap */
    if (TGS_CMD_HELLO >= TGS_CMD_WIN_CREATE)    { printf("  FAIL: HELLO >= WIN_CREATE\n");    fail++; }
    if (TGS_CMD_WIN_DESTROY >= TGS_CMD_WGT_CREATE) { printf("  FAIL: WIN_DESTROY >= WGT_CREATE\n"); fail++; }
    if (TGS_CMD_WGT_DESTROY >= TGS_CMD_NTF_RESIZE) { printf("  FAIL: WGT_DESTROY >= NTF_RESIZE\n"); fail++; }
    if (TGS_CMD_NTF_DESTROY >= TGS_CMD_EVT_CLICK)  { printf("  FAIL: NTF_DESTROY >= EVT_CLICK\n");  fail++; }
    if (TGS_CMD_EVT_FOCUS >= TGS_CMD_IME_PREEDIT)   { printf("  FAIL: EVT_FOCUS >= IME_PREEDIT\n");  fail++; }

    /* Widget types: enum ends at TGS_WIDGET_COUNT (>= 20 per FR-5.2.1) */
    if (TGS_WIDGET_COUNT < 20) {
        printf("  FAIL: widget count %d < 20\n", TGS_WIDGET_COUNT);
        fail++;
    }

    /* IME commands exist */
    if (TGS_CMD_IME_PREEDIT != 96) { printf("  FAIL: IME_PREEDIT != 96\n"); fail++; }
    if (TGS_CMD_IME_COMMIT != 97)  { printf("  FAIL: IME_COMMIT != 97\n");  fail++; }

    /* Protocol version defined */
    #ifndef TGS_PROTOCOL_VERSION
    #error "TGS_PROTOCOL_VERSION not defined"
    #endif

    printf("  %s (%d failures)\n", fail ? "FAIL" : "PASS", fail);
    return fail;
}
