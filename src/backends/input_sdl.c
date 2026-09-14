/*
 * TGS SDL Input Backend
 * Polls SDL events, injects keyboard/mouse into LVGL backend.
 */
#include "input.h"

#include <SDL2/SDL.h>

static tgs_backend *g_backend;
static int g_inited;

int input_init(tgs_backend *backend)
{
    g_backend = backend;
    g_inited  = 1;
    return 0;
}

static int map_sdl_key(int sdlkey)
{
    /* ASCII printable range */
    if (sdlkey >= SDLK_a && sdlkey <= SDLK_z)
        return (sdlkey - SDLK_a) + 'a';
    if (sdlkey >= SDLK_0 && sdlkey <= SDLK_9)
        return (sdlkey - SDLK_0) + '0';
    if (sdlkey == SDLK_RETURN || sdlkey == SDLK_KP_ENTER) return 13;
    if (sdlkey == SDLK_ESCAPE)  return 27;
    if (sdlkey == SDLK_BACKSPACE) return 8;
    if (sdlkey == SDLK_TAB)     return 9;
    if (sdlkey == SDLK_SPACE)   return ' ';
    if (sdlkey == SDLK_LEFT)    return 1000;
    if (sdlkey == SDLK_RIGHT)   return 1001;
    if (sdlkey == SDLK_UP)      return 1002;
    if (sdlkey == SDLK_DOWN)    return 1003;
    if (sdlkey == SDLK_HOME)    return 1004;
    if (sdlkey == SDLK_END)     return 1005;
    if (sdlkey == SDLK_DELETE)  return 127;
    return 0;
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
            g_backend->inject_mouse(ev.motion.x, ev.motion.y, 0,
                                    SDL_GetMouseState(NULL, NULL) &
                                    SDL_BUTTON(1) ? 1 : 0);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int btn = 0;
            if (ev.button.button == SDL_BUTTON_LEFT)   btn = 0;
            else if (ev.button.button == SDL_BUTTON_MIDDLE) btn = 1;
            else if (ev.button.button == SDL_BUTTON_RIGHT)  btn = 2;
            g_backend->inject_mouse(ev.button.x, ev.button.y, btn,
                                    ev.type == SDL_MOUSEBUTTONDOWN ? 1 : 0);
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int key = map_sdl_key(ev.key.keysym.sym);
            if (key)
                g_backend->inject_key(key, 0,
                                      ev.type == SDL_KEYDOWN ? 1 : 0);
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
