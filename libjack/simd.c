/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2005-2008 Jussi Laako
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/


#include <config.h>
#include <jack/intsimd.h>

#ifdef USE_DYNSIMD

#ifdef ARCH_X86

int
have_3dnow ()
{
	unsigned int res = 0;

#ifdef __x86_64__
	asm volatile ("pushq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("pushl %%ebx\n\t" : : : "memory");
#endif
	asm volatile (
		"movl $0x80000000, %%eax\n\t" \
		"cpuid\n\t" \
		"cmpl $0x80000001, %%eax\n\t" \
		"jl tdnow_prexit\n\t" \
		\
		"movl $0x80000001, %%eax\n\t" \
		"cpuid\n\t" \
		\
		"xorl %%eax, %%eax\n\t" \
		\
		"movl $1, %%ecx\n\t" \
		"shll $31, %%ecx\n\t" \
		"testl %%ecx, %%edx\n\t" \
		"jz tdnow_testexit\n\t" \
		"movl $1, %%eax\n\t" \
		\
		"movl $1, %%ecx\n\t" \
		"shll $30, %%ecx\n\t" \
		"testl %%ecx, %%edx\n\t" \
		"jz tdnow_testexit\n\t" \
		"movl $2, %%eax\n\t" \
		"jmp tdnow_testexit\n\t" \
		\
		"tdnow_prexit:\n\t" \
		"xorl %%eax, %%eax\n\t" \
		"tdnow_testexit:\n\t"
		: "=a" (res)
		:
		: "ecx", "edx", "memory");
#ifdef __x86_64__
	asm volatile ("popq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("popl %%ebx\n\t" : : : "memory");
#endif
	return res;
}

int
have_sse ()
{
	unsigned int res = 0;

#ifdef __x86_64__
	asm volatile ("pushq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("pushl %%ebx\n\t" : : : "memory");
#endif
	asm volatile (
		"movl $1, %%eax\n\t" \
		"cpuid\n\t" \
		\
		"xorl %%eax, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"shll $25, %%ebx\n\t" \
		"testl %%ebx, %%edx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $1, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"shll $26, %%ebx\n\t" \
		"testl %%ebx, %%edx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $2, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"testl %%ebx, %%ecx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $3, %%eax\n\t" \
		\
		"sse_testexit:\n\t"
		: "=a" (res)
		:
		: "ecx", "edx", "memory");
#ifdef __x86_64__
	asm volatile ("popq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("popl %%ebx\n\t" : : : "memory");
#endif
	return res;
}

void
x86_3dnow_copyf (float *dest, const float *src, int length)
{
	int i, n1, n2;
	pv2sf m64p_src = (pv2sf) src;
	pv2sf m64p_dest = (pv2sf) dest;

	n1 = (length >> 4);
	n2 = ((length & 0xf) >> 1);
	for (i = 0; i < n1; i++)
	{
		asm volatile ("movq %0, %%mm0\n\t"
			: : "m" (*m64p_src++) : "mm0", "memory");
		asm volatile ("movq %0, %%mm1\n\t"
			: : "m" (*m64p_src++) : "mm1", "memory");
		asm volatile ("movq %0, %%mm2\n\t"
			: : "m" (*m64p_src++) : "mm2", "memory");
		asm volatile ("movq %0, %%mm3\n\t"
			: : "m" (*m64p_src++) : "mm3", "memory");
		asm volatile ("movq %0, %%mm4\n\t"
			: : "m" (*m64p_src++) : "mm4", "memory");
		asm volatile ("movq %0, %%mm5\n\t"
			: : "m" (*m64p_src++) : "mm5", "memory");
		asm volatile ("movq %0, %%mm6\n\t"
			: : "m" (*m64p_src++) : "mm6", "memory");
		asm volatile ("movq %0, %%mm7\n\t"
			: : "m" (*m64p_src++) : "mm7", "memory");

		asm volatile ("movq %%mm0, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm0", "memory");
		asm volatile ("movq %%mm1, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm1", "memory");
		asm volatile ("movq %%mm2, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm2", "memory");
		asm volatile ("movq %%mm3, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm3", "memory");
		asm volatile ("movq %%mm4, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm4", "memory");
		asm volatile ("movq %%mm5, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm5", "memory");
		asm volatile ("movq %%mm6, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm6", "memory");
		asm volatile ("movq %%mm7, %0\n\t"
			: "=m" (*m64p_dest++) : : "mm7", "memory");
	}
	for (i = 0; i < n2; i++)
	{
		asm volatile (
			"movq %1, %%mm0\n\t" \
			"movq %%mm0, %0\n\t"
			: "=m" (*m64p_dest++)
			: "m" (*m64p_src++)
			: "mm0", "memory");
	}
	if (length & 0x1)
	{
		asm volatile (
			"movd %1, %%mm0\n\t" \
			"movd %%mm0, %0\n\t"
			: "=m" (dest[length - 1])
			: "m" (src[length - 1])
			: "mm0", "memory");
	}
	asm volatile (
		"femms\n\t" \
		"sfence\n\t");
}

void
x86_3dnow_add2f (float *dest, const float *src, int length)
{
	int i, n;
	pv2sf m64p_dest = (pv2sf) dest;
	pv2sf m64p_src = (pv2sf) src;

	n = (length >> 1);
	for (i = 0; i < n; i++)
	{
		asm volatile (
			"movq %1, %%mm0\n\t" \
			"pfadd %2, %%mm0\n\t" \
			"movq %%mm0, %0\n\t"
			: "=m" (m64p_dest[i])
			: "m0" (m64p_dest[i]),
			  "m" (m64p_src[i])
			: "mm0", "memory");
	}
	if (n & 0x1)
	{
		asm volatile (
			"movd %1, %%mm0\n\t" \
			"movd %2, %%mm1\n\t" \
			"pfadd %%mm1, %%mm0\n\t" \
			"movd %%mm0, %0\n\t"
			: "=m" (dest[length - 1])
			: "m0" (dest[length - 1]),
			  "m" (src[length - 1])
			: "mm0", "mm1", "memory");
	}
	asm volatile (
		"femms\n\t" \
		"sfence\n\t");
}

void
x86_sse_copyf (float *dest, const float *src, int length)
{
	int i, n1, n2, si3;
	pv4sf m128p_src = (pv4sf) src;
	pv4sf m128p_dest = (pv4sf) dest;

	n1 = (length >> 5);
	n2 = ((length & 0x1f) >> 2);
	si3 = (length & ~0x3);
	for (i = 0; i < n1; i++)
	{
		asm volatile ("movaps %0, %%xmm0\n\t"
			: : "m" (*m128p_src++) : "xmm0", "memory");
		asm volatile ("movaps %0, %%xmm1\n\t"
			: : "m" (*m128p_src++) : "xmm1", "memory");
		asm volatile ("movaps %0, %%xmm2\n\t"
			: : "m" (*m128p_src++) : "xmm2", "memory");
		asm volatile ("movaps %0, %%xmm3\n\t"
			: : "m" (*m128p_src++) : "xmm3", "memory");
		asm volatile ("movaps %0, %%xmm4\n\t"
			: : "m" (*m128p_src++) : "xmm4", "memory");
		asm volatile ("movaps %0, %%xmm5\n\t"
			: : "m" (*m128p_src++) : "xmm5", "memory");
		asm volatile ("movaps %0, %%xmm6\n\t"
			: : "m" (*m128p_src++) : "xmm6", "memory");
		asm volatile ("movaps %0, %%xmm7\n\t"
			: : "m" (*m128p_src++) : "xmm7", "memory");

		asm volatile ("movaps %%xmm0, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm0", "memory");
		asm volatile ("movaps %%xmm1, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm1", "memory");
		asm volatile ("movaps %%xmm2, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm2", "memory");
		asm volatile ("movaps %%xmm3, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm3", "memory");
		asm volatile ("movaps %%xmm4, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm4", "memory");
		asm volatile ("movaps %%xmm5, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm5", "memory");
		asm volatile ("movaps %%xmm6, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm6", "memory");
		asm volatile ("movaps %%xmm7, %0\n\t"
			: "=m" (*m128p_dest++) : : "xmm7", "memory");
	}
	for (i = 0; i < n2; i++)
	{
		asm volatile (
			"movaps %1, %%xmm0\n\t" \
			"movaps %%xmm0, %0\n\t"
			: "=m" (*m128p_dest++)
			: "m" (*m128p_src++)
			: "xmm0", "memory");
	}
	for (i = si3; i < length; i++)
	{
		asm volatile (
			"movss %1, %%xmm0\n\t" \
			"movss %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m" (src[i])
			: "xmm0", "memory");
	}
}

void
x86_sse_add2f (float *dest, const float *src, int length)
{
	int i, n, si2;
	pv4sf m128p_src = (pv4sf) src;
	pv4sf m128p_dest = (pv4sf) dest;

	if (__builtin_expect(((long) src & 0xf) || ((long) dest & 0xf), 0))
	{
		/*jack_error("x86_sse_add2f(): non aligned pointers!");*/
		si2 = 0;
		goto sse_nonalign;
	}
	si2 = (length & ~0x3);
	n = (length >> 2);
	for (i = 0; i < n; i++)
	{
		asm volatile (
			"movaps %1, %%xmm0\n\t" \
			"addps %2, %%xmm0\n\t" \
			"movaps %%xmm0, %0\n\t"
			: "=m" (m128p_dest[i])
			: "m0" (m128p_dest[i]),
			  "m" (m128p_src[i])
			: "xmm0", "memory");
	}
sse_nonalign:
	for (i = si2; i < length; i++)
	{
		asm volatile (
			"movss %1, %%xmm0\n\t" \
			"addss %2, %%xmm0\n\t" \
			"movss %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m0" (dest[i]),
			  "m" (src[i])
			: "xmm0", "memory");
	}
}

void x86_sse_f2i (int *dest, const float *src, int length, float scale)
{
	int i;
	static const float max[4] __attribute__((aligned(16))) =
		{ -1.0F, -1.0F, -1.0F, -1.0F };
	static const float min[4] __attribute__((aligned(16))) =
		{ 1.0F, 1.0F, 1.0F, 1.0F };
	float s[4] __attribute__((aligned(16)));

	s[0] = s[1] = s[2] = s[3] = scale;
	asm volatile (
		"movaps %0, %%xmm4\n\t" \
		"movaps %1, %%xmm5\n\t" \
		"movaps %2, %%xmm6\n\t"
		:
		: "m" (*max),
		  "m" (*min),
		  "m" (*s)
		: "xmm4", "xmm5", "xmm6");

	if (__builtin_expect((((long) dest & 0xf) || ((long) src & 0xf)), 0))
		goto sse_nonalign;
	for (i = 0; i < length; i += 4)
	{
		asm volatile (
			"movaps %1, %%xmm1\n\t" \
			"maxps %%xmm4, %%xmm1\n\t" \
			"minps %%xmm5, %%xmm1\n\t" \
			"mulps %%xmm6, %%xmm1\n\t" \
			"cvtps2dq %%xmm1, %%xmm0\n\t" \
			"movdqa %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m" (src[i])
			: "xmm0", "xmm1", "xmm4", "xmm5", "xmm6", "memory");
	}
	return;

sse_nonalign:
	for (i = 0; i < length; i += 4)
	{
		asm volatile (
			"movups %1, %%xmm1\n\t" \
			"maxps %%xmm4, %%xmm1\n\t" \
			"minps %%xmm5, %%xmm1\n\t" \
			"mulps %%xmm6, %%xmm1\n\t" \
			"cvtps2dq %%xmm1, %%xmm0\n\t" \
			"movdqu %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m" (src[i])
			: "xmm0", "xmm1", "xmm4", "xmm5", "xmm6", "memory");
	}
}


void x86_sse_i2f (float *dest, const int *src, int length, float scale)
{
	int i;
	float s[4] __attribute__((aligned(16)));

	s[0] = s[1] = s[2] = s[3] = scale;
	asm volatile (
		"movaps %0, %%xmm4\n\t"
		:
		: "m" (*s)
		: "xmm4" );

	if (__builtin_expect((((long) dest & 0xf) || ((long) src & 0xf)), 0))
		goto sse_nonalign; 
	for (i = 0; i < length; i += 4)
	{
		asm volatile (
			"cvtdq2ps %1, %%xmm0\n\t" \
			"mulps %%xmm4, %%xmm0\n\t" \
			"movaps %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m" (src[i])
			: "xmm0", "xmm4", "memory");
	}
	return;

sse_nonalign:
	for (i = 0; i < length; i += 4)
	{
		asm volatile (
			"movdqu %1, %%xmm1\n\t" \
			"cvtdq2ps %%xmm1, %%xmm0\n\t" \
			"mulps %%xmm4, %%xmm0\n\t" \
			"movups %%xmm0, %0\n\t"
			: "=m" (dest[i])
			: "m" (src[i])
			: "xmm0", "xmm1", "xmm4", "memory");
	}
}

#endif /* ARCH_X86 */

#endif /* USE_DYNSIMD */

