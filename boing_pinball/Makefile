# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Boing Pinball - three pinball tables for the classic Amiga (68020+) and
# AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh boing_pinball      boing_pinball, boing_pinball_test
#   scripts/build-example-ppc.sh boing_pinball      boing_pinball_os4
#   make host-test                                  the tests on the host
#   make host-preview                               the tables top down, and bot-played statistics

NAME    = boing_pinball
CORE    = pb_core.c pb_tables.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = pb.h pinball.h tests/vectors.h

include ../amiga3d/a3d.mk

host-preview:
	$(HOSTCC) -O2 -I. -I$(A3D) -o tests/host_preview tests/preview.c $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c -lm
	tests/host_preview 40

# the browser game's core, for the test vectors (a checkout of geekychris.github.io's source)
SITE ?= ../../../geekychris-site
vectors:
	node tests/gen_vectors.js $(abspath $(SITE))/site/static/tools/boing-pinball-core.js > tests/vectors.h

# AmigaOS 4's artwork (`make art`):
#   assets/playfield_<table>.png  top down, the table from x 0..520,
#                                 y -60..1040 mm (any size near 1:2), squashed
#                                 into the view
#   assets/backglass_<table>.png  4:3, the title's attract page, with
#   assets/logo.png               on top (black is see-through)
ART = $(patsubst assets/%.png,%.img,$(wildcard assets/playfield_*.png)) \
      $(patsubst assets/backglass_%.png,title_%.img,$(wildcard assets/backglass_*.png))
art: $(ART)
playfield_%.img: assets/playfield_%.png
	python3 $(A3D)/tools/make_image.py $< $@ 572 968 --stretch
title_%.img: assets/backglass_%.png assets/logo.png
	python3 $(A3D)/tools/make_image.py $< $@ 640 512 --over assets/logo.png 70 2 500 --shade-bottom 150
.PHONY: art
