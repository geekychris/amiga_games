# amiga_games

Example projects and games for the [AmigaBridge](https://github.com/geekychris/amiga_mcp) cross-development environment. Every project here compiles to a classic AmigaOS m68k executable and links against `libbridge.a` from the parent [`amiga_mcp`](https://github.com/geekychris/amiga_mcp) repo.

## What's in here

Three flavours of project:

- **Demos** — `boing_ball`, `starfield`, `plasma`, `bouncing_ball`, `aga3d`, `sfx_player`.
- **Games** — `dot_chase`, `stakattack`, `orb_hunter`, `bullion_dash`, `nova_defense`, `orbital_patrol`, `pea_shooter_blast`, `frank_the_frog`, `rock_blaster`, `sky_knights`, `ace_pilot`, `lunar_rider`, `uranus_lander`, `jump_quest`, `rj_birthday`, `hubert`, and more.
- **Tools / tests** — `hello_world`, `debug_test`, `test_example`, `test_new_features`, `memory_monitor`, `system_monitor`, `disk_benchmark`, `symbol_demo`, `game_of_life`, `arexx_test`, `shell_proxy`, `launcher`.

All titles here are original — no commercial trademarks are used in code, on-screen text, or filenames. See [`.coderabbit.yaml`](.coderabbit.yaml) for the trademark-screening rules applied to every PR.

## How to build

This repo is designed to live as a submodule of `amiga_mcp` at `examples/`:

```bash
git clone --recurse-submodules https://github.com/geekychris/amiga_mcp.git
cd amiga_mcp
make examples
```

From within a single example directory (once `libbridge.a` is built in the parent repo):

```bash
cd examples/dot_chase
make            # produces the m68k binary named after the directory
```

Every example Makefile assumes it sits two levels below the `amiga-bridge/` library:

```
CFLAGS += -I../../amiga-bridge/include
LDFLAGS += -L../../amiga-bridge -lbridge -lamiga
```

That path resolves as long as this repo is checked out inside a parent `amiga_mcp` clone at `examples/`. Standalone clones need `amiga-bridge/` symlinked or the paths adjusted.

## How to deploy + run

The parent `amiga_mcp` project ships a Python devbench (host-side) and the bridge daemon (Amiga-side). Once devbench is running and connected to your Amiga (FS-UAE, WinUAE, Amiberry, or real hardware):

```
amiga_build_deploy_run  # via the MCP tool from Claude Code
```

or from the web UI at http://localhost:3000/.

Binaries deploy to the AmiKit shared folder and appear on Amiga as `DH2:Dev/<name>`.

## Bridge client API (quick reference)

Every example uses this pattern:

```c
#include "bridge_client.h"

int main(void) {
    ab_init("my_game");
    ab_register_var("score", AB_TYPE_I32, &score);

    while (running) {
        ab_poll();         // process bridge messages
        game_tick();
    }

    ab_cleanup();          // must be called on every exit path
    return 0;
}
```

See the parent repo's [CLAUDE.md](https://github.com/geekychris/amiga_mcp/blob/main/CLAUDE.md) for the full API.

## Contributing

- New examples must call `ab_init()` / `ab_cleanup()` and follow the AmigaOS memory/signal-pairing rules described in the parent repo's CLAUDE.md.
- No commercial trademarks in code, on-screen text, comments, filenames, or binary targets. See `.coderabbit.yaml` for the screening rules.
- Every example should include `-noixemul` and `-m68020` in its Makefile CFLAGS.

## License

TBD — pending a per-project license decision. All code here is original work.
