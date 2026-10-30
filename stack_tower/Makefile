# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Stack Tower - the falling-block puzzle round a tower, for the classic
# Amiga (68020+) and AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh stack_tower      stack_tower, stack_tower_test
#   scripts/build-example-ppc.sh stack_tower      stack_tower_os4
#   make host-test                                the tests on the host
#   make vectors                                  regenerate tests/vectors.h from the browser game

NAME    = stack_tower
CORE    = st_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = st.h stack.h tests/vectors.h

include ../amiga3d/a3d.mk

SITE ?= ../../../geekychris-site/site/static/tools
vectors:
	node tests/gen_vectors.js $(SITE)/stack-tower-core.js > tests/vectors.h

# AmigaOS 4's artwork (`make art`): assets/title_source.png (4:3) and
# assets/backdrop.png (about 4:5, behind the tower)
art: stack_title.img stack_backdrop.img
stack_title.img: assets/title_source.png
	python3 $(A3D)/tools/make_image.py $< $@ 640 512 --shade-bottom 110
stack_backdrop.img: assets/backdrop.png
	python3 $(A3D)/tools/make_image.py $< $@ 400 512
.PHONY: art vectors
