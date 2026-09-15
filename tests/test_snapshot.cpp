/*
 * L0 render snapshots (tui-testing-debugging layer 2).
 *
 * A snapshot here is a contract, not a screenshot. The terminal is driven to a
 * state by a script with nothing uncontrolled in it — no clock, no randomness,
 * no locale, no network, fixed geometry — and the resulting cell grid is
 * compared against a committed file. Characters and effective styles are
 * snapshotted together but listed separately, and colours are the resolved ARGB
 * values rather than the host theme's, so a snapshot does not depend on where it
 * runs.
 *
 * Regenerate with TGS_UPDATE_SNAPSHOTS=1 and review the diff the way you would
 * review any other test change.
 */
#include <gtest/gtest.h>

extern "C" {
#include "term.h"
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TGS_SNAPSHOT_DIR
#define TGS_SNAPSHOT_DIR "tests/snapshots"
#endif

namespace {

const char *shape_name(int shape)
{
    switch (shape) {
    case TGS_CURSOR_UNDERLINE: return "underline";
    case TGS_CURSOR_BAR:       return "bar";
    default:                   return "block";
    }
}

/* A cell is worth stating if it carries a style, or a character the ASCII text
 * line cannot show (a box-drawing cell would otherwise read as a bare dot). */
bool is_default(const tgs_term_cell &c)
{
    return c.fg == TGS_TERM_DEFAULT && c.bg == TGS_TERM_DEFAULT && c.attr == 0 &&
           c.cp < 127;
}

std::string style_of(const tgs_term_cell &c)
{
    char buf[64];
    std::string out;
    if (c.cp >= 127) {
        std::snprintf(buf, sizeof buf, "cp=%04X", (unsigned)c.cp);
        out += buf;
    }
    if (c.fg != TGS_TERM_DEFAULT) {
        std::snprintf(buf, sizeof buf, "fg=%06X", c.fg & 0x00FFFFFFu);
        out += buf;
    }
    if (c.bg != TGS_TERM_DEFAULT) {
        if (!out.empty()) out += ",";
        std::snprintf(buf, sizeof buf, "bg=%06X", c.bg & 0x00FFFFFFu);
        out += buf;
    }
    if (c.attr) {
        std::string a;
        if (c.attr & TGS_ATTR_BOLD)      a += 'B';
        if (c.attr & TGS_ATTR_UNDERLINE) a += 'U';
        if (c.attr & TGS_ATTR_REVERSE)   a += 'R';
        if (!out.empty()) out += ",";
        out += "a=" + a;
    }
    return out;
}

/* Characters, then the styles that differ from the default, coalesced into runs.
 * Anything the terminal renders but does not state here is default by omission,
 * which is what makes the snapshot readable. */
std::string serialize(const tgs_term *t)
{
    int cols = tgs_term_cols(t);
    int rows = tgs_term_rows(t);
    char buf[128];
    std::string out;

    std::snprintf(buf, sizeof buf, "geometry %dx%d\n", cols, rows);
    out += buf;
    std::snprintf(buf, sizeof buf, "cursor %d,%d visible=%d shape=%s\n",
                  tgs_term_cx(t), tgs_term_cy(t),
                  tgs_term_cursor_visible(t) ? 1 : 0,
                  shape_name(tgs_term_cursor_shape(t)));
    out += buf;
    if (tgs_term_scroll_offset(t) > 0) {
        std::snprintf(buf, sizeof buf, "scroll %d\n", tgs_term_scroll_offset(t));
        out += buf;
    }

    for (int y = 0; y < rows; y++) {
        const tgs_term_cell *line = tgs_term_view_line(t, y);
        std::string text;

        for (int x = 0; x < cols; x++) {
            uint32_t cp = line ? line[x].cp : ' ';
            text += (cp >= 32 && cp < 127) ? (char)cp : (cp ? '.' : ' ');
        }

        std::snprintf(buf, sizeof buf, "%2d \"%s\"", y, text.c_str());
        out += buf;

        if (line) {
            for (int x = 0; x < cols; ) {
                if (is_default(line[x])) { x++; continue; }
                std::string desc = style_of(line[x]);
                int end = x;
                while (end + 1 < cols && !is_default(line[end + 1]) &&
                       style_of(line[end + 1]) == desc)
                    end++;
                out += " [" + std::to_string(x) + "-" + std::to_string(end) + "] " + desc;
                x = end + 1;
            }
        }
        out += '\n';
    }
    return out;
}

std::string snapshot_path(const std::string &name)
{
    return std::string(TGS_SNAPSHOT_DIR) + "/" + name + ".txt";
}

/* A snapshot states its own contract: the header records what was frozen and
 * anything deliberate about the result, so a reviewer reads the file rather
 * than the test that produced it. */
void check_snapshot(const std::string &name, const std::string &notes,
                    const std::string &actual)
{
    std::string path = snapshot_path(name);
    std::string text = "# L0 snapshot: " + name + "\n";
    std::istringstream ns(notes);
    std::string line;

    while (std::getline(ns, line)) text += "# " + line + "\n";
    text += actual;

    if (std::getenv("TGS_UPDATE_SNAPSHOTS")) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(f.good()) << "cannot write " << path;
        f << text;
        GTEST_SKIP() << "updated " << path;
    }

    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.good()) << "missing snapshot " << path
                          << " — run with TGS_UPDATE_SNAPSHOTS=1 and review it";
    std::stringstream ss;
    ss << f.rdbuf();

    EXPECT_EQ(ss.str(), text) << "snapshot " << path << " differs";
}

