# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Sphere Siege - a shooter inside a hollow sphere, for the classic
# Amiga (68020+) and AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh sphere_siege   sphere_siege, sphere_siege_test
#   scripts/build-example-ppc.sh sphere_siege   sphere_siege_os4
#   make host-test                               the tests on the host
#   make ubsan                                   the tests under UBSan and ASan
#   make vectors                                 regenerate tests/vectors.h (the long game's hash)

NAME    = sphere_siege
CORE    = ss_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = ss.h siege.h tests/vectors.h

include ../amiga3d/a3d.mk

ubsan:
	$(HOSTCC) -g -O1 -Wall -fsanitize=undefined,address -fno-sanitize-recover=undefined -I. -I$(A3D) \
	    -o tests/host_tests_san $(A3D)/test_host.c $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/host_tests_san

vectors:
	$(HOSTCC) -O2 -Wall -DSS_TRACE_MAIN -I. -I$(A3D) -o tests/trace $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/trace > tests/vectors.h.new && mv tests/vectors.h.new tests/vectors.h
	cat tests/vectors.h

# play the autopilot for a while and print what happens (tuning)
sim:
	$(HOSTCC) -O2 -Wall -DSS_SIM_MAIN -I. -I$(A3D) -o tests/sim $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/sim
.PHONY: ubsan vectors sim
