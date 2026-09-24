/*
 * kitty dirty-region presentation round-trip.
 *
 * The presenter's contract is checked end to end, not by inspecting its
 * output string but by replaying what a kitty terminal would do with it:
 * follow the cursor (CUP), decode each transmission (base64 → PNG →
 * pixels), blit it at cell + sub-cell offset, and compare the resulting
 * canvas with the framebuffer pixel for pixel. On top of the pixel truth
 * the bandwidth contract is asserted: a one-tile change costs one tile, an
 * idle frame costs nothing, and without host cell geometry the presenter
 * falls back to full-canvas frames (which still skip idle frames).
 *
 * stdout is redirected to a temp file — that fd doubles as the byte counter
 * (lseek on the shared offset) and as a terminal that reports no cell
 * geometry, so TGS_CELL_W/H is how the tile path is selected here.
 */
#include <gtest/gtest.h>

extern "C" {
#include "output.h"
}

#include <zlib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

/* The presenter under test is the kitty channel; other channels expose the
 * same output_* symbols with different semantics. */
#ifndef TGS_KITTY_OUTPUT
TEST(KittyDirty, NeedsKittyChannel)
{
    GTEST_SKIP() << "built with TGS_OUTPUT != kitty";
}
#else
namespace {

/* Why the last composite() refused, and what it saw — surfaced through the
 * assertion message, because "false" alone says nothing about a protocol. */
std::string g_comp_err;
int g_comp_images = 0;
std::vector<int> g_comp_sizes;   /* decoded image sizes, in order */

#define COMP_FAIL(msg)  do { g_comp_err = (msg); return false; } while (0)

/* ---- byte-level replay of the terminal side ---- */

uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

std::vector<uint8_t> b64_decode(const std::string &in)
{
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    int val = 0, valb = -8;

    for (char ch : in) {
        if (ch == '=' || ch == '\n' || ch == '\r') break;
        const char *p = std::strchr(tbl, ch);
        if (!p) continue;
        val = (val << 6) + (int)(p - tbl);
        valb += 6;
        if (valb >= 0) {
            out.push_back((uint8_t)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

/* Decode the PNG the encoder emits:8-bit RGBA, single IDAT, filter-0 rows.
 * Anything else is a protocol surprise and fails the test rather than being
 * guessed at — the decoder bug history here (filters conflated at 88%
 * mismatch) is exactly why PIL stayed authoritative and this stays strict. */
bool png_decode(const std::vector<uint8_t> &png, int &w, int &h,
                std::vector<uint8_t> &rgba)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> idat;
    size_t i;
    uLongf dst;
    size_t raw_len;

    w = h = 0;
    if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0)
        COMP_FAIL("bad PNG signature");
    for (i = 8; i + 12 <= png.size(); ) {
        uint32_t len = be32(&png[i]);
        const char *type = (const char *)&png[i + 4];
        if (i + 12u + len > png.size()) COMP_FAIL("PNG chunk overruns buffer");
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) COMP_FAIL("IHDR too short");
            w = (int)be32(&png[i + 8]);
            h = (int)be32(&png[i + 12]);
            /* IHDR data: w(8) h(12) depth(16) type(17) comp(18) filt(19)
             * interlace(20). */
            if (png[i + 16] != 8 || png[i + 17] != 6 ||
                png[i + 18] != 0 || png[i + 19] != 0 || png[i + 20] != 0)
                COMP_FAIL("need8-bit RGBA non-interlaced PNG");
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), png.begin() + (long)(i + 8),
                        png.begin() + (long)(i + 8 + len));
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        i += 12u + len;
    }
    if (w <= 0 || h <= 0 || idat.empty()) COMP_FAIL("missing IHDR or IDAT");

    raw_len = (size_t)h * ((size_t)w * 4u + 1u);
    rgba.resize(raw_len);
    dst = (uLongf)raw_len;
    if (uncompress(rgba.data(), &dst, idat.data(), idat.size()) != Z_OK ||
        dst != (uLongf)raw_len)
        COMP_FAIL("zlib inflate failed or wrong length");

    std::vector<uint8_t> out((size_t)w * (size_t)h * 4u);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = &rgba[(size_t)y * ((size_t)w * 4u + 1u)];
        /* The encoder emits filter 0 only — anything else is a surprise,
         * and guessing at filters is how the last harness got88% wrong. */
        if (row[0] != 0) COMP_FAIL("row filter != 0");
        std::memcpy(&out[(size_t)y * (size_t)w * 4u], row + 1,
                    (size_t)w * 4u);
    }
    rgba.swap(out);
    return true;
}

