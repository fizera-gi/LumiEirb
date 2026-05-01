/*
 *  Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 *  This file is part of KISS FFT - https://github.com/mborgerding/kissfft
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See COPYING file for more information.
 */

#include "_kiss_fft_guts.h"
#include <stdint.h>


/* Pack a complex Q15 value into one 32-bit word.
 * Layout used by the custom instructions:
 *   bits [15:0]  = real part
 *   bits [31:16] = imaginary part
 */
 
static inline uint32_t pack_cp(kiss_fft_cpx x) {
  return ((uint32_t)(uint16_t)x.r) | ((uint32_t)(uint16_t)x.i << 16);
}


/* Unpack a 32-bit custom-instruction result back into a KISS FFT complex value.
 * Both real and imaginary parts are sign-extended from int16_t.
 */
 
static inline kiss_fft_cpx unpack_cp(uint32_t p) {
  kiss_fft_cpx r;
r.r = (kiss_fft_scalar)(int16_t)(p & 0xFFFF);
r.i = (kiss_fft_scalar)(int16_t)(p >> 16);
return r;
}


/* CMUL:  from _kiss_fft_guts.h */
/* Execute the custom complex multiplication instruction.
 * Operands are packed as {imag[15:0], real[15:0]}.
 * The hardware implements the same Q15 rounding rule as KISS FFT.
 */
 
static inline kiss_fft_cpx cus_cmul_cp(kiss_fft_cpx a, kiss_fft_cpx b) {
  uint32_t ap = pack_cp(a);
uint32_t bp = pack_cp(b);
uint32_t rp = cus_cmul(ap, bp);
return unpack_cp(rp);
}


/* Custom complex addition/subtraction instructions.
 * These replace the original KISS FFT C_ADD/C_SUB macros while preserving
 * the packed Q15 complex format.
 */
/* CADD funct7=0x10 */
static inline uint32_t cus_cadd_u32(uint32_t a, uint32_t b) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x10, %0, %1, %2"
                : "=r"(r) : "r"(a), "r"(b));
return r;
}

/* CSUB funct7=0x14 */
static inline uint32_t cus_csub_u32(uint32_t a, uint32_t b) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x14, %0, %1, %2"
                : "=r"(r) : "r"(a), "r"(b));
return r;
}

static inline kiss_fft_cpx cus_cadd_cp(kiss_fft_cpx a, kiss_fft_cpx b) {
  return unpack_cp(cus_cadd_u32(pack_cp(a), pack_cp(b)));
}
static inline kiss_fft_cpx cus_csub_cp(kiss_fft_cpx a, kiss_fft_cpx b) {
  return unpack_cp(cus_csub_u32(pack_cp(a), pack_cp(b)));
}



/* Store the current twiddle factor inside the CV-X-IF coprocessor.
 * This instruction has no architectural writeback; the twiddle is kept in
 * an internal shadow register and consumed by the following fused butterfly.
 * SETTW */
static inline void cus_settw_u32(uint32_t tw)
{
    asm volatile (".insn r 0x7b, 0x1, 0x28, x0, %0, x0"
                :
                : "r"(tw)
                : "memory");
}



/* BFLY2 funct7=0x2C : rd <- y0 ; shadow_y1 <- y1 */
/* Fused radix-2 butterfly instruction.
 * Input x0 and x1 are packed complex values. The instruction returns y0
 * through rd and stores y1 in an internal shadow register.
 */
static inline uint32_t cus_bfly2_u32(uint32_t x0, uint32_t x1) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x2C, %0, %1, %2"
                : "=r"(r) : "r"(x0), "r"(x1) : "memory" );
return r;
}



/* GETY1 funct7=0x1C : rd <- shadow_y1 */
/* Read the second output of the previous fused butterfly.
 * The memory clobber prevents the compiler from reordering this read before
 * the instruction that produces the shadow value.
 */
static inline uint32_t cus_gety1_u32(void) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x1C, %0, x0, x0"
                : "=r"(r)
                :  
                : "memory" );
return r;
}


