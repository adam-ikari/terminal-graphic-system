/*
 * stb_image — stb single-header library implementation unit.
 * Vendored (upstream v2.30, the copy libsixel carries — which gates the
 * stdint include behind its HAVE_STDINT_H configure macro); TGS only needs
 * PNG decode (the kitty-native receive path), so the other formats are
 * compiled out.
 */
#define STBI_ONLY_PNG
#define HAVE_STDINT_H 1
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