/* Why the last composite() refused, and what it saw — surfaced through the
 * assertion message, because "false" alone says nothing about a protocol. */
std::string kv_get(const std::string &keys, const char *key){
    size_t pos = 0;
    size_t klen = std::strlen(key);

    while (pos <= keys.size()) {
        size_t comma = keys.find(',', pos);
        size_t end = (comma == std::string::npos) ? keys.size() : comma;
        if (end >= pos + klen && keys.compare(pos, klen, key) == 0 &&
            (end == pos + klen || keys[pos + klen] == '='))
            return keys.substr(pos + klen + (end > pos + klen ? 1 : 0),
                               end - pos - klen - (end > pos + klen ? 1 : 0));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return std::string();
}

int kv_int(const std::string &keys, const char *key, int dflt)
{
    std::string v = kv_get(keys, key);
    return v.empty() ? dflt : std::atoi(v.c_str());
}

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

/* Replay a captured stream the way a terminal composites it. The canvas
 * size is given (a tile stream never carries a full-canvas frame first), and
 * every transmission blits clipped into it. */
bool composite(const std::string &s, int cell_w, int cell_h,
               int canvas_w, int canvas_h, Canvas &c)
{
    size_t i = 0;
    int col = 0, row = 0;
    std::string keys, payload;
    int images = 0;

    c.w = canvas_w;
    c.h = canvas_h;
    c.rgb.assign((size_t)canvas_w * (size_t)canvas_h * 3u, 0);
    g_comp_images = 0;
    g_comp_sizes.clear();
    g_comp_err.clear();

    while (i < s.size()) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7E)) j++;
            if (j >= s.size()) COMP_FAIL("truncated CSI sequence");
            if (s[j] == 'H') {                    /* CUP (bare = home) */
                std::string p = s.substr(i + 2, j - i - 2);
                int r = 1, cc = 1;
                if (!p.empty()) {
                    size_t semi = p.find(';');
                    if (semi == std::string::npos) {
                        r = std::atoi(p.c_str());
                    } else {
                        r = std::atoi(p.substr(0, semi).c_str());
                        cc = std::atoi(p.c_str() + semi + 1);
                    }
                }
                row = (r > 0) ? r - 1 : 0;
                col = (cc > 0) ? cc - 1 : 0;
            }
            i = (size_t)j + 1;
            continue;
        }
        if (s[i] == '\x1b' && i + 2 < s.size() && s[i + 1] == '_' &&
            s[i + 2] == 'G') {
            /* APC payload: `keys;payload` — but a keys-only command (our
             * own a=d, and kitty's own `ESC_Ga=d ESC\` in the spec) has no
             * separator at all, so find the ST first. */
            size_t st = s.find("\x1b\\", i + 3);
            if (st == std::string::npos)
                COMP_FAIL("APC without ST at " + std::to_string(i));
            size_t semi = s.find(';', i + 3);
            bool has_payload = (semi != std::string::npos && semi < st);
            std::string chunk_keys = has_payload
                ? s.substr(i + 3, semi - i - 3)
                : s.substr(i + 3, st - i - 3);
            if (has_payload) payload += s.substr(semi + 1, st - semi - 1);
            /* The first chunk of a transmission carries the placement keys,
             * continuations carry only m= — merged, a= tells them apart. */
            if (keys.empty()) keys = chunk_keys;
            else keys += "," + chunk_keys;
            i = st + 2;

            if (kv_int(chunk_keys, "m", 0) != 0) continue;

            if (kv_get(keys, "a") == "T") {
                std::vector<uint8_t> png = b64_decode(payload);
                int w, h;
                std::vector<uint8_t> rgba;
                if (!png_decode(png, w, h, rgba)) return false;
                g_comp_sizes.push_back(w * h);
                int dx = col * cell_w + kv_int(keys, "X", 0);
                int dy = row * cell_h + kv_int(keys, "Y", 0);
                for (int y = 0; y < h; y++) {
                    int cy = dy + y;
                    if (cy < 0 || cy >= c.h) continue;
                    for (int x = 0; x < w; x++) {
                        int cx = dx + x;
                        if (cx < 0 || cx >= c.w) continue;
                        const uint8_t *sp =
                            &rgba[((size_t)y * (size_t)w + (size_t)x) * 4u];
                        uint8_t *dp =
                            &c.rgb[((size_t)cy * (size_t)c.w + (size_t)cx) * 3u];
                        dp[0] = sp[0];
                        dp[1] = sp[1];
                        dp[2] = sp[2];
                    }
                }
                images++;
            }
            keys.clear();
            payload.clear();
            continue;
        }
        i++;
    }
    g_comp_images = images;
    if (images == 0) COMP_FAIL("no a=T transmission in stream");
    return true;
}

