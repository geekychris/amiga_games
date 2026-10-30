// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * os4_compat: the Amiga custom chip registers as an ordinary structure in
 * RAM (see os4_compat.h). This header takes the place of the SDK's
 * <hardware/custom.h> for the ported games: the same registers at the
 * same offsets, followed by the queue that writes to dmacon go into
 * (os4_compat.h turns `x.dmacon = v` into a write to the next queue slot,
 * so the Paula emulation sees every write, in order).
 */
#ifndef HARDWARE_CUSTOM_H
#define HARDWARE_CUSTOM_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(2)

struct AudChannel {
    UWORD *ac_ptr;      /* sample start */
    UWORD  ac_len;      /* length in words */
    UWORD  ac_per;      /* period */
    UWORD  ac_vol;      /* volume 0..64 */
    UWORD  ac_dat;
    UWORD  ac_pad[2];
};

struct SpriteDef {
    UWORD pos, ctl, dataa, datab;
};

#define OC_DMACON_QUEUE 64

struct Custom {
    UWORD bltddat, dmaconr, vposr, vhposr, dskdatr, joy0dat, joy1dat, clxdat;  /* $000 */
    UWORD adkconr, pot0dat, pot1dat, potinp, serdatr, dskbytr, intenar, intreqr; /* $010 */
    APTR  dskpt;                                                                 /* $020 */
    UWORD dsklen, dskdat, refptr, vposw, vhposw, copcon, serdat, serper;         /* $024 */
    UWORD potgo, joytest, strequ, strvbl, strhor, strlong;                       /* $034 */
    UWORD bltcon0, bltcon1, bltafwm, bltalwm;                                    /* $040 */
    APTR  bltcpt, bltbpt, bltapt, bltdpt;                                        /* $048 */
    UWORD bltsize;                                                               /* $058 */
    UBYTE pad2d, bltcon0l;
    UWORD bltsizv, bltsizh;                                                      /* $05c */
    UWORD bltcmod, bltbmod, bltamod, bltdmod;                                    /* $060 */
    UWORD pad34[4];
    UWORD bltcdat, bltbdat, bltadat;                                             /* $070 */
    UWORD pad3b[3];
    UWORD deniseid, dsksync;                                                     /* $07c */
    ULONG cop1lc, cop2lc;                                                        /* $080 */
    UWORD copjmp1, copjmp2, copins, diwstrt, diwstop, ddfstrt, ddfstop;          /* $088 */
    UWORD dmacon;                                                                /* $096 */
    UWORD clxcon, intena, intreq, adkcon;                                        /* $098 */
    struct AudChannel aud[4];                                                    /* $0a0 */
    APTR  bplpt[8];                                                              /* $0e0 */
    UWORD bplcon0, bplcon1, bplcon2, bplcon3, bpl1mod, bpl2mod, bplcon4, clxcon2; /* $100 */
    UWORD bpldat[8];                                                             /* $110 */
    APTR  sprpt[8];                                                              /* $120 */
    struct SpriteDef spr[8];                                                     /* $140 */
    UWORD color[32];                                                             /* $180 */
    UWORD htotal, hsstop, hbstrt, hbstop, vtotal, vsstop, vbstrt, vbstop;        /* $1c0 */
    UWORD sprhstrt, sprhstop, bplhstrt, bplhstop, hhposw, hhposr, beamcon0;      /* $1d0 */
    UWORD hsstrt, vsstrt, hcenter, diwhigh;                                      /* $1de */
    UWORD padf3[11];
    UWORD fmode;                                                                 /* $1fc */
    UWORD pad_end;
    /* not a register: the dmacon writes, oldest first (see os4c_paula.c) */
    UWORD dmacon_q[OC_DMACON_QUEUE];
};

#pragma pack()

#ifdef __cplusplus
}
#endif

#endif
