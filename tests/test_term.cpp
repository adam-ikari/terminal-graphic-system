/*
 * Terminal emulator and one-stream demultiplexer tests.
 *
 * These pin the behaviour a character program actually depends on: cursor
 * addressing, colour, wrap at the right margin, the alternate screen, and the
 * rule that TGS frames are stripped from the character stream. Every case is
 * a contract a real program (a shell, htop, vim) relies on, not an internal
 * detail of the parser.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern "C" {
#include "parser.h"
#include "term.h"
}

namespace {

tgs_term *make_term(int cols = 20, int rows = 5)
{
    return tgs_term_new(cols, rows);
}

void feed(tgs_term *t, const char *s)
{
    tgs_term_feed(t, (const uint8_t *)s, (int)std::strlen(s));
}

const tgs_term_cell *cell(const tgs_term *t, int x, int y)
{
    return &tgs_term_cells(t)[y * tgs_term_cols(t) + x];
}

std::string row_text(const tgs_term *t, int row)
{
    const tgs_term_cell *cells = tgs_term_cells(t);
    int cols = tgs_term_cols(t);
    std::string out;
    for (int x = 0; x < cols; x++) {
        uint32_t cp = cells[row * cols + x].cp;
        out += (cp != 0 && cp < 127) ? (char)cp : ' ';
    }
    return out;
}

} // namespace

TEST(Term, wraps_at_the_right_margin_without_skipping_a_column)
{
    tgs_term *t = make_term(4, 2);
    feed(t, "abcdef");
    EXPECT_EQ(row_text(t, 0), "abcd");
    EXPECT_EQ(row_text(t, 1), "ef  ");
    EXPECT_EQ(tgs_term_cy(t), 1);
    tgs_term_free(t);
}

TEST(Term, cup_addresses_the_cursor_one_based)
{
    tgs_term *t = make_term(10, 4);
    feed(t, "\x1b[3;4HX");
    EXPECT_EQ(cell(t, 3, 2)->cp, 'X');
    EXPECT_EQ(tgs_term_cx(t), 4);
    EXPECT_EQ(tgs_term_cy(t), 2);
    tgs_term_free(t);
}

TEST(Term, sgr_applies_a_colour_and_sgr_0_clears_it)
{
    tgs_term *t = make_term(10, 2);
    uint32_t fg;

    feed(t, "\x1b[31mA\x1b[0mB\x1b[7mC");

    fg = cell(t, 0, 0)->fg;
    EXPECT_NE(fg, TGS_TERM_DEFAULT);                    /* a colour was applied */
    EXPECT_NE(fg, cell(t, 0, 0)->bg);                   /* and it is not the background */
    EXPECT_GT((fg >> 16) & 0xFFu, (fg >> 8) & 0xFFu);   /* red-dominant: SGR 31 */
    EXPECT_GT((fg >> 16) & 0xFFu, fg & 0xFFu);

    EXPECT_EQ(cell(t, 0, 0)->attr, 0);
    EXPECT_EQ(cell(t, 1, 0)->fg, TGS_TERM_DEFAULT);     /* SGR 0 restores the default */
    EXPECT_TRUE(cell(t, 2, 0)->attr & TGS_ATTR_REVERSE);
    tgs_term_free(t);
}

TEST(Term, erase_to_end_of_line_clears_exactly_the_tail)
{
    tgs_term *t = make_term(6, 2);
    feed(t, "abcdef\x1b[1;3H\x1b[K");
    EXPECT_EQ(row_text(t, 0), "ab    ");
    tgs_term_free(t);
}

TEST(Term, alternate_screen_starts_blank_and_restores_the_primary)
{
    tgs_term *t = make_term(10, 3);
    feed(t, "main");
    feed(t, "\x1b[?1049h\x1b[H");   /* switch screens, then home — as a real program does */
    EXPECT_EQ(row_text(t, 0), "          ");
    feed(t, "alt");
    EXPECT_EQ(row_text(t, 0), "alt       ");
    feed(t, "\x1b[?1049l");
    EXPECT_EQ(row_text(t, 0), "main      ");
    tgs_term_free(t);
}

TEST(Term, cursor_visibility_is_tracked)
{
    tgs_term *t = make_term(5, 2);
    feed(t, "\x1b[?25l");
    EXPECT_EQ(tgs_term_cursor_visible(t), 0);
    feed(t, "\x1b[?25h");
    EXPECT_EQ(tgs_term_cursor_visible(t), 1);
    tgs_term_free(t);
}

TEST(Term, utf8_box_drawing_is_one_cell)
{
    tgs_term *t = make_term(5, 2);
    feed(t, "\xe2\x94\x8c");   /* U+250C ┌ */
    EXPECT_EQ(cell(t, 0, 0)->cp, 0x250Cu);
    EXPECT_EQ(tgs_term_cx(t), 1);
    tgs_term_free(t);
}

TEST(Term, scrolling_region_leaves_lines_outside_it_untouched)
{
    tgs_term *t = make_term(6, 4);
    /* A line break in the byte stream is CR LF: LF alone moves the row and
     * leaves the column where it was, which is what the emulator must do. */
    feed(t, "1\r\n2\r\n3\r\n4");
    feed(t, "\x1b[2;4r");    /* scroll region rows 2..4 */
    feed(t, "\x1b[4;1H\n");  /* newline at the bottom of the region */
    EXPECT_EQ(row_text(t, 0), "1     ");
    EXPECT_EQ(row_text(t, 1), "3     ");
    EXPECT_EQ(row_text(t, 2), "4     ");
    EXPECT_EQ(row_text(t, 3), "      ");
    tgs_term_free(t);
}

/* --- one stream: character output and TGS frames ---------------------- */

namespace {

struct Demux {
    std::string text;
    int frames = 0;
};

void on_text(const uint8_t *data, int len, void *ud)
{
    static_cast<Demux *>(ud)->text.append((const char *)data, (size_t)len);
}

void on_frame(const tgs_frame *, void *ud)
{
    static_cast<Demux *>(ud)->frames++;
}

void demux_feed(tgs_parser *p, const std::string &s)
{
    tgs_parser_feed(p, (const uint8_t *)s.data(), (int)s.size());
}

} // namespace

TEST(StreamDemux, plain_output_is_all_text)
{
    Demux d;
    tgs_parser p;
    tgs_parser_init(&p, on_frame, &d);
    tgs_parser_set_text_cb(&p, on_text, &d);

    demux_feed(&p, "hello world");
    EXPECT_EQ(d.text, "hello world");
    EXPECT_EQ(d.frames, 0);
}

TEST(StreamDemux, frame_bytes_never_leak_into_the_text_stream)
{
    Demux d;
    tgs_parser p;
    tgs_parser_init(&p, on_frame, &d);
    tgs_parser_set_text_cb(&p, on_text, &d);

    demux_feed(&p, "ab\x1b_TGS;0;1;1;1.0;x\x1b\\cd");
    EXPECT_EQ(d.text, "abcd");
}

TEST(StreamDemux, a_frame_split_across_reads_is_never_emitted_as_text)
{
    Demux d;
    tgs_parser p;
    tgs_parser_init(&p, on_frame, &d);
    tgs_parser_set_text_cb(&p, on_text, &d);

    demux_feed(&p, "ab\x1b_TGS;0;1;1;1.0");
    EXPECT_EQ(d.text, "ab");   /* the partial frame is held back, not printed */
    demux_feed(&p, ";x\x1b\\cd");
    EXPECT_EQ(d.text, "abcd");
}
