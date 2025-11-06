/**
 * Everything below this line was basically implented using an LLM.
 * 
 * Took a few iterations to fix some things:
 * 1. Used float for DPI, when one really wants to use int
 * 2. Change behavior so that it doesn't output to new file, but does an in-place update.
 * 3. Remove an unnecessary implementation guard.
 * 4. Change from dynamic memory allocation to using a fixed buffer of 32MB, which can be
 *    increased by changing MAX_PNG_SIZE.
*/


// png_dpi_util.h
// Update a PNG's DPI (pHYs) in-place, without libpng or dynamic allocation.
// Reads entire file into a fixed buffer, rewrites same path.
//
// Usage:
//   #include "png_dpi_util.h"
//   update_png_dpi("image.png", 300);
//
// Build (with test main):
//   gcc -std=c11 -Wall -Wextra -pedantic png_dpi_util.h -o update_png_dpi -DPNG_DPI_UTIL_TEST

#ifndef PNG_DPI_UTIL_H
#define PNG_DPI_UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

// Max PNG size we will process (bytes). You can override before including.
#ifndef MAX_PNG_SIZE
#define MAX_PNG_SIZE (32u * 1024u * 1024u)  // 32 MiB
#endif

// API: returns 0 on success; nonzero on failure.
int update_png_dpi(const char *path, int dpi);

// (Optional) legacy 3-arg wrapper for old callsites:
// #define PNG_DPI_UTIL_ENABLE_LEGACY_3ARG
#ifdef PNG_DPI_UTIL_ENABLE_LEGACY_3ARG
static inline int update_png_dpi_legacy(const char *in_path, const char *out_path, int dpi) {
    (void)out_path; return update_png_dpi(in_path, dpi);
}
#endif

#ifdef __cplusplus
}
#endif

// ===== Implementation (header-only) =====
#define PNG_SIG_BYTES 8
static const uint8_t PNG_SIG[PNG_SIG_BYTES] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};

// Fixed global buffers (no heap). OUT has small headroom for inserting a pHYs.
static unsigned char PNG_IN_BUF [MAX_PNG_SIZE];
static unsigned char PNG_OUT_BUF[MAX_PNG_SIZE + 64];  // +64 to accommodate a new pHYs

