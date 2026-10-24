// The average colour of a texture in the GameCube's own format (port_puddles.h's
// port_texture_average): Sky in Reflections measures the stage's sky with it as the stage loads.
// The graphics layer's decoder (platform/gx/src/gx_texture.cpp) turns it into RGBA first.
#include "port_puddles.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace gx {
void decodeTexture(const uint8_t* src, uint32_t fmt, uint32_t w, uint32_t h, const uint8_t* tlut, uint32_t tlutFmt,
                   uint8_t* out);
}

extern "C" int port_texture_average(const void* data, unsigned fmt, unsigned w, unsigned h, const void* tlut,
                                    unsigned tlutFmt, unsigned char* rgb, unsigned* weight)
{
    *weight = 0;
    if (!data || !w || !h || w > 4096 || h > 4096) return 0;
    std::vector<uint8_t> pixels(size_t(w) * h * 4);
    gx::decodeTexture((const uint8_t*)data, fmt, w, h, (const uint8_t*)tlut, tlutFmt, pixels.data());
    // each pixel as much as it shows (its alpha)
    uint64_t r = 0, g = 0, b = 0, a = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned alpha = pixels[i + 3];
        r += uint64_t(pixels[i]) * alpha;
        g += uint64_t(pixels[i + 1]) * alpha;
        b += uint64_t(pixels[i + 2]) * alpha;
        a += alpha;
    }
    if (!a) return 0;
    rgb[0] = (unsigned char)(r / a);
    rgb[1] = (unsigned char)(g / a);
    rgb[2] = (unsigned char)(b / a);
    *weight = (unsigned)(a / 255 ? a / 255 : 1);
    return 1;
}
