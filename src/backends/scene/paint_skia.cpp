/*
 * paint_skia.cpp — Skia paint port (C++).
 *
 * Second reference backend for the drawing-semantics renderer: rasterizes
 * the scene into the same published ARGB8888 framebuffer through a SkCanvas
 * wrapping it. Anti-aliased rounded rects and Skia's font stack (FreeType
 * backend) instead of the SDL2 hand rasterizer. Text: DejaVu via SkTypeface,
 * same fixed size cache as paint_sdl2.
 *
 * Compiled behind TGS_USE_SKIA (CMake option); the build links the vendored
 * static libskia from third_party/skia.
 */
#include "tgs_scene.h"

#include "SkCanvas.h"
#include "SkData.h"
#include "SkFont.h"
#include "SkFontMgr.h"
#include "SkPaint.h"
#include "ports/SkFontMgr_directory.h"
#include "SkRRect.h"
#include "SkSurface.h"
#include "SkTypeface.h"

#include <string.h>

namespace {

struct skia_paint {
    uint32_t *fb;
    int w, h;
    sk_sp<SkSurface> surface;
    SkCanvas *canvas;
    sk_sp<SkTypeface> face;
};

static SkColor to_color(uint32_t c)
{
    return (SkColor)c;  /* 0xAARRGGBB == SkColor layout */
}

void paint_box(tgs_paint *p, int x, int y, int w, int h, int radius,
               uint32_t fill, int has_fill, uint32_t border, int border_w)
{
    skia_paint *pp = (skia_paint *)p->priv;
    if (!pp) return;

    SkRRect rr;
    SkRect rect = SkRect::MakeXYWH((SkScalar)x, (SkScalar)y,
                                   (SkScalar)w, (SkScalar)h);
    SkScalar r = (SkScalar)(radius > 0 ? radius : 0);
    if (r > 0)
        rr.setRectXY(rect, r, r);
    else
        rr = SkRRect::MakeRect(rect);
    SkCanvas *canvas = pp->canvas;

    if (has_fill) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kFill_Style);
        paint.setColor(to_color(fill));
        canvas->drawRRect(rr, paint);
    }
    if (border_w > 0) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth((SkScalar)border_w);
        paint.setColor(to_color(border));
        canvas->drawRRect(rr, paint);
    }
}

void paint_text(tgs_paint *p, int x, int y, int w, int h,
                const char *s, int font_size, uint32_t color, int align)
{
    skia_paint *pp = (skia_paint *)p->priv;
    if (!pp || !s || !s[0]) return;
    if (font_size <= 0) font_size = 14;

    SkFont font(pp->face, (SkScalar)font_size);
    SkRect bounds;
    font.measureText(s, strlen(s), SkTextEncoding::kUTF8, &bounds);
    int tw = (int)bounds.width(), th = (int)bounds.height();
    int tx = x, ty = y + (h - th) / 2;
    if (align == 1) tx = x + (w - tw) / 2;

    SkCanvas *canvas = pp->canvas;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(to_color(color));
    canvas->drawSimpleText(s, strlen(s), SkTextEncoding::kUTF8,
                           (SkScalar)tx, (SkScalar)(ty - bounds.fTop),
                           font, paint);
}

int paint_measure(tgs_paint *p, const char *s, int font_size, int *w, int *h)
{
    skia_paint *pp = (skia_paint *)p->priv;
    (void)pp;
    if (!s) return -1;
    if (font_size <= 0) font_size = 14;
    SkFont font(pp->face, (SkScalar)font_size);
    SkRect bounds;
    font.measureText(s, strlen(s), SkTextEncoding::kUTF8, &bounds);
    if (w) *w = (int)bounds.width();
    if (h) *h = (int)bounds.height();
    return 0;
}

/* Scene paint sequences clip/reset per node (clip before children, reset
 * at frame start), so a save/restore pair maps 1:1 onto the port contract. */
void paint_clip(tgs_paint *p, int x, int y, int w, int h)
{
    skia_paint *pp = (skia_paint *)p->priv;
    if (!pp || !pp->canvas) return;
    if (w <= 0 || h <= 0) {
        pp->canvas->restore();
        pp->canvas->save();
        return;
    }
    pp->canvas->save();
    pp->canvas->clipRect(SkRect::MakeXYWH((SkScalar)x, (SkScalar)y,
                                          (SkScalar)w, (SkScalar)h));
}

void paint_underlay(tgs_paint *p, const uint32_t *px, int w, int h)
{
    skia_paint *pp = (skia_paint *)p->priv;
    if (!pp) return;
    if (!px) {
        memset(pp->fb, 0, (size_t)pp->w * (size_t)pp->h * 4u);
        return;
    }
    size_t n = (size_t)w * (size_t)h < (size_t)pp->w * (size_t)pp->h
                   ? (size_t)w * (size_t)h
                   : (size_t)pp->w * (size_t)pp->h;
    memcpy(pp->fb, px, n * 4u);
}

void paint_deinit(tgs_paint *p)
{
    skia_paint *pp = (skia_paint *)p->priv;
    delete pp;
    p->priv = NULL;
}

}  // namespace

void paint_skia_init(tgs_paint *p, uint32_t *fb, int w, int h)
{
    memset(p, 0, sizeof(*p));
    skia_paint *pp = new skia_paint();
    pp->fb = fb;
    pp->w = w;
    pp->h = h;
    /* DejaVu ships everywhere; the SDL2 port uses the same path. */
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    sk_sp<SkFontMgr> mgr =
        SkFontMgr_New_Custom_Directory("/usr/share/fonts/truetype/dejavu");
    for (int i = 0; paths[i]; i++) {
        if (mgr) {
            pp->face = mgr->makeFromFile(paths[i]);
            if (pp->face) break;
        }
    }
    SkImageInfo info = SkImageInfo::Make(w, h, kBGRA_8888_SkColorType,
                                         kPremul_SkAlphaType);
    pp->surface = SkSurfaces::WrapPixels(info, fb, (size_t)w * 4);
    pp->canvas = pp->surface ? pp->surface->getCanvas() : nullptr;

    p->priv = pp;
    p->box = paint_box;
    p->text = paint_text;
    p->measure = paint_measure;
    p->clip = paint_clip;
    p->underlay = paint_underlay;
    p->deinit = paint_deinit;
}
