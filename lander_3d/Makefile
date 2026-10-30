# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Lander 3D - land on the Moon, Mars and an asteroid, for the classic Amiga
# (68020+) and AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh lander_3d      lander_3d, lander_3d_test
#   scripts/build-example-ppc.sh lander_3d      lander_3d_os4
#   make host-test                              the tests on the host

NAME    = lander_3d
CORE    = ld_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = ld.h lander.h

include ../amiga3d/a3d.mk

host-trace:
	$(HOSTCC) -O2 -I. -I$(A3D) -o tests/host_trace tests/trace_main.c $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c


# AmigaOS 4's artwork (`make art`): assets/title_source.png (4:3) and the
# sky panoramas assets/sky_<world>.png (about 3:1, the bottom edge the
# horizon, the ends joined here)
ART = lander_title.img $(patsubst assets/%.png,%.img,$(wildcard assets/sky_*.png))
art: $(ART)
lander_title.img: assets/title_source.png
	python3 $(A3D)/tools/make_image.py $< $@ 640 512 --shade-bottom 110
sky_%.img: assets/sky_%.png
	python3 $(A3D)/tools/make_image.py $< $@ 768 256 --align bottom --wrap 64 --fade-top 40
.PHONY: art host-trace