// CRC32 (same polynomial as libpng/zlib)
static uint32_t _png_crc_table[256];
static int _png_crc_ready = 0;
static void _png_crc_make_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        _png_crc_table[n] = c;
    }
    _png_crc_ready = 1;
}
static uint32_t _png_crc32(const uint8_t *buf, size_t len, uint32_t crc) {
    if (!_png_crc_ready) _png_crc_make_table();
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) crc = _png_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static inline uint32_t _png_read_u32_be(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline void _png_write_u32_be(uint8_t b[4], uint32_t v) {
    b[0]=(uint8_t)(v>>24); b[1]=(uint8_t)(v>>16); b[2]=(uint8_t)(v>>8); b[3]=(uint8_t)v;
}

// Simple round-to-nearest (avoid <math.h>)
static inline uint32_t _png_round_u32(double x) {
    if (x <= 1.0) return 1u;
    if (x >= 4294967295.0) return 4294967295u;
    return (uint32_t)(x + 0.5);
}
// DPI -> pixels-per-meter
static inline uint32_t _png_dpi_to_ppm(int dpi) {
    if (dpi <= 0) dpi = 1;
    const double factor = 39.37007874; // 1/0.0254
    return _png_round_u32(dpi * factor);
}

static int _png_write_chunk(FILE *out, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t len_be[4], crc_be[4];
    _png_write_u32_be(len_be, len);
    if (fwrite(len_be,1,4,out)!=4) return 0;
    if (fwrite(type, 1,4,out)!=4) return 0;
    if (len && fwrite(data,1,len,out)!=len) return 0;
    uint32_t crc = _png_crc32((const uint8_t*)type,4,0);
    if (len) crc = _png_crc32(data,len,crc);
    _png_write_u32_be(crc_be, crc);
    if (fwrite(crc_be,1,4,out)!=4) return 0;
    return 1;
}
static int _png_emit_pHYs_to_mem(uint8_t *dst, size_t dst_cap, size_t *dst_off, uint32_t ppm) {
    // Writes a 21-byte pHYs chunk at dst+*dst_off
    if (*dst_off + 4 + 4 + 9 + 4 > dst_cap) return 0;
    uint8_t *p = dst + *dst_off;
    // length
    _png_write_u32_be(p, 9); p += 4;
    // type "pHYs"
    memcpy(p, "pHYs", 4); p += 4;
    // data: X ppm, Y ppm, unit=1
    _png_write_u32_be(p+0, ppm);
    _png_write_u32_be(p+4, ppm);
    p[8] = 1;
    // crc
    uint32_t crc = _png_crc32((const uint8_t*)"pHYs", 4, 0);
    crc = _png_crc32(p, 9, crc);
    p += 9;
    _png_write_u32_be(p, crc); p += 4;
    *dst_off = (size_t)(p - dst);
    return 1;
}

int update_png_dpi(const char *path, int dpi) {
    if (!path) return 31;

    // Read whole file to IN buffer
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 3; }
    long sz_long = ftell(f);
    if (sz_long < 0) { fclose(f); return 3; }
    size_t in_sz = (size_t)sz_long;
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 3; }
    if (in_sz > MAX_PNG_SIZE) { fclose(f); return 4; }
    if (fread(PNG_IN_BUF, 1, in_sz, f) != in_sz) { fclose(f); return 3; }
    fclose(f);

    // Verify signature
    if (in_sz < PNG_SIG_BYTES || memcmp(PNG_IN_BUF, PNG_SIG, PNG_SIG_BYTES) != 0) return 3;

    // Rebuild into OUT buffer
    size_t out_off = 0;
    memcpy(PNG_OUT_BUF + out_off, PNG_IN_BUF, PNG_SIG_BYTES);
    out_off += PNG_SIG_BYTES;

    const uint8_t *p   = PNG_IN_BUF + PNG_SIG_BYTES;
    const uint8_t *end = PNG_IN_BUF + in_sz;
    int wrote_phys = 0;
    uint32_t ppm = _png_dpi_to_ppm(dpi);

    while (p + 12 <= end) { // need length(4)+type(4)+crc(4) at least
        uint32_t len = _png_read_u32_be(p); p += 4;
        const uint8_t *type = p;            p += 4;
        if ((size_t)(end - p) < len + 4) return 21; // malformed
        const uint8_t *data = p; p += len;
        const uint8_t *crc  = p; p += 4;

        // Drop existing pHYs; we'll write our own once
        if (memcmp(type, "pHYs", 4) == 0) continue;

        // Inject pHYs before first IDAT if not yet written
        if (!wrote_phys && memcmp(type, "IDAT", 4) == 0) {
            if (!_png_emit_pHYs_to_mem(PNG_OUT_BUF, sizeof(PNG_OUT_BUF), &out_off, ppm)) return 22;
            wrote_phys = 1;
        }

        // Copy this chunk verbatim
        if (out_off + 4 + 4 + len + 4 > sizeof(PNG_OUT_BUF)) return 23;
        memcpy(PNG_OUT_BUF + out_off, type - 4, 4 + 4 + len + 4);
        out_off += 4 + 4 + len + 4;

        if (memcmp(type, "IEND", 4) == 0) break;
    }

    // If file had no IDAT (malformed), we didn't inject; that's fine. Otherwise pHYs was injected.

    // Now write OUT buffer back to the same path (truncate+write)
    f = fopen(path, "wb");
    if (!f) return 2;
    if (fwrite(PNG_OUT_BUF, 1, out_off, f) != out_off) { fclose(f); return 24; }
    if (fclose(f) != 0) return 30;
    return 0;
}

#ifdef PNG_DPI_UTIL_TEST
#include <stdio.h> // for demo main only
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image.png> <dpi>\n", argv[0]);
        return 1;
    }
    int dpi = atoi(argv[2]);
    int rc = update_png_dpi(argv[1], dpi);
    if (rc) fprintf(stderr, "Failed (code %d)\n", rc);
    return rc;
}
#endif // PNG_DPI_UTIL_TEST

#endif // PNG_DPI_UTIL_H


