#ifndef FRACTALUS_COMBAT_H
#define FRACTALUS_COMBAT_H

#include <exec/types.h>
#include "fixed.h"

/*
 * Jaggi flying saucers + player/enemy bullets. Everything lives in
 * world coordinates and is projected each frame — no world-space
 * culling, just the near/behind-camera check the renderer does.
 */

#define MAX_SAUCERS      4
#define MAX_BULLETS      16
#define PLAYER_FIRE_COOL 8     /* frames between player shots */
#define SAUCER_FIRE_COOL 90    /* base cooldown */
#define BULLET_LIFETIME  120
#define BULLET_SPEED     70    /* world units / tick */
#define SAUCER_SPEED     18
#define SAUCER_ATTACK_DIST  400
#define SAUCER_SPAWN_MIN    650
#define SAUCER_SPAWN_MAX    900

enum SaucerState {
    SS_INACTIVE    = 0,
    SS_APPROACHING = 1,
    SS_ATTACKING   = 2,
    SS_DYING       = 3,
};

struct Saucer {
    LONG   x, y, z;
    UBYTE  state;
    UBYTE  dying_timer;
    UWORD  fire_cooldown;
    LONG   hp;
};

enum BulletOwner {
    BO_INACTIVE = 0,
    BO_PLAYER   = 1,
    BO_ENEMY    = 2,
};

struct Bullet {
    LONG   x, y, z;
    LONG   vx, vy, vz;
    UWORD  lifetime;
    UBYTE  owner;
    UBYTE  _pad;
};

class Combat {
public:
    void init(ULONG seed, LONG cam_x, LONG cam_y, LONG cam_z);

    /* Advance saucers, bullets, collisions. Reads current camera state
     * so enemies can chase the player and player bullets can be spawned
     * pointing along ship yaw. Reports hits by mutating shield/score. */
    void tick(LONG cam_x, LONG cam_y, LONG cam_z, LONG cam_yaw,
              UWORD input_flags, LONG *shield_out, LONG *score_out);

    LONG saucer_count() const { return MAX_SAUCERS; }
    const Saucer &saucer(LONG i) const { return saucers[i]; }
    LONG bullet_count() const { return MAX_BULLETS; }
    const Bullet &bullet(LONG i) const { return bullets[i]; }
    LONG live_saucer_count() const;

private:
    Saucer saucers[MAX_SAUCERS];
    Bullet bullets[MAX_BULLETS];
    UWORD  player_cooldown;

    LONG spawn_bullet(LONG x, LONG y, LONG z,
                      LONG vx, LONG vy, LONG vz,
                      UBYTE owner);
};

#endif
