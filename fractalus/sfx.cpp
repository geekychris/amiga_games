#include "sfx.h"

#include <exec/memory.h>
#include <devices/audio.h>
#include <exec/io.h>

#include <proto/exec.h>

#include <string.h>

/*
 * Sample generation — signed 8-bit, byte per sample. Chip RAM.
 *
 * Period picks the sample rate: Paula clock is ~3.55 MHz PAL, so
 * period 300 gives ~11.8 KHz playback. Kept below 250 to stay
 * clear of aliasing artefacts on the short waveforms.
 */

static void gen_laser(BYTE *buf, int n)
{
    /* Sharp downward chirp — sawtooth whose fundamental drops from
     * high (fast phase inc) to mid over the sample. Amplitude fades
     * out. */
    LONG phase = 0;
    for (int i = 0; i < n; i++) {
        LONG inc = 24 - (i * 18 / n);              /* pitch drop */
        LONG amp = 100 - (i * 100 / n);            /* fade */
        phase += inc;
        LONG saw = ((phase & 0x7F) - 64);          /* -64..+63 */
        buf[i] = (BYTE)((saw * amp) / 100);
    }
}

static void gen_explosion(BYTE *buf, int n)
{
    /* White noise burst with exponential-ish amplitude decay. */
    ULONG seed = 0xABCDEF12UL;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245UL + 12345UL;
        LONG amp = 127 - (i * 127 / n);
        BYTE noise = (BYTE)(seed >> 24);
        buf[i] = (BYTE)((noise * amp) / 127);
    }
}

static void gen_damage(BYTE *buf, int n)
{
    /* Harsh alternating square-ish thing — a warning buzz. */
    for (int i = 0; i < n; i++) {
        buf[i] = ((i >> 2) & 1) ? (BYTE)80 : (BYTE)-80;
    }
}

static void gen_rescue(BYTE *buf, int n)
{
    /* Ascending sawtooth — pitch rises over the sample. */
    LONG phase = 0;
    for (int i = 0; i < n; i++) {
        LONG inc = 8 + (i * 20 / n);
        phase += inc;
        LONG saw = ((phase & 0x7F) - 64);
        buf[i] = (BYTE)saw;
    }
}

/* ---------------------------------------------------------------- */

int Sfx::init()
{
    int i;
    memset(this, 0, sizeof(*this));

    port = CreateMsgPort();
    if (!port) return -1;

    /* Try to allocate each channel independently. If one fails, keep
     * going — we can still play SFX on the channels that opened. */
    for (i = 0; i < SFX_COUNT; i++) {
        req[i] = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio));
        if (!req[i]) continue;
        cmask[i] = (UBYTE)(1 << i);
        req[i]->ioa_Request.io_Message.mn_ReplyPort = port;
        req[i]->ioa_Request.io_Command = ADCMD_ALLOCATE;
        req[i]->ioa_Request.io_Flags   = ADIOF_NOWAIT;
        req[i]->ioa_AllocKey = 0;
        req[i]->ioa_Data   = &cmask[i];
        req[i]->ioa_Length = 1;
        if (OpenDevice((CONST_STRPTR)"audio.device", 0,
                       (struct IORequest *)req[i], 0) == 0) {
            opened[i] = 1;
        } else {
            DeleteIORequest((struct IORequest *)req[i]);
            req[i] = 0;
        }
    }

    generate_samples();
    return 0;
}

void Sfx::generate_samples()
{
    /* Sample lengths / periods per SFX id. All allocated in CHIP RAM
     * because Paula's audio DMA can only read chip. */
    static const struct { int len; UWORD per; } cfg[SFX_COUNT] = {
        { 512, 300 },   /* LASER */
        { 1024, 250 },  /* EXPLOSION */
        { 400, 400 },   /* DAMAGE */
        { 800, 350 },   /* RESCUE */
    };
    for (int i = 0; i < SFX_COUNT; i++) {
        sample_len[i]    = cfg[i].len;
        sample_period[i] = cfg[i].per;
        sample[i] = (BYTE *)AllocMem(cfg[i].len, MEMF_CHIP | MEMF_CLEAR);
        if (!sample[i]) continue;
        switch (i) {
        case SFX_LASER:     gen_laser    (sample[i], cfg[i].len); break;
        case SFX_EXPLOSION: gen_explosion(sample[i], cfg[i].len); break;
        case SFX_DAMAGE:    gen_damage   (sample[i], cfg[i].len); break;
        case SFX_RESCUE:    gen_rescue   (sample[i], cfg[i].len); break;
        }
    }
}

void Sfx::free_samples()
{
    for (int i = 0; i < SFX_COUNT; i++) {
        if (sample[i]) {
            FreeMem(sample[i], sample_len[i]);
            sample[i] = 0;
        }
    }
}

void Sfx::shutdown()
{
    /* Drain and abort any in-flight audio writes before closing. */
    for (int i = 0; i < SFX_COUNT; i++) {
        if (req[i] && opened[i] && busy[i]) {
            AbortIO((struct IORequest *)req[i]);
            WaitIO((struct IORequest *)req[i]);
        }
    }
    for (int i = 0; i < SFX_COUNT; i++) {
        if (req[i] && opened[i]) {
            CloseDevice((struct IORequest *)req[i]);
            opened[i] = 0;
        }
        if (req[i]) {
            DeleteIORequest((struct IORequest *)req[i]);
            req[i] = 0;
        }
    }
    free_samples();
    if (port) { DeleteMsgPort(port); port = 0; }
}

void Sfx::tick()
{
    if (!port) return;
    struct Message *m;
    while ((m = GetMsg(port))) {
        for (int i = 0; i < SFX_COUNT; i++) {
            if ((struct Message *)req[i] == m) {
                busy[i] = 0;
                break;
            }
        }
    }
}

void Sfx::play(int id)
{
    if (id < 0 || id >= SFX_COUNT) return;
    if (!opened[id] || !sample[id]) return;
    tick();                 /* reap any completions first */
    if (busy[id]) return;   /* channel still playing — drop the trigger */

    req[id]->ioa_Request.io_Command = CMD_WRITE;
    req[id]->ioa_Request.io_Flags   = ADIOF_PERVOL;
    req[id]->ioa_Data   = (UBYTE *)sample[id];
    req[id]->ioa_Length = sample_len[id];
    req[id]->ioa_Period = sample_period[id];
    req[id]->ioa_Volume = 64;
    req[id]->ioa_Cycles = 1;
    SendIO((struct IORequest *)req[id]);
    busy[id] = 1;
}