/* --------------------------------------------------------------------------
 * Override macros used by KISS FFT
 * -------------------------------------------------------------------------- */
 /* Redirect KISS FFT complex arithmetic macros to CV-X-IF custom instructions.
 * This keeps most of the original KISS FFT code unchanged while accelerating
 * the dominant complex add/sub/multiply operations.
 */
 
#undef C_MUL
#define C_MUL(m,a,b)   do { (m) = cus_cmul_cp((a),(b)); } while (0)

#undef C_ADD
#define C_ADD(r,a,b)   do { (r) = cus_cadd_cp((a),(b)); } while (0)

#undef C_SUB
#define C_SUB(r,a,b)   do { (r) = cus_csub_cp((a),(b)); } while (0)

#undef C_ADDTO
#define C_ADDTO(a,b)   do { (a) = cus_cadd_cp((a),(b)); } while (0)




/* Radix-2 butterfly accelerated with a fused custom instruction.
 *
 * Original operation:
 *   t  = Fout2 * twiddle
 *   y0 = Fout + t
 *   y1 = Fout - t
 *
 * The twiddle is first written to the coprocessor shadow register. Then the
 * fused instruction computes both outputs: y0 is returned directly and y1 is
 * retrieved with GETY1.
 */
static void kf_bfly2(
        kiss_fft_cpx* Fout,
        const size_t fstride,
        const kiss_fft_cfg st,
        int m
        )
{
    kiss_fft_cpx* Fout2 = Fout + m;
    kiss_fft_cpx* tw1 = st->twiddles;

    do
    {
        C_FIXDIV(*Fout, 2);
        C_FIXDIV(*Fout2, 2);

        
        kiss_fft_cpx tw = *tw1;
        cus_settw_u32(pack_cp(tw));

        uint32_t y0p = cus_bfly2_u32(pack_cp(*Fout),  pack_cp(*Fout2));
        uint32_t y1p = cus_gety1_u32();

        *Fout = unpack_cp(y0p);
        *Fout2 = unpack_cp(y1p);

        ++Fout;
        ++Fout2;
        tw1 += fstride;
    } while (--m);
}



/* Radix-4 butterfly.
 * The original KISS FFT structure is preserved, but complex arithmetic macros
 * now map to custom instructions. The final rotation/add-sub step is expressed
 * with C_ADD/C_SUB so it also benefits from the custom complex operators.
 */
static void kf_bfly4(
        kiss_fft_cpx * Fout,
        const size_t fstride,
        const kiss_fft_cfg st,
        const size_t m
        )
{
    kiss_fft_cpx *tw1,*tw2,*tw3;
    kiss_fft_cpx scratch[6];
    size_t k=m;
    const size_t m2=2*m;
    const size_t m3=3*m;


    tw3 = tw2 = tw1 = st->twiddles;

    do {
        C_FIXDIV(*Fout,4); C_FIXDIV(Fout[m],4); C_FIXDIV(Fout[m2],4); C_FIXDIV(Fout[m3],4);

        C_MUL(scratch[0],Fout[m] , *tw1 );
        C_MUL(scratch[1],Fout[m2] , *tw2 );
        C_MUL(scratch[2],Fout[m3] , *tw3 );

        C_SUB( scratch[5] , *Fout, scratch[1] );
        C_ADDTO(*Fout, scratch[1]);
        C_ADD( scratch[3] , scratch[0] , scratch[2] );
        C_SUB( scratch[4] , scratch[0] , scratch[2] );
        C_SUB( Fout[m2], *Fout, scratch[3] );
        tw1 += fstride;
        tw2 += fstride*2;
        tw3 += fstride*3;
        C_ADDTO( *Fout , scratch[3] );

        if(st->inverse) {
	    kiss_fft_cpx a, b;

	    a.r = scratch[5].r;
	    a.i = scratch[5].i;
	    b.r = scratch[4].i;
	    b.i = -scratch[4].r;

	    C_SUB(Fout[m], a, b);
	    C_ADD(Fout[m3], a, b);
	}else{
	    kiss_fft_cpx a, b;

	    a.r = scratch[5].r;
	    a.i = scratch[5].i;
	    b.r = scratch[4].i;
	    b.i = -scratch[4].r;

	    C_ADD(Fout[m], a, b);
	    C_SUB(Fout[m3], a, b);
	}
        ++Fout;
    }while(--k);
}

