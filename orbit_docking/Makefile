# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Orbit Docking - docking with a spinning station, for the classic
# Amiga (68020+) and AmigaOS 4.1, on the shared engine in ../amiga3d.
#   scripts/build-example-68k.sh orbit_docking   orbit_docking, orbit_docking_test
#   scripts/build-example-ppc.sh orbit_docking   orbit_docking_os4
#   make host-test                               the tests on the host
#   make ubsan                                   the tests under UBSan and ASan
#   make vectors                                 regenerate tests/vectors.h (the long game's hash)

NAME    = orbit_docking
CORE    = od_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = od.h dock.h tests/vectors.h

include ../amiga3d/a3d.mk

ubsan:
	$(HOSTCC) -g -O1 -Wall -fsanitize=undefined,address -fno-sanitize-recover=undefined -I. -I$(A3D) \
	    -o tests/host_tests_san $(A3D)/test_host.c $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/host_tests_san

vectors:
	$(HOSTCC) -O2 -Wall -DOD_TRACE_MAIN -I. -I$(A3D) -o tests/trace $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/trace > tests/vectors.h.new && mv tests/vectors.h.new tests/vectors.h
	cat tests/vectors.h
.PHONY: ubsan vectors
