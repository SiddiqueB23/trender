#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
    void* data;
    int width;
    int height;
    int color_size;
} texture_t;

// Spread bits of v apart, inserting a 0 between each bit
static uint32_t spread_bits(uint16_t v) {
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FF;
    x = (x | (x << 4)) & 0x0F0F0F0F;
    x = (x | (x << 2)) & 0x33333333;
    x = (x | (x << 1)) & 0x55555555;
    return x;
}

// Morton encode: interleave x and y bits
static uint32_t morton_encode(uint16_t x, uint16_t y) {
    return spread_bits(x) | (spread_bits(y) << 1);
}

// Swizzle a linear RGBA32 texture into Morton order.
//
// tile_size: width (and height) of the Morton tile in pixels, must be power of 2.
//            e.g. 4 = 4x4 tiles, 8 = 8x8 tiles, 0 = full Morton (whole image).
//
// Output is allocated and returned; caller must free().
// Width and height are padded up to the next tile_size multiple internally.
int morton_swizzle(
    texture_t   src,
    uint32_t    tile_size,  // 0 means full Morton over whole image
    texture_t*  dst     // Output pointer
) {
    // For full Morton, tile_size covers the whole image.
    // Use the larger power-of-2 dimension so the tile is square.
	int width = src.width;
	int height = src.height;
    if (tile_size == 0) {
        uint32_t dim = width > height ? width : height;
        // Round up to next power of 2
        dim--;
        dim |= dim >> 1;
        dim |= dim >> 2;
        dim |= dim >> 4;
        dim |= dim >> 8;
        dim |= dim >> 16;
        dim++;
        tile_size = dim;
    }

    assert((tile_size & (tile_size - 1)) == 0 && "tile_size must be power of 2");

    // Pad width and height to tile_size multiples
    uint32_t pad_w = (width + tile_size - 1) & ~(tile_size - 1);
    uint32_t pad_h = (height + tile_size - 1) & ~(tile_size - 1);

    uint32_t tiles_x = pad_w / tile_size;
    uint32_t tiles_y = pad_h / tile_size;
    uint32_t tile_pixels = tile_size * tile_size;

    dst->data = (unsigned char*)calloc(pad_w * pad_h * 4, sizeof(unsigned char));
    if (!dst->data) return -1;
	dst->width = pad_w;
	dst->height = pad_h;

    // Precompute Morton codes for one tile row and column
    // morton_x[i] = spread_bits(i)       for i in [0, tile_size)
    // morton_y[i] = spread_bits(i) << 1  for i in [0, tile_size)
    uint32_t* morton_x = (uint32_t*)malloc(tile_size * sizeof(uint32_t));
    uint32_t* morton_y = (uint32_t*)malloc(tile_size * sizeof(uint32_t));
    if (!morton_x || !morton_y) { free(dst->data); free(morton_x); free(morton_y); return -1; }

    for (uint32_t i = 0; i < tile_size; i++) {
        morton_x[i] = spread_bits((uint16_t)i);
        morton_y[i] = spread_bits((uint16_t)i) << 1;
    }

    for (uint32_t ty = 0; ty < tiles_y; ty++) {
        for (uint32_t tx = 0; tx < tiles_x; tx++) {
            // Base offset of this tile in the destination buffer (in pixels)
            uint32_t tile_base = (ty * tiles_x + tx) * tile_pixels;

            for (uint32_t ly = 0; ly < tile_size; ly++) {
                uint32_t src_y = ty * tile_size + ly;

                for (uint32_t lx = 0; lx < tile_size; lx++) {
                    uint32_t src_x = tx * tile_size + lx;

                    // Morton offset within the tile
                    uint32_t morton_off = morton_x[lx] | morton_y[ly];

                    // Source pixel (0 for out-of-bounds — padding)
                    unsigned char pixel[4] = {0, 0, 0, 0};
                    if (src_x < width && src_y < height)
                        memcpy(pixel, (unsigned char*)src.data + (src_y * width + src_x) * 4, 4);

                    memcpy((unsigned char*)dst->data + (tile_base + morton_off) * 4, pixel, 4);
                }
            }
        }
    }

    free(morton_x);
    free(morton_y);
    return 0;
}

#include <immintrin.h>
#include <stdint.h>

// Precomputed constants for spread_bits
typedef struct {
    __m256i c0, c1, c2, c3;
    __m256i tile_mask;
    __m256i tiles_per_row;
    __m256i tile_pixels;
    int     tile_shift;
} morton_ctx_t;

static inline int ctz_u32(uint32_t x) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, x);
    return (int)idx;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(x);
#else
    // Fallback: De Bruijn sequence
    static const int debruijn_table[32] = {
         0,  1, 28,  2, 29, 14, 24,  3, 30, 22, 20, 15, 25, 17,  4,  8,
        31, 27, 13, 23, 21, 19, 16,  7, 26, 12, 18,  6, 11,  5, 10,  9
    };
    return debruijn_table[((uint32_t)((x & -x) * 0x077CB531U)) >> 27];
#endif
}

