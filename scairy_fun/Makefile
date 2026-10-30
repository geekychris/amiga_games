# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ScAIry Fun - a self-driving car game for the classic Amiga (68020+) and
# AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh scairy_fun      scairy_fun, scairy_fun_test
#   scripts/build-example-ppc.sh scairy_fun      scairy_fun_os4
#   make host-test                               the unit and gameplay tests on the host
#   make host-trace                              tests/host_trace: a route driven, every event printed
#   make art                                     the AmigaOS 4 pictures (scairy_title.img, scairy_horizon.img)
#   make music        scairy.mod   (tools/modgen/scairy_fun.py)
#   make voice        scairy.voc   (tools/modgen/scairy_voices.py, needs Kokoro)

NAME    = scairy_fun
CORE    = track.c world.c
GAME    = game.c render.c models.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = sc.h render.h game.h sound.h models.h

include ../amiga3d/a3d.mk

host-trace:
	$(HOSTCC) -O2 -Wall -I. -I$(A3D) -o tests/host_trace tests/trace_main.c $(CORE) $(A3D)/a3d_math.c

# AmigaOS 4's artwork (`make art`): the title picture, and the dusk
# panorama (2 pi FOV x 2 = 2262 pixels round, the bottom edge the horizon)
art title: scairy_title.img scairy_horizon.img
scairy_title.img: assets/title_source.png
	python3 $(A3D)/tools/make_image.py $< $@ 640 512
scairy_horizon.img: assets/horizon.png
	python3 $(A3D)/tools/make_image.py $< $@ 2262 288 --rows 0 0.78 --vscale 0.5 --align bottom --wrap 128 --fade-top 48

music:
	cd ../tools/modgen && python3 scairy_fun.py

voice:
	cd ../tools/modgen && python3 scairy_voices.py

.PHONY: music voice host-trace title art
