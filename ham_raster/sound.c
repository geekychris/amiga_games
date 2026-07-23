#include "sound.h"

#include <exec/memory.h>
#include <devices/audio.h>
#include <proto/exec.h>

/* Self-contained clang: mix of six inharmonic sine partials plus a
 * short noise attack, exponentially decayed. Copy of the boing_ball
 * synth with its own sin_table[] inlined so we don't take on a
 * tables.c dependency. */
#define SAMPLE_LENGTH 8000    /* ~0.36 seconds at ~22 kHz */
#define SAMPLE_PERIOD  162    /* ~22050 Hz sample rate */
#define SAMPLE_VOLUME   64

static struct MsgPort *audio_port = NULL;
static struct IOAudio *audio_io   = NULL;
static BYTE *sample_data = NULL;
static UBYTE which_channel[] = { 1, 2, 4, 8 };
static BOOL device_open = FALSE;
static BOOL playing     = FALSE;

/* 256-entry 8.8 sine table, built at init time from a 65-entry
 * first-quadrant lookup. */
static WORD sin_table[256];
static const WORD sine_q1[65] = {
      0,   6,  13,  19,  25,  31,  38,  44,
     50,  56,  62,  68,  74,  80,  86,  92,
     98, 104, 109, 115, 121, 126, 132, 137,
    142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250,
    251, 252, 253, 254, 255, 255, 256, 256,
    256
};

static void build_sin_table(void)
{
    WORD i;
    for (i = 0; i <= 64; i++) {
        sin_table[i]         =  sine_q1[i];
        sin_table[128 - i]   =  sine_q1[i];
        sin_table[128 + i]   = -sine_q1[i];
        if (i > 0)
            sin_table[256 - i] = -sine_q1[i];
    }
}

static WORD noise_state = 12345;
static WORD noise_next(void)
{
    noise_state = (noise_state * 25173 + 13849) & 0x7FFF;
    return (noise_state & 0xFF) - 128;
}

static void synthesize_clank(void)
{
    WORD i;
    if (!sample_data) return;

    for (i = 0; i < SAMPLE_LENGTH; i++) {
        LONG t = ((LONG)i * 256) / SAMPLE_LENGTH;    /* 0..255 */
        LONG env = 256 - t;
        if (env < 0) env = 0;
        env = (env * env) >> 8;

        LONG p0 = ((LONG)i *  19) / 16;
        LONG p1 = ((LONG)i *  42) / 16;
        LONG p2 = ((LONG)i *  82) / 16;
        LONG p3 = ((LONG)i * 137) / 16;
        LONG p4 = ((LONG)i * 218) / 16;
        LONG p5 = ((LONG)i * 347) / 16;

        LONG val = (sin_table[p0 & 0xFF] * 5) / 4;
        val +=  sin_table[p1 & 0xFF];
        val += (sin_table[p2 & 0xFF] * 3) / 4;
        val += (sin_table[p3 & 0xFF] * 3) / 4;
        val +=  sin_table[p4 & 0xFF] / 2;
        val +=  sin_table[p5 & 0xFF] / 3;

        if (i < 300) {
            LONG noise_amt = ((300 - i) * 3);
            val += (noise_next() * noise_amt) >> 7;
        }
        val = (val * env) >> 8;
        if (val >  127) val =  127;
        if (val < -127) val = -127;
        sample_data[i] = (BYTE)val;
    }
}

BOOL sound_init(void)
{
    audio_port = CreateMsgPort();
    if (!audio_port) { sound_cleanup(); return FALSE; }

    audio_io = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
    if (!audio_io) { sound_cleanup(); return FALSE; }

    sample_data = (BYTE *)AllocMem(SAMPLE_LENGTH, MEMF_CHIP | MEMF_CLEAR);
    if (!sample_data) { sound_cleanup(); return FALSE; }

    audio_io->ioa_Request.io_Message.mn_ReplyPort = audio_port;
    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audio_io->ioa_Request.io_Command = ADCMD_ALLOCATE;
    audio_io->ioa_Request.io_Flags   = ADIOF_NOWAIT;
    audio_io->ioa_AllocKey = 0;
    audio_io->ioa_Data     = which_channel;
    audio_io->ioa_Length   = sizeof(which_channel);

    if (OpenDevice(AUDIONAME, 0, (struct IORequest *)audio_io, 0) != 0) {
        sound_cleanup();
        return FALSE;
    }
    device_open = TRUE;

    build_sin_table();
    synthesize_clank();
    return TRUE;
}

void sound_play_boing(void)
{
    if (!device_open || !sample_data) return;

    if (playing) {
        AbortIO((struct IORequest *)audio_io);
        WaitIO ((struct IORequest *)audio_io);
    }
    audio_io->ioa_Request.io_Command = CMD_WRITE;
    audio_io->ioa_Request.io_Flags   = ADIOF_PERVOL;
    audio_io->ioa_Data   = (UBYTE *)sample_data;
    audio_io->ioa_Length = SAMPLE_LENGTH;
    audio_io->ioa_Period = SAMPLE_PERIOD;
    audio_io->ioa_Volume = SAMPLE_VOLUME;
    audio_io->ioa_Cycles = 1;

    BeginIO((struct IORequest *)audio_io);
    playing = TRUE;
}

void sound_cleanup(void)
{
    if (device_open) {
        if (playing) {
            AbortIO((struct IORequest *)audio_io);
            WaitIO ((struct IORequest *)audio_io);
        }
        CloseDevice((struct IORequest *)audio_io);
        device_open = FALSE;
    }
    if (sample_data) { FreeMem(sample_data, SAMPLE_LENGTH); sample_data = NULL; }
    if (audio_io)    { DeleteIORequest((struct IORequest *)audio_io); audio_io = NULL; }
    if (audio_port)  { DeleteMsgPort(audio_port); audio_port = NULL; }
}
