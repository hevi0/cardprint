/**
 * Everything below this line was basically implented using an LLM.
 * 
 * Took a few iterations to fix some things:
 * 1. Used float for DPI, when one really wants to use int
 * 2. Didn't take into account that the infile and outfile might be the same name.
 *    So, we added support for that using a temporary file underneath. Now
 *    this has some OS-specific dependencies (POSIX and Windows supported).
 * 3. Remove an unnecessary implementation guard.
*/


// png_dpi_util.h
// Header-only utility to update a PNG's DPI (pHYs) without libpng.
// Supports safe in-place updates using a temporary file.
//
// Usage:
//   #include "png_dpi_util.h"
//   update_png_dpi("input.png", "output.png", 300);
//
//   // in-place safe update:
//   update_png_dpi("image.png", "image.png", 300);
//
// Build example:
//   gcc png_dpi_util.h -o update_png_dpi -DPNG_DPI_UTIL_TEST
//   ./update_png_dpi in.png out.png 300

#ifndef PNG_DPI_UTIL_H
#define PNG_DPI_UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
  #define PNG_UNLINK _unlink
#else
  #include <unistd.h>
  #define PNG_UNLINK unlink
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Main API
// Returns 0 on success; nonzero on failure.
int update_png_dpi(const char *in_path, const char *out_path, int dpi);

#ifdef __cplusplus
}
#endif


// ===== Implementation =====
#define PNG_SIG_BYTES 8

static const uint8_t PNG_SIG[PNG_SIG_BYTES] = {
    0x89, 'P','N','G', 0x0D, 0x0A, 0x1A, 0x0A
};

static uint32_t _png_crc_table[256];
static int _png_crc_ready = 0;

static void _png_crc_make_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        _png_crc_table[n] = c;
    }
    _png_crc_ready = 1;
}

static uint32_t _png_crc32(const uint8_t *buf, size_t len, uint32_t crc) {
    if (!_png_crc_ready) _png_crc_make_table();
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = _png_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static inline uint32_t _png_read_u32_be(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

static inline void _png_write_u32_be(uint8_t b[4], uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}

static inline uint32_t _png_dpi_to_ppm(int dpi) {
    if (dpi <= 0) dpi = 1;
    double ppm = dpi * 39.37007874; // pixels per meter
    if (ppm > 4294967295.0) ppm = 4294967295.0;
    return (uint32_t)llround(ppm);
}

static int _png_write_chunk(FILE *out, const char type[4],
                            const uint8_t *data, uint32_t len) {
    uint8_t len_be[4];
    _png_write_u32_be(len_be, len);
    if (fwrite(len_be, 1, 4, out) != 4) return 0;
    if (fwrite(type, 1, 4, out) != 4) return 0;
    if (len > 0 && fwrite(data, 1, len, out) != len) return 0;

    uint32_t crc = _png_crc32((const uint8_t*)type, 4, 0);
    if (len > 0) crc = _png_crc32(data, len, crc);

    uint8_t crc_be[4];
    _png_write_u32_be(crc_be, crc);
    if (fwrite(crc_be, 1, 4, out) != 4) return 0;
    return 1;
}

static int _png_write_pHYs(FILE *out, uint32_t ppm_x, uint32_t ppm_y) {
    uint8_t payload[9];
    _png_write_u32_be(payload + 0, ppm_x);
    _png_write_u32_be(payload + 4, ppm_y);
    payload[8] = 1; // unit = meter
    return _png_write_chunk(out, "pHYs", payload, 9);
}

static void _png_build_temp_path(const char *dest_path, char *buf, size_t bufsz) {
    static unsigned counter = 0;
    unsigned local = ++counter;
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    unsigned pid = (unsigned)getpid();
#endif
    snprintf(buf, bufsz, "%s.tmp-%u-%u", dest_path ? dest_path : "out.png", pid, local);
}

static int _png_replace_file(const char *src, const char *dst) {
#ifdef _WIN32
    if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        PNG_UNLINK(dst);
        if (rename(src, dst) != 0) return 1;
    }
#else
    if (rename(src, dst) != 0) return 1;
#endif
    return 0;
}

// Main function with safe in-place updates.
int update_png_dpi(const char *in_path, const char *out_path, int dpi) {
    if (!in_path || !out_path) return 31;

    char tmp_path[4096];
    _png_build_temp_path(out_path, tmp_path, sizeof(tmp_path));

    FILE *in = fopen(in_path, "rb");
    if (!in) return 1;

    FILE *tmp = fopen(tmp_path, "wb+");
    if (!tmp) { fclose(in); return 2; }

    uint8_t sig[PNG_SIG_BYTES];
    if (fread(sig, 1, PNG_SIG_BYTES, in) != PNG_SIG_BYTES ||
        memcmp(sig, PNG_SIG, PNG_SIG_BYTES) != 0) {
        fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 3;
    }
    if (fwrite(sig, 1, PNG_SIG_BYTES, tmp) != PNG_SIG_BYTES) {
        fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 20;
    }

    uint8_t len_be[4], type[4];
    int wrote_phys = 0;
    uint32_t ppm = _png_dpi_to_ppm(dpi);

    for (;;) {
        if (fread(len_be, 1, 4, in) != 4) break;
        uint32_t len = _png_read_u32_be(len_be);
        if (fread(type, 1, 4, in) != 4) break;

        uint8_t *data = len ? (uint8_t*)malloc(len) : NULL;
        if (len && (!data || fread(data, 1, len, in) != len)) {
            free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 23;
        }

        uint8_t crc_be[4];
        if (fread(crc_be, 1, 4, in) != 4) { free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 24; }

        if (memcmp(type, "pHYs", 4) == 0) {
            free(data);
            continue; // skip old pHYs
        }

        if (!wrote_phys && memcmp(type, "IDAT", 4) == 0) {
            if (!_png_write_pHYs(tmp, ppm, ppm)) {
                fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 29;
            }
            wrote_phys = 1;
        }

        if (fwrite(len_be, 1, 4, tmp) != 4) { free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 25; }
        if (fwrite(type, 1, 4, tmp) != 4)   { free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 26; }
        if (len > 0 && fwrite(data, 1, len, tmp) != len) { free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 27; }
        if (fwrite(crc_be, 1, 4, tmp) != 4) { free(data); fclose(in); fclose(tmp); PNG_UNLINK(tmp_path); return 28; }

        free(data);
        if (memcmp(type, "IEND", 4) == 0) break;
    }

    fclose(in);
    if (fclose(tmp) != 0) { PNG_UNLINK(tmp_path); return 30; }

    if (_png_replace_file(tmp_path, out_path) != 0) {
        PNG_UNLINK(out_path);
        if (rename(tmp_path, out_path) != 0) {
            PNG_UNLINK(tmp_path);
            return 32;
        }
    }

    return 0;
}

#ifdef PNG_DPI_UTIL_TEST
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input.png> <output.png> <dpi>\n", argv[0]);
        return 1;
    }
    int dpi = atoi(argv[3]);
    int rc = update_png_dpi(argv[1], argv[2], dpi);
    if (rc) fprintf(stderr, "Failed (code %d)\n", rc);
    return rc;
}
#endif // PNG_DPI_UTIL_TEST

#endif // PNG_DPI_UTIL_H


