/*
 * Copyright (C) 2025 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef infl_h
#define infl_h

#include "common.h"

/* inflate flags */
#define INFL_ZLIB 1

/*!
 * @brief initialize inflate stream the destination must be a known-size addr
 *
 * @param[in]     dst       uncompressed data (memory addr to unzip)
 * @param[in]     dstlen    size of uncompressed data in bytes
 * @param[in]     flags     pass 1 for zlib header
 *
 * @returns infl stream to use later
 */
UNZ_EXPORT
infl_stream_t*
infl_init(const void * __restrict dst, uint32_t dstlen, int flags);

/*!
 * @brief appends a chunk to unzip stream to uncompress, the chunks may be
 *        separated from each other but can be uncompressed together
 *
 *  this appends uncopressed data into src which specified with unzip_init(), the
 *  subsequent calls will assume that the chunk that include zip header already
 *  included
 *
 * @param[in,out] stream    deflate stream
 * @param[in]     ptr       compressed data (memory addr to unzip)
 * @param[in]     len       size of chunk
 */
UNZ_EXPORT
void
infl_include(infl_stream_t * __restrict stream,
             const void    * __restrict ptr,
             uint32_t                   len);

/*!
 * @brief inflate given deflated content, all included chunks will be inflated
 *        until end,
 *
 *  you must manually call infl_destroy() after this
 *
 * @param[in] stream  deflate stream
 */
UNZ_EXPORT
int
infl(infl_stream_t * __restrict stream);

/*!
 * @brief destroys inflate stream and cleanup
 *
 * @param[in] stream  deflate stream
 */
UNZ_EXPORT
void
infl_destroy(infl_stream_t * __restrict stream);

/*!
 * @brief inflate given deflated content
 *
 * @param[in,out] src       deflate stream: NULL to get created one, stream to
 *                          continue unzipping.
 * @param[in]     srclen    compressed data (memory addr to unzip)
 * @param[in]     dst       size of chunk
 * @param[in]     dstlen    size of chunk
 * @param[in]     flags     pass 1 for zlib header
 */
UNZ_INLINE
int
infl_buf(const void * __restrict src,
         uint32_t                srclen,
         const void * __restrict dst,
         uint32_t                dstlen,
         int                     flags) {
  infl_stream_t *st;
  int            ret;

  if (!(st = infl_init(dst, dstlen, flags))) {
    return -1;
  }

  infl_include(st, src, srclen);
  ret = infl(st);
  infl_destroy(st);

  return ret;
}

UNZ_EXPORT
int
infl_stream(infl_stream_t * __restrict stream,
            const void    * __restrict src,
            uint32_t                   srclen);

#endif /* infl_h */
