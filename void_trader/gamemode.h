#ifndef VT_GAMEMODE_H
#define VT_GAMEMODE_H

/*
 * Top-level game mode. Drives which subsystems tick and which
 * screen the renderer paints each frame.
 *
 *   FLIGHT     — 3D world + combat + scanner active
 *   DOCKING    — cinematic slot approach, player controls
 *                partially locked, "DOCKING" banner
 *   DOCKED     — station interior menu (trading in Phase 5)
 *   UNDOCKING  — cinematic pull-out back to FLIGHT
 *   GAME_OVER  — final screen, waits for restart
 */
enum GameMode {
    GM_FLIGHT    = 0,
    GM_DOCKING   = 1,
    GM_DOCKED    = 2,
    GM_UNDOCKING = 3,
    GM_GAME_OVER = 4,
};

/* Distance from station centre within which the docking prompt
 * appears and D triggers the approach. */
#define DOCK_APPROACH_RANGE  4500

/* Distance at which the DOCKING animation completes to DOCKED. */
#define DOCK_LOCK_RANGE      1200

#endif