/* ---- deterministic noise: compresses badly, so byte counts are real ---- */

void fill_noise(uint8_t *fb, int stride, int x0, int y0, int w, int h,
                uint32_t seed)
{
    uint32_t s = seed;
    for (int y = 0; y < h; y++) {
        uint8_t *row = fb + (size_t)(y0 + y) * (size_t)stride +
                       (size_t)x0 * 4u;
        for (int x = 0; x < w; x++) {
            s = s * 1664525u + 1013904223u;
            row[x * 4 + 0] = (uint8_t)(s & 0xFF);         /* B */
            row[x * 4 + 1] = (uint8_t)((s >> 8) & 0xFF);  /* G */
            row[x * 4 + 2] = (uint8_t)((s >> 16) & 0xFF); /* R */
            row[x * 4 + 3] = 0xFF;                        /* A */
        }
    }
}

int rgb_mismatches(const Canvas &c, const uint8_t *argb, int stride)
{
    int bad = 0;
    for (int y = 0; y < c.h; y++) {
        const uint8_t *row = argb + (size_t)y * (size_t)stride;
        for (int x = 0; x < c.w; x++) {
            const uint8_t *sp = &c.rgb[((size_t)y * (size_t)c.w + (size_t)x) * 3u];
            if (sp[0] != row[x * 4 + 2] || sp[1] != row[x * 4 + 1] ||
                sp[2] != row[x * 4 + 0])
                bad++;
        }
    }
    return bad;
}

/* ---- capture: output_* writes go to a temp file, which doubles as the
 * byte counter (shared offset / pread) and as a terminal that reports no
 * cell geometry. Only the presenter's calls are redirected — assertions and
 * gtest's own reporting must keep the real stdout, or their messages land
 * in the capture and vanish with it. ---- */

class KittyCapture : public ::testing::Test {
protected:
    void SetUp() override
    {
        char tmpl[] = "/tmp/tgs_kitty_dirty_XXXXXX";
        fd_ = mkstemp(tmpl);
        ASSERT_GE(fd_, 0);
        path_ = tmpl;
        setenv("TGS_FPS", "240", 1);   /* 4ms gate; tests pace with sleeps */
        saved_ = dup(STDOUT_FILENO);
        ASSERT_GE(saved_, 0);
        /* Let any present issued by a previous test expire its pacing slot. */
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void TearDown() override
    {
        close(saved_);
        close(fd_);
        if (testing::Test::HasFailure()) {
            fprintf(stderr, "capture kept for inspection: %s\n",
                    path_.c_str());
        } else {
            unlink(path_.c_str());
        }
    }

    /* Run one presenter call with stdout aimed at the capture. */
    template <typename Fn>
    void capture(Fn fn)
    {
        fflush(stdout);
        ASSERT_GE(dup2(fd_, STDOUT_FILENO), 0);
        fn();
        fflush(stdout);
        ASSERT_GE(dup2(saved_, STDOUT_FILENO), 0);
    }

    off_t mark() const { return lseek(fd_, 0, SEEK_CUR); }

    /* Bytes [from, to) of the capture; pread, so it works at any time.
     * The final read is clamped — pread would otherwise hand back a whole
     * block past `to`. */
    std::string slice(off_t from, off_t to) const
    {
        std::string out;
        char buf[8192];
        off_t at = from;
        while (at < to) {
            ssize_t want = (to - at) < (off_t)sizeof(buf)
                               ? (to - at) : (off_t)sizeof(buf);
            ssize_t n = pread(fd_, buf, (size_t)want, at);
            if (n <= 0) break;
            out.append(buf, (size_t)n);
            at += n;
        }
        return out;
    }

    void tick() const
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    int fd_ = -1, saved_ = -1;
    std::string path_;
};

}  // namespace

/* The tile path: first frame carries the grid, then only the tile that
 * changes crosses the wire, then nothing at all while the canvas is still —
 * and the composited result is the framebuffer, pixel for pixel. */