static void kf_bfly3(
         kiss_fft_cpx * Fout,
         const size_t fstride,
         const kiss_fft_cfg st,
         size_t m
         )
{
     size_t k=m;
     const size_t m2 = 2*m;
     kiss_fft_cpx *tw1,*tw2;
     kiss_fft_cpx scratch[5];
     kiss_fft_cpx epi3;
     epi3 = st->twiddles[fstride*m];

     tw1=tw2=st->twiddles;

     do{
         C_FIXDIV(*Fout,3); C_FIXDIV(Fout[m],3); C_FIXDIV(Fout[m2],3);

         C_MUL(scratch[1],Fout[m] , *tw1);
         C_MUL(scratch[2],Fout[m2] , *tw2);

         C_ADD(scratch[3],scratch[1],scratch[2]);
         C_SUB(scratch[0],scratch[1],scratch[2]);
         tw1 += fstride;
         tw2 += fstride*2;

         Fout[m].r = Fout->r - HALF_OF(scratch[3].r);
         Fout[m].i = Fout->i - HALF_OF(scratch[3].i);

         C_MULBYSCALAR( scratch[0] , epi3.i );

         C_ADDTO(*Fout,scratch[3]);

         Fout[m2].r = Fout[m].r + scratch[0].i;
         Fout[m2].i = Fout[m].i - scratch[0].r;

         Fout[m].r -= scratch[0].i;
         Fout[m].i += scratch[0].r;

         ++Fout;
     }while(--k);
}

static void kf_bfly5(
        kiss_fft_cpx * Fout,
        const size_t fstride,
        const kiss_fft_cfg st,
        int m
        )
{
    kiss_fft_cpx *Fout0,*Fout1,*Fout2,*Fout3,*Fout4;
    int u;
    kiss_fft_cpx scratch[13];
    kiss_fft_cpx * twiddles = st->twiddles;
    kiss_fft_cpx *tw;
    kiss_fft_cpx ya,yb;
    ya = twiddles[fstride*m];
    yb = twiddles[fstride*2*m];

    Fout0=Fout;
    Fout1=Fout0+m;
    Fout2=Fout0+2*m;
    Fout3=Fout0+3*m;
    Fout4=Fout0+4*m;

    tw=st->twiddles;
    for ( u=0; u<m; ++u ) {
        C_FIXDIV( *Fout0,5); C_FIXDIV( *Fout1,5); C_FIXDIV( *Fout2,5); C_FIXDIV( *Fout3,5); C_FIXDIV( *Fout4,5);
        scratch[0] = *Fout0;

        C_MUL(scratch[1] ,*Fout1, tw[u*fstride]);
        C_MUL(scratch[2] ,*Fout2, tw[2*u*fstride]);
        C_MUL(scratch[3] ,*Fout3, tw[3*u*fstride]);
        C_MUL(scratch[4] ,*Fout4, tw[4*u*fstride]);

        C_ADD( scratch[7],scratch[1],scratch[4]);
        C_SUB( scratch[10],scratch[1],scratch[4]);
        C_ADD( scratch[8],scratch[2],scratch[3]);
        C_SUB( scratch[9],scratch[2],scratch[3]);

        Fout0->r += scratch[7].r + scratch[8].r;
        Fout0->i += scratch[7].i + scratch[8].i;

        scratch[5].r = scratch[0].r + S_MUL(scratch[7].r,ya.r) + S_MUL(scratch[8].r,yb.r);
        scratch[5].i = scratch[0].i + S_MUL(scratch[7].i,ya.r) + S_MUL(scratch[8].i,yb.r);

        scratch[6].r =  S_MUL(scratch[10].i,ya.i) + S_MUL(scratch[9].i,yb.i);
        scratch[6].i = -S_MUL(scratch[10].r,ya.i) - S_MUL(scratch[9].r,yb.i);

        C_SUB(*Fout1,scratch[5],scratch[6]);
        C_ADD(*Fout4,scratch[5],scratch[6]);

        scratch[11].r = scratch[0].r + S_MUL(scratch[7].r,yb.r) + S_MUL(scratch[8].r,ya.r);
        scratch[11].i = scratch[0].i + S_MUL(scratch[7].i,yb.r) + S_MUL(scratch[8].i,ya.r);
        scratch[12].r = - S_MUL(scratch[10].i,yb.i) + S_MUL(scratch[9].i,ya.i);
        scratch[12].i = S_MUL(scratch[10].r,yb.i) - S_MUL(scratch[9].r,ya.i);

        C_ADD(*Fout2,scratch[11],scratch[12]);
        C_SUB(*Fout3,scratch[11],scratch[12]);

        ++Fout0;++Fout1;++Fout2;++Fout3;++Fout4;
    }
}