static inline morton_ctx_t morton_ctx_init(uint32_t tile_size, uint32_t padded_width) {
    morton_ctx_t ctx;
    ctx.c0 = _mm256_set1_epi32(0x00FF00FF);
    ctx.c1 = _mm256_set1_epi32(0x0F0F0F0F);
    ctx.c2 = _mm256_set1_epi32(0x33333333);
    ctx.c3 = _mm256_set1_epi32(0x55555555);
    ctx.tile_mask = _mm256_set1_epi32(tile_size - 1);
    ctx.tile_shift = ctz_u32(tile_size); // log2(tile_size), valid since power-of-2
    ctx.tiles_per_row = _mm256_set1_epi32(padded_width / tile_size);
    ctx.tile_pixels = _mm256_set1_epi32(tile_size * tile_size);
    return ctx;
}

static inline __m256i spread_bits_epi32(__m256i v, const morton_ctx_t* ctx) {
    v = _mm256_and_si256(_mm256_or_si256(v, _mm256_slli_epi32(v, 8)), ctx->c0);
    v = _mm256_and_si256(_mm256_or_si256(v, _mm256_slli_epi32(v, 4)), ctx->c1);
    v = _mm256_and_si256(_mm256_or_si256(v, _mm256_slli_epi32(v, 2)), ctx->c2);
    v = _mm256_and_si256(_mm256_or_si256(v, _mm256_slli_epi32(v, 1)), ctx->c3);
    return v;
}

// Compute 8 Morton byte-offsets at once given 8 (x, y) pairs.
// Handles all tile sizes:
//   tile_size == 0 or tile_size >= image dims  -> full Morton
//   tile_size == 1                             -> linear (degenerate, no Morton)
//   anything else                              -> Morton within tiles, row-major above
static inline __m256i morton_index_epi32(
    __m256i         tex_x,
    __m256i         tex_y,
    const morton_ctx_t* ctx,
    int             full_morton  // 1 if tile covers whole image
) {
    __m256i index_vec;

    if (full_morton) {
        // Full Morton: spread entire x and y, interleave
        __m256i sx = spread_bits_epi32(tex_x, ctx);
        __m256i sy = spread_bits_epi32(tex_y, ctx);
        index_vec = _mm256_or_si256(sx, _mm256_slli_epi32(sy, 1));
    }
    else {
        // Split into tile index (row-major) + local offset (Morton)

        // Tile indices
        __m256i tile_x = _mm256_srli_epi32(tex_x, ctx->tile_shift);
        __m256i tile_y = _mm256_srli_epi32(tex_y, ctx->tile_shift);

        // Tile base offset = (tile_y * tiles_per_row + tile_x) * tile_pixels
        __m256i tile_base = _mm256_mullo_epi32(
            _mm256_add_epi32(
                _mm256_mullo_epi32(tile_y, ctx->tiles_per_row),
                tile_x),
            ctx->tile_pixels);

        // Local coords within tile
        __m256i lx = _mm256_and_si256(tex_x, ctx->tile_mask);
        __m256i ly = _mm256_and_si256(tex_y, ctx->tile_mask);

        // Morton encode local coords
        __m256i sx = spread_bits_epi32(lx, ctx);
        __m256i sy = spread_bits_epi32(ly, ctx);
        __m256i morton_local = _mm256_or_si256(sx, _mm256_slli_epi32(sy, 1));

        index_vec = _mm256_add_epi32(tile_base, morton_local);
    }

    // Multiply by 4: pixel index -> byte offset (32bpp)
    return _mm256_slli_epi32(index_vec, 2);
}

uint16_t convert_8r8g8b8a_to_5r6g5b(unsigned char r8, unsigned char g8, unsigned char b8) {
    uint16_t r5 = (r8 * 31) / 255;
    uint16_t g6 = (g8 * 63) / 255;
    uint16_t b5 = (b8 * 31) / 255;
    return (r5 << 11) | (g6 << 5) | b5;
}

void convert_8r8g8b8a_to_5r6g5b_texture(texture_t* src) {
	texture_t dst;
    dst.width = src->width;
    dst.height = src->height;
    dst.color_size = 2; // 16 bits per pixel
    dst.data = malloc(dst.width * dst.height * dst.color_size);
    if (!dst.data) return;
    uint32_t pixel_count = dst.width * dst.height;
    for (uint32_t i = 0; i < pixel_count; i++) {
        unsigned char r8 = ((unsigned char*)src->data)[i * 4 + 0];
        unsigned char g8 = ((unsigned char*)src->data)[i * 4 + 1];
        unsigned char b8 = ((unsigned char*)src->data)[i * 4 + 2];
        ((uint16_t*)dst.data)[i] = convert_8r8g8b8a_to_5r6g5b(r8, g8, b8);
    }
	free(src->data);
	*src = dst; // Caller should now use dst as the new texture
}

void convert_5r6g5b_to_8r8g8b8a(uint16_t src, unsigned char* rgba_out) {
    uint16_t r5 = (src >> 11) & 0x1F;
    uint16_t g6 = (src >> 5) & 0x3F;
    uint16_t b5 = src & 0x1F;
    rgba_out[0] = (r5 * 255) / 31;
    rgba_out[1] = (g6 * 255) / 63;
    rgba_out[2] = (b5 * 255) / 31;
    rgba_out[3] = 255; // Opaque alpha
}
#endif // TEXTURES_H