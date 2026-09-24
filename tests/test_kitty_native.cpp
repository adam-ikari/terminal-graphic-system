/*
 * kitty-native graphics frame tests — the TGS ⊃ kitty superset face,
 * receive side (kitty_native.h).
 *
 * These pin the contract a real kitty-graphics program depends on:
 * cursor-cell placement with X/Y in-cell offsets, chunked base64 assembly
 * across 4096-byte splits, PNG and raw formats, crop and c/r display
 * sizing, a=p moves, the delete scopes, and the i=-keynowledged reply
 * contract (q=0/1/2). Rounding in the alpha blend and the nearest-
 * neighbour scale is part of the pixel contract, so the expected values
 * restate the same integer arithmetic.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <poll.h>
#include <string>
#include <unistd.h>

extern "C" {
#include "kitty_native.h"
#include "parser.h"
#include "term.h"
#include "stb_image_write.h"

/* The character-base view lives in the scene backend. */
void *tgs_term_view_create(int cols, int rows);
void tgs_term_view_draw(void *view, const tgs_term *t);
void tgs_term_view_destroy(void *view);
int tgs_term_view_cell_w(void);
int tgs_term_view_cell_h(void);
const uint32_t *tgs_term_view_pixels(void *view);
}

namespace {

const int COLS = 100;
const int ROWS = 37;
const uint32_t DEF_BG = 0xFF101014u;            /* term_view.c's prefill */

std::string b64_encode(const std::string &bin)
{
    static const char *A =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;

    for (; i + 2 < bin.size(); i += 3) {
        uint32_t v = ((uint32_t)(uint8_t)bin[i] << 16) |
                     ((uint32_t)(uint8_t)bin[i + 1] << 8) |
                     (uint32_t)(uint8_t)bin[i + 2];
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += A[v & 63];
    }
    if (i + 1 == bin.size()) {
        uint32_t v = (uint32_t)(uint8_t)bin[i] << 16;
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bin.size()) {
        uint32_t v = ((uint32_t)(uint8_t)bin[i] << 16) |
                     ((uint32_t)(uint8_t)bin[i + 1] << 8);
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

/* 4×4 RGBA test pattern: pixel i has distinct channels (opaque). */
std::string pattern4x4()
{
    std::string s;
    for (int i = 0; i < 16; i++) {
        s += (char)(i * 15 + 1);
        s += (char)(250 - i * 14);
        s += (char)((i * 37) & 0xFF);
        s += (char)255;
    }
    return s;
}

uint32_t pattern_px(int i)
{
    uint32_t r = (uint32_t)(i * 15 + 1) & 0xFF;
    uint32_t g = (uint32_t)(250 - i * 14) & 0xFF;
    uint32_t b = (uint32_t)(i * 37) & 0xFF;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Write-end of a pipe stands in for the program's pty. */
class ReplyPipe {
public:
    ReplyPipe() { ::pipe(fds_); }
    ~ReplyPipe() { ::close(fds_[0]); ::close(fds_[1]); }
    int fd() const { return fds_[1]; }
    std::string read(int ms = 150)
    {
        struct pollfd p = { fds_[0], POLLIN, 0 };
        std::string out;
        if (::poll(&p, 1, ms) > 0 && (p.revents & POLLIN)) {
            char b[512];
            ssize_t n = ::read(fds_[0], b, sizeof b);
            if (n > 0) out.assign(b, (size_t)n);
        }
        return out;
    }
private:
    int fds_[2];
};

class KittyNative : public ::testing::Test {
protected:
    void SetUp() override
    {
        tgs_kitty_reset();
        (void)tgs_kitty_take_dirty();           /* start from settled */
        term = tgs_term_new(COLS, ROWS);
        view = tgs_term_view_create(COLS, ROWS);
        ASSERT_TRUE(term && view);
        /* Hide the cursor: the empty base is then exactly DEF_BG, so
         * every surrounding-pixel assertion is exact. */
        const char *hide = "\x1b[?25l";
        tgs_term_feed(term, (const uint8_t *)hide, (int)strlen(hide));
    }

    void TearDown() override
    {
        tgs_kitty_reset();
        tgs_term_view_destroy(view);
        tgs_term_free(term);
    }

    void cup(int row1, int col1)                 /* CUP, 1-based */
    {
        char b[32];
        int n = snprintf(b, sizeof b, "\x1b[%d;%dH", row1, col1);
        tgs_term_feed(term, (const uint8_t *)b, n);
    }

    void text(const char *s)
    {
        tgs_term_feed(term, (const uint8_t *)s, (int)strlen(s));
    }

    void feed(const std::string &payload, int reply_fd = -1)
    {
        tgs_kitty_feed((const uint8_t *)payload.data(), (int)payload.size(),
                       term, tgs_term_view_cell_w(), tgs_term_view_cell_h(),
                       reply_fd);
    }

    void redraw()
    {
        tgs_term_view_draw(view, term);
        tgs_kitty_draw(const_cast<uint32_t *>(tgs_term_view_pixels(view)),
                       COLS * tgs_term_view_cell_w(),
                       ROWS * tgs_term_view_cell_h());
    }

    uint32_t at(int x, int y) const
    {
        return tgs_term_view_pixels(view)[(size_t)y * COLS * tgs_term_view_cell_w() + x];
    }

    tgs_term *term = nullptr;
    void *view = nullptr;
};

/* ── Parser routing ─────────────────────────────────────────────────────── */

struct RouteCounters { int frames = 0, kitty = 0, text = 0; int last_kitty_len = 0; };

void cb_frame(const tgs_frame *, void *ud) { ((RouteCounters *)ud)->frames++; }
void cb_kitty(const uint8_t *, int len, void *ud)
{
    RouteCounters *c = (RouteCounters *)ud;
    c->kitty++;
    c->last_kitty_len = len;
}
void cb_text(const uint8_t *, int, void *ud) { ((RouteCounters *)ud)->text++; }

TEST(KittyRouting, oneChannelThreeFates)
{
    tgs_parser p;
    RouteCounters c;

    tgs_parser_init(&p, cb_frame, &c);
    tgs_parser_set_text_cb(&p, cb_text, &c);
    tgs_parser_set_kitty_cb(&p, cb_kitty, &c);

    auto feedp = [&](const std::string &s) {
        tgs_parser_feed(&p, (const uint8_t *)s.data(), (int)s.size());
    };

    feedp("\x1b_G1;0;0;1;2.0;\x1b\\");
    EXPECT_EQ(c.frames, 1);
    EXPECT_EQ(c.kitty, 0);

    feedp("\x1b_Ga=T,i=5,f=32,w=1,h=1;\x1b\\");
    EXPECT_EQ(c.kitty, 1);
    EXPECT_EQ(c.frames, 1);

    feedp("plain text");
    EXPECT_EQ(c.text, 1);

    /* The same frame split across feeds still routes to one fate. */
    feedp("\x1b_Ga=");
    feedp("T,i=6\x1b\\");
    EXPECT_EQ(c.kitty, 2);
    EXPECT_EQ(c.frames, 1);
    EXPECT_EQ(c.text, 1);
}

TEST(KittyRouting, fullSizeChunkSurvivesTheBuffer)
{
    tgs_parser p;
    RouteCounters c;

    tgs_parser_init(&p, cb_frame, &c);
    tgs_parser_set_kitty_cb(&p, cb_kitty, &c);

    /* One kitty chunk is base64(4096 binary) ≈ 5.5KB — the case the old
     * 4096-byte parser buffer silently dropped. */
    std::string payload = "Ga=T,i=9,f=32,w=32,h=32;" + std::string(5400, 'A');
    std::string frame = "\x1b_" + payload + "\x1b\\";
    tgs_parser_feed(&p, (const uint8_t *)frame.data(), (int)frame.size());

    EXPECT_EQ(c.kitty, 1);
    EXPECT_EQ(c.last_kitty_len, (int)payload.size());
    EXPECT_EQ(c.frames, 0);
}

/* ── Placement ──────────────────────────────────────────────────────────── */

TEST_F(KittyNative, rawAtCursorWithCellOffset)
{
    cup(3, 5);                                   /* cx=4, cy=2 → (32,32) */
    feed("Ga=T,i=1,f=32,w=4,h=4,X=1,Y=2,C=1;" +
         b64_encode(pattern4x4()));
    EXPECT_EQ(tgs_kitty_take_dirty(), 1);
    redraw();

    for (int dy = 0; dy < 4; dy++)
        for (int dx = 0; dx < 4; dx++)
            EXPECT_EQ(at(33 + dx, 34 + dy), pattern_px(dy * 4 + dx))
                << "pixel " << dx << "," << dy;
    /* The X/Y origin row/column of the cell was not covered. */
    EXPECT_EQ(at(32, 34), DEF_BG);
    EXPECT_EQ(at(33, 32), DEF_BG);
    /* And nothing spilled past the image. */
    EXPECT_EQ(at(37, 34), DEF_BG);
    EXPECT_EQ(at(33, 38), DEF_BG);
}

TEST_F(KittyNative, imageWinsOverGlyphs)
{
    text("AB");                                   /* glyphs in cells 0,1 */
    cup(1, 1);                                    /* back over them */
    feed("Ga=T,i=1,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    for (int dy = 0; dy < 4; dy++)
        for (int dx = 0; dx < 4; dx++)
            EXPECT_EQ(at(dx, dy), pattern_px(dy * 4 + dx));
}

TEST_F(KittyNative, pngRoundtripWithAlphaBlend)
{
    uint8_t rgba[8 * 8 * 4];
    for (int i = 0; i < 64; i++) {
        rgba[i * 4] = (uint8_t)(i * 4);
        rgba[i * 4 + 1] = (uint8_t)(255 - i * 3);
        rgba[i * 4 + 2] = (uint8_t)(i * 7);
        rgba[i * 4 + 3] = 255;
    }
    rgba[0] = 200; rgba[1] = 100; rgba[2] = 50; rgba[3] = 128;  /* blend px */

    std::string png;
    auto png_cb = [](void *ctx, void *data, int size) {
        ((std::string *)ctx)->append((const char *)data, (size_t)size);
    };
    ASSERT_TRUE(stbi_write_png_to_func(png_cb, &png, 8, 8, 4, rgba, 8 * 4));

    feed("Ga=T,i=2,f=100,C=1;" + b64_encode(png));
    redraw();

    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            if (x == 0 && y == 0) continue;
            uint32_t expect = 0xFF000000u | ((uint32_t)rgba[(y * 8 + x) * 4] << 16) |
                              ((uint32_t)rgba[(y * 8 + x) * 4 + 1] << 8) |
                              (uint32_t)rgba[(y * 8 + x) * 4 + 2];
            EXPECT_EQ(at(x, y), expect) << "at " << x << "," << y;
        }
    /* The half-transparent pixel: same integer /255 contract as term_view. */
    {
        uint32_t d = DEF_BG;
        unsigned a = 128, ia = 255 - a;
        unsigned r = (200 * a + ((d >> 16) & 0xFF) * ia) / 255;
        unsigned g = (100 * a + ((d >> 8) & 0xFF) * ia) / 255;
        unsigned b = (50 * a + (d & 0xFF) * ia) / 255;
        EXPECT_EQ(at(0, 0), 0xFF000000u | (r << 16) | (g << 8) | b);
    }
}

TEST_F(KittyNative, raw24TakesThreeBytesPerPixel)
{
    std::string raw;
    raw += (char)10; raw += (char)20; raw += (char)30;
    raw += (char)40; raw += (char)50; raw += (char)60;
    raw += (char)70; raw += (char)80; raw += (char)90;
    raw += (char)100; raw += (char)110; raw += (char)120;
    feed("Ga=T,i=3,f=24,w=2,h=2,C=1;" + b64_encode(raw));
    redraw();
    EXPECT_EQ(at(0, 0), 0xFF0A141Eu);
    EXPECT_EQ(at(1, 0), 0xFF28323CU);
    EXPECT_EQ(at(0, 1), 0xFF46505AU);
    EXPECT_EQ(at(1, 1), 0xFF646E78u);
}

TEST_F(KittyNative, chunkedAssemblyAcrossASplit)
{
    /* 4096 binary bytes as f=32 for 32×32 — split the base64 at a
     * NON-quantum boundary (4097) so the streaming carry is exercised.
     * Byte 3 of every pixel is the alpha channel: keep it opaque here
     * (the alpha path is pinned by pngRoundtripWithAlphaBlend). */
    std::string bin;
    for (int i = 0; i < 4096; i++)
        bin += (char)(((i % 4) == 3) ? 255 : ((i * 31) & 0xFF));
    std::string enc = b64_encode(bin);
    ASSERT_EQ(enc.size(), 5464u);

    std::string c1 = enc.substr(0, 4097);
    std::string c2 = enc.substr(4097);
    feed("Ga=T,i=4,f=32,w=32,h=32,m=1;" + c1);
    EXPECT_EQ(tgs_kitty_take_dirty(), 0);         /* nothing visible yet */
    feed("Ga=T,i=4,f=32,w=32,h=32,m=0;" + c2);
    redraw();
    EXPECT_EQ(at(0, 0), 0xFF000000u |
              ((uint32_t)(uint8_t)bin[0] << 16) |
              ((uint32_t)(uint8_t)bin[1] << 8) |
              (uint32_t)(uint8_t)bin[2]);
    /* pixel (1,0) = bytes 4..6 of the raw stream */
    EXPECT_EQ(at(1, 0), 0xFF000000u |
              ((uint32_t)(uint8_t)bin[4] << 16) |
              ((uint32_t)(uint8_t)bin[5] << 8) |
              (uint32_t)(uint8_t)bin[6]);
}

TEST_F(KittyNative, cropThenNearestNeighbourScale)
{
    feed("Ga=T,i=5,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    /* Display only the 2×2 sub-rectangle at (1,1), scaled to 8 columns
     * (64px) with the rows aspect-derived (also 64px for a square crop). */
    feed("Ga=p,i=5,x=1,y=1,w=2,h=2,c=8");
    redraw();

    /* NN: dx<32 → src col 1, else col 2; dy<32 → row 1, else row 2. */
    auto expect = [&](int dx, int dy) {
        int sx = 1 + (dx * 2 / 64);
        int sy = 1 + (dy * 2 / 64);
        return pattern_px(sy * 4 + sx);
    };
    EXPECT_EQ(at(0, 0), expect(0, 0));
    EXPECT_EQ(at(31, 31), expect(31, 31));
    EXPECT_EQ(at(32, 0), expect(32, 0));
    EXPECT_EQ(at(63, 63), expect(63, 63));
    EXPECT_EQ(at(64, 0), DEF_BG);                 /* past the display size */
}

TEST_F(KittyNative, moveByPlacementLeavesNoTrail)
{
    cup(11, 2);                                   /* (8,160) */
    feed("Ga=T,i=6,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    EXPECT_EQ(at(8, 160), pattern_px(0));

    cup(21, 11);                                  /* (80,320) */
    feed("Ga=p,i=6");
    redraw();
    EXPECT_EQ(at(80, 320), pattern_px(0));
    EXPECT_EQ(at(8, 160), DEF_BG);                /* base repaint wiped it */
}

TEST_F(KittyNative, deleteScopes)
{
    cup(2, 1);
    feed("Ga=T,i=7,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    EXPECT_EQ(at(0, 16), pattern_px(0));

    feed("Ga=d,i=7,d=i");                          /* by id */
    redraw();
    EXPECT_EQ(at(0, 16), DEF_BG);

    /* d=a wipes everything. */
    feed("Ga=T,i=8,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    EXPECT_EQ(at(0, 16), pattern_px(0));
    feed("Ga=d,d=a");
    redraw();
    EXPECT_EQ(at(0, 16), DEF_BG);

    /* Default d=c clears the placement at the cursor cell. */
    cup(2, 1);
    feed("Ga=T,i=9,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    EXPECT_EQ(at(0, 16), pattern_px(0));
    feed("Ga=d");
    redraw();
    EXPECT_EQ(at(0, 16), DEF_BG);
}

TEST_F(KittyNative, autoIdStillDisplays)
{
    feed("Ga=T,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()));
    redraw();
    EXPECT_EQ(at(0, 0), pattern_px(0));
    feed("Ga=d,d=a");
    redraw();
    EXPECT_EQ(at(0, 0), DEF_BG);
}

/* ── Reply contract ─────────────────────────────────────────────────────── */

TEST_F(KittyNative, ackFollowsTheQuietLevels)
{
    ReplyPipe rp;

    feed("Ga=T,i=11,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()), rp.fd());
    EXPECT_EQ(rp.read(), "\x1b_Gi=11,OK\x1b\\");

    feed("Ga=T,i=12,f=32,w=4,h=4,C=1;" + b64_encode(pattern4x4()), rp.fd());
    EXPECT_EQ(rp.read(), "\x1b_Gi=12,OK\x1b\\");
}

TEST_F(KittyNative, quietOneSwallowsOkButNotErrors)
{
    {
        ReplyPipe rp;
        feed("Ga=T,i=13,f=32,w=4,h=4,q=1,C=1;" + b64_encode(pattern4x4()),
             rp.fd());
        EXPECT_EQ(rp.read(), "");                  /* OK suppressed */
    }
    {
        ReplyPipe rp;
        feed("Ga=p,i=99,q=1", rp.fd());            /* ENOENT is an error */
        EXPECT_EQ(rp.read(), "\x1b_Gi=99,ENOENT\x1b\\");
    }
    {
        ReplyPipe rp;
        feed("Ga=p,i=99,q=2", rp.fd());
        EXPECT_EQ(rp.read(), "");                  /* fully silent */
    }
    {
        ReplyPipe rp;
        feed("Ga=T,i=14,f=999,C=1;" + std::string(16, 'A'), rp.fd());
        EXPECT_EQ(rp.read(), "\x1b_Gi=14,ENOTSUPPORTED\x1b\\");
    }
    {
        ReplyPipe rp;
        feed("Ga=T,i=15,f=32,w=4,h=4,C=1;" + std::string(12, 'A'), rp.fd());
        EXPECT_EQ(rp.read(), "\x1b_Gi=15,EINVAL\x1b\\");   /* size mismatch */
    }
    {
        ReplyPipe rp;
        feed("Ga=T,i=16,f=zz,C=1;" + std::string(4, 'A'), rp.fd());
        EXPECT_EQ(rp.read(), "\x1b_Gi=16,EINVAL\x1b\\");   /* bad value */
    }
    {
        ReplyPipe rp;
        feed("Ga=p", rp.fd());                      /* no id → no ack */
        EXPECT_EQ(rp.read(), "");
    }
}

TEST_F(KittyNative, queryValidatesWithoutDecodingPng)
{
    ReplyPipe rp;

    feed("Ga=q,i=21,f=100;" + std::string(64, 'A'), rp.fd());
    EXPECT_EQ(rp.read(), "\x1b_Gi=21,OK\x1b\\");

    std::string raw(64, '\0');                      /* 4×4×4 = 64 bytes */
    feed("Ga=q,i=22,f=32,w=4,h=4;" + b64_encode(raw), rp.fd());
    EXPECT_EQ(rp.read(), "\x1b_Gi=22,OK\x1b\\");

    feed("Ga=q,i=23,f=32,w=4,h=4;" + b64_encode(std::string(48, '\0')),
         rp.fd());
    EXPECT_EQ(rp.read(), "\x1b_Gi=23,EINVAL\x1b\\");
}

TEST_F(KittyNative, documentedCutsAnswerUnsupported)
{
    {
        ReplyPipe rp;
        feed("Ga=T,U=1,i=31,f=32,w=1,h=1;" + std::string(8, 'A'), rp.fd());
        EXPECT_EQ(rp.read(), "\x1b_Gi=31,ENOTSUPPORTED\x1b\\");
    }
    {
        ReplyPipe rp;
        feed("Gt=t,f=100,i=32;x", rp.fd());         /* file target */
        EXPECT_EQ(rp.read(), "\x1b_Gi=32,ENOTSUPPORTED\x1b\\");
    }
    {
        ReplyPipe rp;
        feed("Ga=T,i=33,p=1,f=32,w=1,h=1;" + std::string(8, 'A'), rp.fd());
        EXPECT_EQ(rp.read(), "\x1b_Gi=33,ENOTSUPPORTED\x1b\\");  /* p>0 */
    }
}

} // namespace
