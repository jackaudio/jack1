/*
    Copyright (C) 2001 Paul Davis
    Code derived from various headers from the Linux kernel

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

 */

#ifndef __jack_cycles_h__
#define __jack_cycles_h__

/* PowerPC */

#define CPU_FTR_601                     0x00000100
#ifdef __powerpc64__
#define CPU_FTR_CELL_TB_BUG             0x0000800000000000UL
#endif /* __powerpc64__ */

typedef unsigned long cycles_t;

/* For the "cycle" counter we use the timebase lower half. */

extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	cycles_t ret = 0;

#ifdef __powerpc64__
#ifdef ENABLE_CELLBE
	asm volatile (					 \
		"90:	mftb %0;\n"			\
		"97:	cmpwi %0,0;\n"			\
		"	beq- 90b;\n"			\
		"99:\n"					\
		".section __ftr_fixup,\"a\"\n"		\
		".align 3\n"				\
		"98:\n"					\
		"	.llong %1\n"			\
		"	.llong %1\n"			\
		"	.llong 97b-98b\n"		\
		"	.llong 99b-98b\n"		\
		".previous"				\
		: "=r" (ret) : "i" (CPU_FTR_CELL_TB_BUG));
#else   /* !ENABLE_CELLBE */
	__asm__ __volatile__ ("mftb %0" : "=r" (ret));
#endif /* !ENABLE_CELLBE */
#else   /* !__powerpc64__ */
	__asm__ __volatile__ (
		"98:	mftb %0\n"
		"99:\n"
		".section __ftr_fixup,\"a\"\n"
		"	.long %1\n"
		"	.long 0\n"
		"	.long 98b\n"
		"	.long 99b\n"
		".previous"
		: "=r" (ret) : "i" (CPU_FTR_601));
#endif  /* !__powerpc64__ */
	return ret;
}

#endif /* __jack_cycles_h__ */
