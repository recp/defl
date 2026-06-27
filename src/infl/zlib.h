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

#ifndef unz_zlib_h
#define unz_zlib_h

#include "../common.h"

UNZ_INLINE
UnzResult
getbyt(defl_chunk_t ** __restrict chunkref,
       uint8_t       * __restrict dst) {
  unz_chunk_t *ch;

  ch = *chunkref;

  if (!ch)
    return UNZ_ERR;

  while ((!ch->p || ch->p >= ch->end) && ch->next) {
    ch        = ch->next;
    *chunkref = ch;
  }

  if (!ch->p || ch->p >= ch->end)
    return UNZ_ERR;

  *dst = *ch->p++;
  return UNZ_OK;
}

UNZ_INLINE
UnzResult
zlib_header(defl_stream_t * __restrict stream,
            defl_chunk_t ** __restrict chunkref,
            bool                       nodict) {
  UnzResult res;
  uint8_t   cmf, cm, cinfo, fdict, flags /*, fcheck, flevel*/;

  (void)stream;
  (void)nodict;

  if ((res = getbyt(chunkref, &cmf)) != UNZ_OK) { return res; }

  cm    = cmf & 0xf;
  cinfo = cmf >> 4;

  if ((res = getbyt(chunkref, &flags)) != UNZ_OK) { return res; }

  fdict  = (flags & 0x20) >> 5;
  /*
  fcheck = flags & 0xf;
  flevel = (flags & 0xe0) >> 5;
   */

  /* validate compression method, 8: DEFLATE, and 32K max window */
  if (cm != 8 || cinfo > 7) {
#ifdef DEBUG
    printf("Error: Unsupported zlib header (CM = %d, CINFO = %d)\n", cm, cinfo);
#endif
    return UNZ_ERR;
  }

  /* validate header checksum (CMF + FLG) % 31 == 0 */
  if ((((uint16_t)cmf << 8) + flags) % 31 != 0) {
#ifdef DEBUG
    printf("Error: Invalid header checksum\n");
    printf("CMF: 0x%x, FLG: 0x%x\n", cmf, flags);
    printf("Checksum validation: ((CMF << 8) + FLG) %% 31 = %d\n",
           ((cmf << 8) + flags) % 31);
#endif
    return UNZ_ERR;
  }

  if (fdict) {
#ifdef DEBUG
    printf("Error: zlib preset dictionaries are not supported\n");
#endif
    return UNZ_ERR;
  }

  return UNZ_OK;
}

#endif /* unz_zlib_h */