TEST_F(KittyCapture, TilesCarryOnlyWhatChanged)
{
    const int W = 320, H = 200, CELL_W = 10, CELL_H = 20;
    tgs_display d;
    std::vector<uint8_t> expected;

    setenv("TGS_CELL_W", "10", 1);
    setenv("TGS_CELL_H", "20", 1);
    bool ok = false;
    capture([&] { ok = output_init(&d, W, H) == 0; });
    ASSERT_TRUE(ok) << "output_init failed";
    fill_noise(d.buffer, d.stride, 0, 0, W, H, 0xC0FFEEu);

    off_t o0 = mark();
    capture([&] { output_present(&d); });                    /* first frame: the whole grid */
    off_t o1 = mark();
    ASSERT_GT(o1, o0);
    const off_t first = o1 - o0;

    tick();
    fill_noise(d.buffer, d.stride, 128, 64, 64, 64, 0xDEADBEEFu);
    capture([&] { output_present(&d); });                    /* exactly one tile is dirty */
    off_t o2 = mark();
    const off_t delta = o2 - o1;
    EXPECT_GT(delta, 0);
    /* One 64×64 tile of noise ≈ 16KB raw → well under a third of a
     * 5×4-tile grid and under 32KB even at worst-case deflate ratio. */
    EXPECT_LE(delta, 32 * 1024);
    EXPECT_LT(delta, first / 3);

    tick();
    capture([&] { output_present(&d); });                    /* nothing changed: idle frame */
    EXPECT_EQ(mark(), o2) << "an idle frame must write zero bytes";

    expected.assign(d.buffer, d.buffer + (size_t)d.stride * H);
    capture([&] { output_cleanup(&d); });

    Canvas c;
    ASSERT_TRUE(composite(slice(0, o2), CELL_W, CELL_H, W, H, c))
        << "stream did not decode into a canvas: " << g_comp_err
        << " (images=" << g_comp_images << ")";
    ASSERT_EQ(c.w, W);
    ASSERT_EQ(c.h, H);
    EXPECT_EQ(rgb_mismatches(c, expected.data(), W * 4), 0)
        << "composited canvas differs from the framebuffer";
    /* Evidence in the failure message: how many transmissions the two
     * presents produced (expect grid + 1 dirty tile). */
    EXPECT_EQ(g_comp_images, 21) << "images=" << g_comp_images;
}

/* No cell geometry (stdout is a file: TIOCGWINSZ fails) → full-canvas
 * frames, placed by the home cursor with no offsets. Idle frames are still
 * free. */
TEST_F(KittyCapture, FullFrameFallbackWithoutCellGeometry)
{
    const int W = 320, H = 200;
    tgs_display d;
    std::vector<uint8_t> expected;

    unsetenv("TGS_CELL_W");
    unsetenv("TGS_CELL_H");
    bool ok = false;
    capture([&] { ok = output_init(&d, W, H) == 0; });
    ASSERT_TRUE(ok) << "output_init failed";
    fill_noise(d.buffer, d.stride, 0, 0, W, H, 0x12345678u);

    off_t o0 = mark();
    capture([&] { output_present(&d); });
    off_t o1 = mark();
    ASSERT_GT(o1, o0);

    tick();
    fill_noise(d.buffer, d.stride, 10, 10, 50, 30, 0xABCDEF01u);
    capture([&] { output_present(&d); });
    off_t o2 = mark();
    /* A50×30 change still costs a full canvas here — that is exactly the
     * cost the tile path exists to avoid, so it must be visible as such:
     * the retransmission is the same order of bytes as the first frame. */
    EXPECT_GT(o2 - o1, (o1 - o0) / 2);

    tick();
    capture([&] { output_present(&d); });
    EXPECT_EQ(mark(), o2) << "an idle frame must write zero bytes";

    expected.assign(d.buffer, d.buffer + (size_t)d.stride * H);
    capture([&] { output_cleanup(&d); });

    Canvas c;
    ASSERT_TRUE(composite(slice(0, o2), W, H, W, H, c))
        << "stream did not decode into a canvas: " << g_comp_err
        << " (images=" << g_comp_images << ")";
    ASSERT_EQ(c.w, W);
    ASSERT_EQ(c.h, H);
    EXPECT_EQ(rgb_mismatches(c, expected.data(), W * 4), 0);
}
#endif /* TGS_KITTY_OUTPUT */
