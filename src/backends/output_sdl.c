/*
 * TGS SDL Display Backend
 * Presents ARGB8888 framebuffer via SDL2 texture.
 */
#include "output.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
} sdl_priv;

int output_init(tgs_display *d, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "TGS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        /* Fallback to software renderer */
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return -1;
    }

    SDL_Texture *tex = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return -1;
    }

    sdl_priv *priv = (sdl_priv *)calloc(1, sizeof(sdl_priv));
    if (!priv) {
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return -1;
    }
    priv->window   = win;
    priv->renderer = ren;
    priv->texture  = tex;

    d->width       = width;
    d->height      = height;
    d->bpp         = 32;
    d->stride      = width * 4;
    d->buffer      = NULL; /* LVGL provides the buffer */
    d->backend_priv = priv;

    return 0;
}

int output_resize(tgs_display *d, int width, int height)
{
    sdl_priv *priv = (sdl_priv *)d->backend_priv;
    SDL_Texture *tex;

    if (!priv || width < 1 || height < 1) return -1;
    if (width == d->width && height == d->height) return 0;

    tex = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!tex) return -1;

    SDL_DestroyTexture(priv->texture);
    priv->texture = tex;

    d->width  = width;
    d->height = height;
    d->stride = width * 4;
    return 0;
}

void output_present(tgs_display *d)
{
    sdl_priv *priv = (sdl_priv *)d->backend_priv;
    if (!priv || !d->buffer) return;

    SDL_UpdateTexture(priv->texture, NULL, d->buffer, d->stride);
    SDL_RenderCopy(priv->renderer, priv->texture, NULL, NULL);
    SDL_RenderPresent(priv->renderer);
}

void output_cleanup(tgs_display *d)
{
    sdl_priv *priv = (sdl_priv *)d->backend_priv;
    if (!priv) return;

    if (priv->texture)  SDL_DestroyTexture(priv->texture);
    if (priv->renderer) SDL_DestroyRenderer(priv->renderer);
    if (priv->window)   SDL_DestroyWindow(priv->window);
    SDL_Quit();

    free(priv);
    d->backend_priv = NULL;
}
