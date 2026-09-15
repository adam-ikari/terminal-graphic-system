/*
 * TGS SDL Input Backend
 * Polls SDL events, injects keyboard/mouse into LVGL backend.
 */
#include "input.h"

#include <SDL2/SDL.h>

/* Canonical TGS key space and modifier mask (docs/navigation.md §D.4) — the
 * same values input_fb.c/evdev emits and lvgl_backend.c translates to LVGL.
 * Printable ASCII maps to itself and 1000+ is reserved for keys without one:
 *   1000 LEFT  1001 RIGHT  1002 UP  1003 DOWN  1004 HOME  1005 END
 * Modifiers, carried in inject_key()'s `mods`; left/right collapse into the
 * class bit (navigation only tests the class, e.g. Shift+Tab):
 *   0x01 SHIFT   0x02 CTRL   0x04 ALT */
#define TGS_KEY_LEFT   1000
#define TGS_KEY_RIGHT  1001
#define TGS_KEY_UP     1002
#define TGS_KEY_DOWN   1003
#define TGS_KEY_HOME   1004
#define TGS_KEY_END    1005

#define TGS_MOD_SHIFT  0x01
#define TGS_MOD_CTRL   0x02
#define TGS_MOD_ALT    0x04

static tgs_backend *g_backend;
static int g_inited;

/* Compositor-owned sink (input.h) — who consumes a key is not the backend's
 * business, so the backend only reports the edge to both consumers. */
static tgs_input_key_sink g_key_sink;
static void *g_key_sink_ud;

static tgs_input_mouse_sink g_mouse_sink;
static void *g_mouse_sink_ud;

void input_set_mouse_sink(tgs_input_mouse_sink sink, void *ud)
{
    g_mouse_sink = sink;
    g_mouse_sink_ud = ud;
}

void input_set_key_sink(tgs_input_key_sink sink, void *ud)
{
    g_key_sink = sink;
    g_key_sink_ud = ud;
}

int input_init(tgs_backend *backend)
{
    g_backend = backend;
    g_inited  = 1;
    return 0;
}

static int sdl_mods(SDL_Keymod km)
{
    int mods = 0;
    if (km & (KMOD_LSHIFT | KMOD_RSHIFT)) mods |= TGS_MOD_SHIFT;
    if (km & (KMOD_LCTRL  | KMOD_RCTRL))  mods |= TGS_MOD_CTRL;
    if (km & (KMOD_LALT   | KMOD_RALT))   mods |= TGS_MOD_ALT;
    return mods;
}

/* SDL reports the unshifted key for printable keys, so Shift is applied here
 * (SDL has no SDLK_A..SDLK_Z: an uppercase letter is 'a' + KMOD_SHIFT). */
static int map_sdl_key(int sdlkey, int shift)
{
    static const char shifted_digits[] = ")!@#$%^&*(";

    if (sdlkey >= SDLK_a && sdlkey <= SDLK_z)
        return (sdlkey - SDLK_a) + (shift ? 'A' : 'a');
    if (sdlkey >= SDLK_0 && sdlkey <= SDLK_9)
        return shift ? shifted_digits[sdlkey - SDLK_0]
                     : (sdlkey - SDLK_0) + '0';

    switch (sdlkey) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return 13;
    case SDLK_ESCAPE:    return 27;
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB:       return 9;
    case SDLK_SPACE:     return ' ';
    case SDLK_DELETE:    return 127;
    case SDLK_LEFT:      return TGS_KEY_LEFT;
    case SDLK_RIGHT:     return TGS_KEY_RIGHT;
    case SDLK_UP:        return TGS_KEY_UP;
    case SDLK_DOWN:      return TGS_KEY_DOWN;
    case SDLK_HOME:      return TGS_KEY_HOME;
    case SDLK_END:       return TGS_KEY_END;
    default:             return 0;
    }
}

void input_poll(void)
{
    if (!g_inited || !g_backend) return;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            SDL_PushEvent(&(SDL_Event){.type = SDL_USEREVENT});
            return;

        case SDL_MOUSEMOTION:
            if (g_mouse_sink) g_mouse_sink(ev.motion.x, ev.motion.y, 0, -1, g_mouse_sink_ud);
            g_backend->inject_mouse(ev.motion.x, ev.motion.y, 0,
                                    (ev.motion.state & SDL_BUTTON_LMASK) ? 1 : 0);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int btn = 0;
            int down = (ev.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
            if (ev.button.button == SDL_BUTTON_LEFT)        btn = 0;
            else if (ev.button.button == SDL_BUTTON_MIDDLE) btn = 1;
            else if (ev.button.button == SDL_BUTTON_RIGHT)  btn = 2;
            if (g_mouse_sink) g_mouse_sink(ev.button.x, ev.button.y, btn, down, g_mouse_sink_ud);
            g_backend->inject_mouse(ev.button.x, ev.button.y, btn, down);
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int mods = sdl_mods(ev.key.keysym.mod);
            int key  = map_sdl_key(ev.key.keysym.sym, mods & TGS_MOD_SHIFT);
            int down = (ev.type == SDL_KEYDOWN) ? 1 : 0;
            if (key) {
                if (g_key_sink) g_key_sink(key, mods, down, g_key_sink_ud);
                g_backend->inject_key(key, mods, down);
            }
            break;
        }

        default:
            break;
        }
    }
}

void input_cleanup(void)
{
    g_inited  = 0;
    g_backend = NULL;
}