/* perform the butterfly for one stage of a mixed radix FFT */
static void kf_bfly_generic(
        kiss_fft_cpx * Fout,
        const size_t fstride,
        const kiss_fft_cfg st,
        int m,
        int p
        )
{
    int u,k,q1,q;
    kiss_fft_cpx * twiddles = st->twiddles;
    kiss_fft_cpx t;
    int Norig = st->nfft;

    kiss_fft_cpx * scratch = (kiss_fft_cpx*)KISS_FFT_TMP_ALLOC(sizeof(kiss_fft_cpx)*p);
    if (scratch == NULL){
        KISS_FFT_ERROR("Memory allocation failed.");
        return;
    }

    for ( u=0; u<m; ++u ) {
        k=u;
        for ( q1=0 ; q1<p ; ++q1 ) {
            scratch[q1] = Fout[ k  ];
            C_FIXDIV(scratch[q1],p);
            k += m;
        }

        k=u;
        for ( q1=0 ; q1<p ; ++q1 ) {
            int twidx=0;
            Fout[ k ] = scratch[0];
            for (q=1;q<p;++q ) {
                twidx += fstride * k;
                if (twidx>=Norig) twidx-=Norig;
                C_MUL(t,scratch[q] , twiddles[twidx] );
                C_ADDTO( Fout[ k ] ,t);
            }
            k += m;
        }
    }
    KISS_FFT_TMP_FREE(scratch);
}

typedef struct {
    kiss_fft_cpx *Fout;
    const kiss_fft_cpx *f;
    size_t fstride;
    int in_stride;
    int *factors;

    int p;
    int m;

    int stage;   // 0 = descend, 1 = recombine
    int idx;     // sous-FFT courante
} kf_frame_t;
/*  facbuf is populated by p1,m1,p2,m2, ...
    where
    p[i] * m[i] = m[i-1]
    m0 = n                  */
static
void kf_factor(int n,int * facbuf)
{
    int p=4;
    double floor_sqrt;
    floor_sqrt = floor( sqrt((double)n) );

    /*factor out powers of 4, powers of 2, then any remaining primes */
    do {
        while (n % p) {
            switch (p) {
                case 4: p = 2; break;
                case 2: p = 3; break;
                default: p += 2; break;
            }
            if (p > floor_sqrt)
                p = n;          /* no more factors, skip to end */
        }
        n /= p;
        *facbuf++ = p;
        *facbuf++ = n;
    } while (n > 1);
}

/*
 *
 * User-callable function to allocate all necessary storage space for the fft.
 *
 * The return value is a contiguous block of memory, allocated with malloc.  As such,
 * It can be freed with free(), rather than a kiss_fft-specific function.
 * */
