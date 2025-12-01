/* Thin wrapper around zlib providing a minimal API.
 *
 * This keeps the rest of the code independent from zlib headers, and
 * lets us swap out the backend if desired.
 */

#include "miniz.h"

#include <zlib.h>

int mz_compress2(unsigned char *pDest, size_t *pDest_len,
                 const unsigned char *pSource, size_t source_len,
                 int level)
{
    if (!pDest || !pDest_len) return Z_BUF_ERROR;
    uLongf dest_len = (uLongf)*pDest_len;
    int ret = compress2(pDest, &dest_len,
                        pSource, (uLong)source_len,
                        level);
    *pDest_len = (size_t)dest_len;
    return ret;
}

int mz_uncompress(unsigned char *pDest, size_t *pDest_len,
                  const unsigned char *pSource, size_t source_len)
{
    if (!pDest || !pDest_len) return Z_BUF_ERROR;
    uLongf dest_len = (uLongf)*pDest_len;
    int ret = uncompress(pDest, &dest_len,
                         pSource, (uLong)source_len);
    *pDest_len = (size_t)dest_len;
    return ret;
}

