/*
    Copyright (C) 1999-2000 Paul Davis 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/

#ifndef __jack_memops_h__
#define __jack_memops_h__

#include <jack/types.h>

void sample_move_d32u24_sS           (char *dst, sample_t *src, unsigned long nsamples, unsigned long dst_skip);
void sample_move_d16_sS              (char *dst,  sample_t *src, unsigned long nsamples, unsigned long dst_skip);

void sample_move_dS_s32u24           (sample_t *dst, char *src, unsigned long nsamples, unsigned long src_skip);
void sample_move_dS_s16              (sample_t *dst, char *src, unsigned long nsamples, unsigned long src_skip);

void sample_merge_d16_sS             (char *dst,  sample_t *src, unsigned long nsamples, unsigned long dst_skip);
void sample_merge_d32u24_sS          (char *dst, sample_t *src, unsigned long nsamples, unsigned long dst_skip);

static __inline__ void
sample_merge (sample_t *dst, sample_t *src, unsigned long cnt)

{
	while (cnt--) {
		*dst += *src;
		dst++;
		src++;
	}
}

static __inline__ void
sample_memcpy (sample_t *dst, sample_t *src, unsigned long cnt)

{
	memcpy (dst, src, cnt * sizeof (sample_t));
}

void memset_interleave               (char *dst, char val, unsigned long bytes, unsigned long unit_bytes, unsigned long skip_bytes);
void memcpy_fake                     (char *dst, char *src, unsigned long src_bytes, unsigned long foo, unsigned long bar);

void memcpy_interleave_d16_s16       (char *dst, char *src, unsigned long src_bytes, unsigned long dst_skip_bytes, unsigned long src_skip_bytes);
void memcpy_interleave_d32_s32       (char *dst, char *src, unsigned long src_bytes, unsigned long dst_skip_bytes, unsigned long src_skip_bytes);

void merge_memcpy_interleave_d16_s16 (char *dst, char *src, unsigned long src_bytes, unsigned long dst_skip_bytes, unsigned long src_skip_bytes);
void merge_memcpy_interleave_d32_s32 (char *dst, char *src, unsigned long src_bytes, unsigned long dst_skip_bytes, unsigned long src_skip_bytes);

void merge_memcpy_d16_s16            (char *dst, char *src, unsigned long src_bytes, unsigned long foo, unsigned long bar);
void merge_memcpy_d32_s32            (char *dst, char *src, unsigned long src_bytes, unsigned long foo, unsigned long bar);

#endif /* __jack_memops_h__ */