/* Every snapshot uses these, so the frozen inputs are stated once. */
tgs_term *make(int cols, int rows, const char *script)
{
    tgs_term *t = tgs_term_new(cols, rows);
    tgs_term_feed(t, (const uint8_t *)script, (int)std::strlen(script));
    return t;
}

}  // namespace

/* Colours, the attribute set, and reset — the styles a program paints with. */
TEST(L0Snapshot, attributes)
{
    static const char *script =
        "plain\r\n"
        "\x1b[31mRED\x1b[0m \x1b[32mGREEN\x1b[0m \x1b[34mBLUE\x1b[0m\r\n"
        "\x1b[1mBOLD\x1b[0m \x1b[2mFAINT\x1b[0m \x1b[4mUNDER\x1b[0m \x1b[7mREV\x1b[0m\r\n"
        "\x1b[41mBG\r\n"
        "\x1b[0m";

    tgs_term *t = make(24, 5, script);
    check_snapshot("l0-attributes",
                   "frozen: 24x5 ASCII, resolved ARGB, no clock or randomness\n"
                   "colours are the emulator's palette values, not the host theme's\n"
                   "SGR 2 (FAINT) renders at normal intensity: libvterm has no faint bit",
                   serialize(t));
    tgs_term_free(t);
}

/* Cursor addressing, wrapping at the right margin, and erase-to-end-of-line. */
TEST(L0Snapshot, layout)
{
    static const char *script =
        "abcdefghij"           /* exactly the width: no wrap */
        "kl"                   /* wraps to the next row */
        "\x1b[3;4HADDR"        /* CUP is one-based */
        "\x1b[4;1H12345678"    /* a full row, so the erase has something to remove */
        "\x1b[4;5H\x1b[K";     /* EL: erase from the cursor rightwards */

    tgs_term *t = make(10, 5, script);
    check_snapshot("l0-layout",
                   "frozen: 10x5 ASCII\n"
                   "CUP is one-based; wrap happens at the right margin\n"
                   "EL erases from the cursor to the end and leaves the cursor put",
                   serialize(t));
}

/* Box drawing arrives as UTF-8 and must occupy one cell, not one byte. */
TEST(L0Snapshot, box_drawing)
{
    static const char *script =
        "\x1b[2;3H\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90\r\n"  /* ┌─┐ */
        "\x1b[3;3H\xe2\x94\x82 \xe2\x94\x82\r\n"              /* │ │ */
        "\x1b[4;3H\xe2\x94\x94\xe2\x94\x80\xe2\x94\x98";      /* └─┘ */
    tgs_term *t = make(8, 5, script);
    check_snapshot("l0-box-drawing",
                   "frozen: 8x5\n"
                   "box drawing arrives as UTF-8 and occupies one cell each;\n"
                   "the codepoint is stated because the text line is ASCII",
                   serialize(t));
    tgs_term_free(t);
}

/* The alternate screen: a program's full-screen pass must not touch the
 * primary screen, and leaving it must restore what was there. */
TEST(L0Snapshot, alternate_screen)
{
    tgs_term *t = tgs_term_new(12, 3);
    static const char *primary = "primary\r\n";
    static const char *alt = "\x1b[?1049h\x1b[HALT\x1b[2;1H\x1b[?25l";
    tgs_term_feed(t, (const uint8_t *)primary, (int)std::strlen(primary));
    tgs_term_feed(t, (const uint8_t *)alt, (int)std::strlen(alt));
    check_snapshot("l0-alt-screen",
                   "frozen: 12x3\n"
                   "the alternate screen starts blank and the cursor is hidden;\n"
                   "the primary screen is untouched while it is shown",
                   serialize(t));
    tgs_term_free(t);
}

/* Scrollback: what the viewport shows with history above the live screen. */
TEST(L0Snapshot, scrollback)
{
    tgs_term *t = tgs_term_new(8, 2);
    static const char *script = "AAAA\r\nBBBB\r\nCCCC";
    tgs_term_feed(t, (const uint8_t *)script, (int)std::strlen(script));
    tgs_term_scroll(t, 1);
    check_snapshot("l0-scrollback",
                   "frozen: 8x2\n"
                   "viewport scrolled one line back: history first, then the live screen",
                   serialize(t));
    tgs_term_free(t);
}
