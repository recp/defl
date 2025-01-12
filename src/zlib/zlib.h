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

  /* empty chnuk is not allowed */
  if (ch->p == ch->end) {
    ch        = ch->next;
    *chunkref = ch;
  }

  if (ch->p) {
    *dst        = *ch->p++;
    ch->bitpos += 8;
    return UNZ_OK;
  } else {
    return UNZ_ERR;
  }
}

UNZ_INLINE
UnzResult
zlib_header(defl_stream_t * __restrict stream,
            defl_chunk_t ** __restrict chunkref,
            bool                       nodict) {
  UnzResult res;
  uint8_t   cmf, cm, cinfo, fcheck, fdict, flevel, flags;

  /**
   * nodict: PNG spec doesnt allow dict so give a chance to skip fdict and fdict
   *         errors.
   */
  if ((res = getbyt(chunkref, &cmf)) != UNZ_OK) { return res; }

  cm    = cmf & 0xf;
  cinfo = cmf >> 4;

  if ((res = getbyt(chunkref, &flags)) != UNZ_OK) { return res; }

  fcheck = flags & 0xf;
  fdict  = (flags & 0x10) >> 4;
  flevel = (flags & 0xe0) >> 5;

  /* validate compression method, 8: DEFLATE */
  if (cm != 8) {
#if DEBUG
    printf("Error: Unsupported compression method (CM = %d)\n", cm);
#endif
    return UNZ_ERR;
  }

  /* validate header checksum (CMF + FLG) % 31 == 0 */
  if ((((uint16_t)cmf << 8) + flags) % 31 != 0) {
#if DEBUG
    printf("Error: Invalid header checksum\n");
    printf("CMF: 0x%x, FLG: 0x%x\n", cmf, flags);
    printf("Checksum validation: ((CMF << 8) + FLG) %% 31 = %d\n",
           ((cmf << 8) + flags) % 31);
#endif

    if (!nodict)
      return UNZ_ERR;
  }

  /* Handle preset dictionaries */
  if (!nodict && fdict) {
    uint8_t  dictbyt[4];
    uint32_t dictid;

    if ((res = getbyt(chunkref, &dictbyt[0])) != UNZ_OK) { return res; }
    if ((res = getbyt(chunkref, &dictbyt[1])) != UNZ_OK) { return res; }
    if ((res = getbyt(chunkref, &dictbyt[2])) != UNZ_OK) { return res; }
    if ((res = getbyt(chunkref, &dictbyt[3])) != UNZ_OK) { return res; }

    dictid = (dictbyt[0]<<24)|(dictbyt[1]<<16)|(dictbyt[2]<<8)|dictbyt[3];
#if DEBUG
    printf("FDICT set. Dictionary ID: 0x%x\n", dictid);
#endif
  }

  return UNZ_OK;
}

#endif /* unz_zlib_h */
