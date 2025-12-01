/* Minimal compression API used by this project.
 *
 * This header declares a tiny wrapper around zlib (implemented in miniz.c)
 * so the rest of the code can call mz_compress2/mz_uncompress without
 * depending directly on zlib headers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {

/* Compression levels: 0-9 like zlib. */
int mz_compress2(unsigned char *pDest, size_t *pDest_len,
                 const unsigned char *pSource, size_t source_len,
                 int level);

int mz_uncompress(unsigned char *pDest, size_t *pDest_len,
                  const unsigned char *pSource, size_t source_len);

} // extern "C"



