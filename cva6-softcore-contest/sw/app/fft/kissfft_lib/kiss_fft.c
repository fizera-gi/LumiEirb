/*
 *  Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 *  This file is part of KISS FFT - https://github.com/mborgerding/kissfft
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See COPYING file for more information.
 */

#include "_kiss_fft_guts.h"
#include <stdint.h>


/* pack {imag[15:0], real[15:0]} */
static inline uint32_t pack_cp(kiss_fft_cpx x) {
  return ((uint32_t)(uint16_t)x.r) | ((uint32_t)(uint16_t)x.i << 16);
}

static inline kiss_fft_cpx unpack_cp(uint32_t p) {
  kiss_fft_cpx r;
r.r = (kiss_fft_scalar)(int16_t)(p & 0xFFFF);
r.i = (kiss_fft_scalar)(int16_t)(p >> 16);
return r;
}

/* CMUL: keep cus_cmul() from _kiss_fft_guts.h */
static inline kiss_fft_cpx cus_cmul_cp(kiss_fft_cpx a, kiss_fft_cpx b) {
  uint32_t ap = pack_cp(a);
uint32_t bp = pack_cp(b);
uint32_t rp = cus_cmul(ap, bp);
return unpack_cp(rp);
}

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

/* SETTW funct7=0x28 (no writeback) */
static inline void cus_settw_u32(uint32_t tw)
{
    asm volatile (".insn r 0x7b, 0x1, 0x28, x0, %0, x0"
                :
                : "r"(tw)
                : "memory");
}

/* BFLY2 funct7=0x2C : rd <- y0 ; shadow_y1 <- y1 */
static inline uint32_t cus_bfly2_u32(uint32_t x0, uint32_t x1) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x2C, %0, %1, %2"
                : "=r"(r) : "r"(x0), "r"(x1));
return r;
}

/* GETY1 funct7=0x1C : rd <- shadow_y1 */
static inline uint32_t cus_gety1_u32(void) {
  uint32_t r;
asm volatile (".insn r 0x7b, 0x1, 0x1C, %0, x0, x0"
                : "=r"(r));
return r;
}

/* --------------------------------------------------------------------------
 * Override macros used by KISS FFT
 * -------------------------------------------------------------------------- */
#undef C_MUL
#define C_MUL(m,a,b)   do { (m) = cus_cmul_cp((a),(b)); } while (0)

#undef C_ADD
#define C_ADD(r,a,b)   do { (r) = cus_cadd_cp((a),(b)); } while (0)

#undef C_SUB
#define C_SUB(r,a,b)   do { (r) = cus_csub_cp((a),(b)); } while (0)

#undef C_ADDTO
#define C_ADDTO(a,b)   do { (a) = cus_cadd_cp((a),(b)); } while (0)



/* The guts header contains all the multiplication and addition macros that are defined for
 fixed or floating point complex numbers.  It also delares the kf_ internal functions.
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

        /* twiddle for this lane */
        kiss_fft_cpx tw = *tw1;
        cus_settw_u32(pack_cp(tw));
        
        //uint32_t twp = pack_cp(*tw1);
        //cus_settw(twp);

        //uint32_t x0p = pack_cp(*Fout);
        //uint32_t x1p = pack_cp(*Fout2);

        uint32_t y0p = cus_bfly2_u32(pack_cp(*Fout),  pack_cp(*Fout2));
        uint32_t y1p = cus_gety1_u32();

        *Fout = unpack_cp(y0p);
        *Fout2 = unpack_cp(y1p);

        ++Fout;
        ++Fout2;
        tw1 += fstride;
    } while (--m);
}
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
            Fout[m].r = scratch[5].r - scratch[4].i;
            Fout[m].i = scratch[5].i + scratch[4].r;
            Fout[m3].r = scratch[5].r + scratch[4].i;
            Fout[m3].i = scratch[5].i - scratch[4].r;
        }else{
            Fout[m].r = scratch[5].r + scratch[4].i;
            Fout[m].i = scratch[5].i - scratch[4].r;
            Fout[m3].r = scratch[5].r - scratch[4].i;
            Fout[m3].i = scratch[5].i + scratch[4].r;
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

static void kf_work_iterative(
    kiss_fft_cpx *Fout,
    const kiss_fft_cpx *f,
    size_t fstride,
    int in_stride,
    int *factors,
    const kiss_fft_cfg st
)
{
    // Safe pour FFT jusqu’à 4096 (radix mixte)
    kf_frame_t stack[32];
    int sp = 0;

    // push frame racine
    stack[sp++] = (kf_frame_t){
        .Fout = Fout,
        .f = f,
        .fstride = fstride,
        .in_stride = in_stride,
        .factors = factors,
        .p = factors[0],
        .m = factors[1],
        .stage = 0,
        .idx = 0
    };

    while (sp > 0) {
        kf_frame_t *fr = &stack[sp - 1];

        if (fr->stage == 0) {

            // Cas terminal : m == 1
            if (fr->m == 1) {
                kiss_fft_cpx *Fo = fr->Fout;
                const kiss_fft_cpx *fi = fr->f;
                const kiss_fft_cpx *Fo_end = Fo + fr->p;

                do {
                    *Fo = *fi;
                    fi += fr->fstride * fr->in_stride;
                } while (++Fo != Fo_end);

                fr->stage = 1;
                continue;
            }

            // Descente dans les sous-FFT
            if (fr->idx < fr->p) {
                int k = fr->idx++;
		if (sp >= 32) {
		    KISS_FFT_ERROR("kf_work_iterative stack overflow");
		    return;
		}
                stack[sp++] = (kf_frame_t){
                    .Fout = fr->Fout + k * fr->m,
                    .f = fr->f + k * fr->fstride * fr->in_stride,
                    .fstride = fr->fstride * fr->p,
                    .in_stride = fr->in_stride,
                    .factors = fr->factors + 2,
                    .p = fr->factors[2],
                    .m = fr->factors[3],
                    .stage = 0,
                    .idx = 0
                };
                continue;
            }

            fr->stage = 1;
        }

        switch (fr->p) {
            case 2: kf_bfly2(fr->Fout, fr->fstride, st, fr->m); break;
            case 3: kf_bfly3(fr->Fout, fr->fstride, st, fr->m); break;
            case 4: kf_bfly4(fr->Fout, fr->fstride, st, fr->m); break;
            case 5: kf_bfly5(fr->Fout, fr->fstride, st, fr->m); break;
            default:
                kf_bfly_generic(fr->Fout, fr->fstride, st, fr->m, fr->p);
                break;
        }

        sp--;
    }
}


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

        //kf_work(tmpbuf,fin,1,in_stride, st->factors,st);
        kf_work_iterative(tmpbuf,fin,1,in_stride, st->factors,st);
        memcpy(fout,tmpbuf,sizeof(kiss_fft_cpx)*st->nfft);
        KISS_FFT_TMP_FREE(tmpbuf);
    }else{
        //kf_work( fout, fin, 1,in_stride, st->factors,st );
        kf_work_iterative(fout,fin,1,in_stride, st->factors,st);

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
