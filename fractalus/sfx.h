#ifndef FRACTALUS_SFX_H
#define FRACTALUS_SFX_H

#include <exec/types.h>

/*
 * Small fire-and-forget SFX system built on audio.device. Four
 * pre-generated 8-bit signed samples in chip RAM, one per channel.
 * If a channel is busy when play() is called we just skip that
 * trigger rather than queueing — sounds don't stack.
 *
 * Samples are procedural (no on-disk assets). The generation
 * routines run once in init() at startup.
 */

enum SfxId {
    SFX_LASER      = 0,
    SFX_EXPLOSION  = 1,
    SFX_DAMAGE     = 2,
    SFX_RESCUE     = 3,
    SFX_COUNT      = 4,
};

struct MsgPort;
struct IOAudio;

class Sfx {
public:
    int  init();               /* returns 0 on success, -1 if audio unavailable */
    void shutdown();
    void play(int id);         /* fire-and-forget; drops if channel busy */
    void tick();               /* call once per frame to reap completions */

private:
    struct MsgPort *port;
    struct IOAudio *req[SFX_COUNT];
    UBYTE           cmask[SFX_COUNT];   /* persistent channel-mask bytes */
    BYTE           *sample[SFX_COUNT];  /* chip RAM, 8-bit signed */
    UWORD           sample_len[SFX_COUNT];
    UWORD           sample_period[SFX_COUNT];
    UBYTE           busy[SFX_COUNT];
    UBYTE           opened[SFX_COUNT];  /* which channels successfully opened */

    void generate_samples();
    void free_samples();
};

#endif