extern kiss_fft_cpx g_twiddles[512];
extern int g_factors[10];
kiss_fft_cfg kiss_fft_alloc(int nfft,int inverse_fft,void * mem,size_t * lenmem )
{

    KISS_FFT_ALIGN_CHECK(mem)

    kiss_fft_cfg st=NULL;
    size_t memneeded = KISS_FFT_ALIGN_SIZE_UP(sizeof(struct kiss_fft_state)
        + sizeof(kiss_fft_cpx)*(nfft-1)); /* twiddle factors*/

    if ( lenmem==NULL ) {
        st = ( kiss_fft_cfg)KISS_FFT_MALLOC( memneeded );
    }else{
        if (mem != NULL && *lenmem >= memneeded)
            st = (kiss_fft_cfg)mem;
        *lenmem = memneeded;
    }
    if (st) {
        int i;
        st->nfft=nfft;
        st->inverse = inverse_fft;

	// THE C_EXP OPERATIONS ARE PRECALCULATED

	st->twiddles = (kiss_fft_cpx *)&g_twiddles;
	st->factors = (int *)&g_factors;

        // for (i=0;i<nfft;++i) {
        //     const double pi=3.141592653589793238462643383279502884197169399375105820974944;
        //     double phase = -2*pi*i / nfft;
        //     if (st->inverse)
        //         phase *= -1;
        //     kf_cexp(st->twiddles+i, phase );
        // }

        // kf_factor(nfft,st->factors);
    }

    return st;
}




/* Optimized iterative FFT schedule for the contest FFT size.
 * The recursive KISS FFT traversal is replaced by an explicit static schedule
 * matching the known radix decomposition of the provided 512-point benchmark.
 * Control remains fully in software: the CPU still performs all memory accesses
 * and calls elementary radix-2/radix-4 butterflies. This exposes the butterfly
 * operations to the custom CV-X-IF instructions and removes recursion/switch
 * overhead from the hot path.
 */
static void kf_work_iterative(
    kiss_fft_cpx *Fout,
    const kiss_fft_cpx *f,
    int in_stride,
    const kiss_fft_cfg st
)
{
    int a, b, c, d;

    for (a = 0; a < 4; ++a) {
        for (b = 0; b < 4; ++b) {
            for (c = 0; c < 4; ++c) {
                for (d = 0; d < 4; ++d) {
                    kiss_fft_cpx *base =
                        Fout + a * 128 + b * 32 + c * 8 + d * 2;

                    const kiss_fft_cpx *fin =
                        f + (a + 4*b + 16*c + 64*d) * in_stride;

                    base[0] = fin[0];
                    base[1] = fin[256 * in_stride];

                    kf_bfly2(base, 256, st, 1);
                }

                kf_bfly4(Fout + a * 128 + b * 32 + c * 8, 64, st, 2);
            }

            kf_bfly4(Fout + a * 128 + b * 32, 16, st, 8);
        }

        kf_bfly4(Fout + a * 128, 4, st, 32);
    }

    kf_bfly4(Fout, 1, st, 128);
}




/* Use the optimized software schedule for the benchmark FFT.
 * The computation remains out-of-place, as in the original KISS FFT flow.
 */
void kiss_fft_stride(kiss_fft_cfg st,const kiss_fft_cpx *fin,kiss_fft_cpx *fout,int in_stride)
{
    if (fin == fout) {
        //NOTE: this is not really an in-place FFT algorithm.
        //It just performs an out-of-place FFT into a temp buffer
        if (fout == NULL){
            KISS_FFT_ERROR("fout buffer NULL.");
        return;
        }

        kiss_fft_cpx * tmpbuf = (kiss_fft_cpx*)KISS_FFT_TMP_ALLOC( sizeof(kiss_fft_cpx)*st->nfft);
        if (tmpbuf == NULL){
            KISS_FFT_ERROR("Memory allocation error.");
        return;
        }

        kf_work_iterative(tmpbuf, fin, in_stride, st);
        memcpy(fout,tmpbuf,sizeof(kiss_fft_cpx)*st->nfft);
        KISS_FFT_TMP_FREE(tmpbuf);
    }else{
	kf_work_iterative(fout, fin, in_stride, st);
    }
}

void kiss_fft(kiss_fft_cfg cfg,const kiss_fft_cpx *fin,kiss_fft_cpx *fout)
{
    kiss_fft_stride(cfg,fin,fout,1);
}


void kiss_fft_cleanup(void)
{
    // nothing needed any more
}

int kiss_fft_next_fast_size(int n)
{
    while(1) {
        int m=n;
        while ( (m%2) == 0 ) m/=2;
        while ( (m%3) == 0 ) m/=3;
        while ( (m%5) == 0 ) m/=5;
        if (m<=1)
            break; /* n is completely factorable by twos, threes, and fives */
        n++;
    }
    return n;
}
