#!/bin/sh
# build.sh - the PPC build through Docker, from anywhere (a git worktree of
# examples/ too): AMIGA_MCP points at the amiga_mcp checkout (default: the
# one this examples/ lives in, or ~/code/claude_world/amiga_mcp).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
EX="$(dirname "$HERE")"
MCP="${AMIGA_MCP:-$(dirname "$EX")}"
[ -d "$MCP/third_party/mesa-os4" ] || MCP="$HOME/code/claude_world/amiga_mcp"
IMG="${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11}"
docker run --rm -v "$MCP:/work" -v "$EX:/work/examples" -w /work/examples/boing_twist "$IMG" \
    sh -c 'export PATH=/opt/ppc-amigaos/bin:/opt/adtools/bin:$PATH && make '"${1:-all}"' 2>&1'
