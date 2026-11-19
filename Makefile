# Builds build/Par386.prc with prc-tools-remix and PilRC.
#   make             debug PRC (build/debug/)
#   make release     release PRC (build/release/)
#   make host-test   builds and runs the core tests on the host, then course-test
#   make course-test compiles the test course, round-trips it and checks it loads
#   make courses     compiles every course in courses/ to build/courses/
#   make tables      makes src/core/trigtab.h, fonttab.h, pal.h and paltab.h again (the output is committed)
#   make art         makes the title and menu pictures and the renderer's sprites in build/art/ (part of the PRC)
#   make ui-test     draws the title and menu with the core UI kit on the host, to PNGs
#   make render-test draws the test hole from 3 places and compares with host/ref/
#   make course-render draws every hole of Auld Kirk from the tee, the landing area and near the green
#   make auldkirk    writes courses/auldkirk/ again from tools/mkauldkirk.py
#   make render-ref  makes host/ref/ again from the current renderer
#   make physics-test plays shots on the host and checks carries, wind, lies, water and the meter
#   make rules-test  turn order, honour, scores, the scorecard and the saved round
# Both flavours write build/Par386.prc, so build one at a time.

SDK      = ../palm-os-sdk/sdk-5r3
PRC      = Par386.prc

ifdef RELEASE
BUILD    = build/release
DEFS     = $(EXTRA)
else
BUILD    = build/debug
DEFS     = -DDEBUG $(EXTRA)
endif

CC       = m68k-palmos-gcc
INCLUDES = $(addprefix -I,$(shell find $(SDK)/include -type d))
CFLAGS   = -O2 -Wall -Wno-unknown-pragmas $(DEFS) $(INCLUDES) -Isrc/core -Isrc/palm
LDFLAGS  = -L$(SDK)/lib/m68k-palmos-coff

PALMSRCS = main screen
CORESRCS = fixed trig vec hole gfx ui front render play ball rules card
OBJS     = $(addprefix $(BUILD)/,$(addsuffix .o,$(PALMSRCS) $(CORESRCS)))

all: build/$(PRC) courses

release:
	$(MAKE) RELEASE=1 build/$(PRC) courses

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/palm/%.c src/palm/*.h src/core/*.h | $(BUILD)
	@echo CC $<; $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/core/%.c src/core/*.h | $(BUILD)
	@echo CC $<; $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/par386-sections.s $(BUILD)/par386-sections.ld &: src/par386.def | $(BUILD)
	m68k-palmos-multigen -b $(BUILD)/par386-sections src/par386.def

$(BUILD)/par386-sections.o: $(BUILD)/par386-sections.s
	$(CC) -c $< -o $@

# segcheck fails the build on references that link but go wrong on the
# device (extern const tables, calls across code segments).
$(BUILD)/par386: $(OBJS) $(BUILD)/par386-sections.o $(BUILD)/par386-sections.ld
	python3 tools/segcheck.py $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(BUILD)/par386-sections.o $(BUILD)/par386-sections.ld -o $@

$(BUILD)/par386.ro: src/par386.rcp src/palm/resource.h res/*.bmp | $(BUILD)
	pilrc -q -ro -I src src/par386.rcp $@

# Title and menu pictures. The scene render is slow and lives in
# tools/mockup/r/ (not in git); it's made again when render.py changes.
SCENE    = tools/mockup/r/scene_bg_960.png
ART      = build/art

$(SCENE): tools/mockup/render.py
	python3 tools/mockup/render.py tools/mockup/r 960

$(ART)/art.rcp: tools/mkart.py tools/mkpal.py tools/mockup/mock1b.py tools/mockup/mock1b2.py src/core/uicol.h art/golfer/f01-40.png $(SCENE)
	python3 tools/mkart.py $(ART)

$(ART)/art.ro: $(ART)/art.rcp
	pilrc -q -ro $(ART)/art.rcp $@

# The renderer's sprites, golfer and backdrop, cut to the master palette.
$(ART)/obj.rcp: tools/mkobj.py tools/mkpal.py tools/mkart.py src/core/uicol.h art/golfer/*.png
	python3 tools/mkobj.py $(ART) $(DEBUGDIR)/m3/art

$(ART)/obj.ro: $(ART)/obj.rcp
	pilrc -q -ro $(ART)/obj.rcp $@

art: $(ART)/art.rcp $(ART)/obj.rcp

build/$(PRC): src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro $(ART)/obj.ro
	build-prc -o $@ src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro $(ART)/obj.ro

HOSTSRC  = $(addprefix src/core/,$(addsuffix .c,$(CORESRCS)))
HOSTCC   = gcc -Wall -Wextra -O2 -Isrc/core -fsanitize=address,undefined -fno-sanitize-recover=all
DEBUGDIR = build/evidence

host-test: build/hosttest
	build/hosttest
	$(MAKE) course-test
	$(MAKE) ui-test
	$(MAKE) render-test
	$(MAKE) physics-test
	$(MAKE) rules-test

build/hosttest: host/hosttest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/hosttest.c $(HOSTSRC) -lm -o $@

build/coursetest: host/coursetest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/coursetest.c $(HOSTSRC) -lm -o $@

COURSES  = $(notdir $(wildcard courses/*))
courses: $(foreach c,$(COURSES),build/courses/Par386-$(shell echo $(c) | sed 's/./\u&/').pdb)

build/courses/Par386-%.pdb: tools/coursec.py
	python3 tools/coursec.py build courses/$(shell echo $* | tr A-Z a-z) $@

build/courses/Par386-Test.pdb: $(wildcard courses/test/*)
build/courses/Par386-Auldkirk.pdb: $(wildcard courses/auldkirk/*)

# The Auld Kirk sources are made by a script; run it after editing it.
auldkirk:
	python3 tools/mkauldkirk.py courses/auldkirk

# Compiles the test course, dumps it to text, compiles the dump and checks
# both load the same. The surface grid picture goes to $(DEBUGDIR).
course-test: build/coursetest build/courses/Par386-Test.pdb
	rm -rf build/course-dump
	python3 tools/coursec.py dump build/courses/Par386-Test.pdb build/course-dump/test
	python3 tools/coursec.py build build/course-dump/test build/course-dump/Par386-Test.pdb
	build/coursetest build/courses/Par386-Test.pdb build/course-dump/Par386-Test.pdb build/m2-test-hole-surface.ppm
	mkdir -p $(DEBUGDIR)
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
		build/m2-test-hole-surface.ppm $(DEBUGDIR)/m2-test-hole-surface.png

build/uitest: host/uitest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/uitest.c $(HOSTSRC) -lm -o $@

# Draws each screen at 160x160 2-bit and 320x320 8-bit from the packed
# pictures through the same core code as the Palm build, and checks taps
# and keys move through the screens.
ui-test: build/uitest $(ART)/art.rcp
	mkdir -p $(DEBUGDIR)/m1b
	build/uitest $(ART) build/ui
	python3 tools/topng.py build/ui $(DEBUGDIR)/m1b host-

build/rendertest: host/rendertest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/rendertest.c $(HOSTSRC) -lm -o $@

# Draws the test hole from the tee, mid-fairway, near the green and the
# green view for putting at
# 160x160 2-bit and 320x320 8-bit, 4-bit and 1-bit at 160, 16-bit output
# and the 320x480 and 480x320 screens, writes PNGs to $(DEBUGDIR)/m3 and
# compares them with host/ref/. render-ref stores the current ones there.
REFS     = tee-160 tee-320 fairway-160 fairway-320 approach-160 approach-320 green-160 green-320 \
           d4-tee-160 d4-green-160 d1-tee-160 d1-green-160 d8-tee-320-16 \
           tee-320x480-16 green-320x480-16 map-320x480 tee-480x320-16 green-480x320-16 map-480x320

render-test: build/rendertest build/courses/Par386-Test.pdb $(ART)/obj.rcp
	rm -rf build/render && mkdir -p build/render
	build/rendertest build/courses/Par386-Test.pdb $(ART) build/render
	python3 tools/refcmp.py build/render $(DEBUGDIR)/m3 host/ref $(REFS)

render-ref: build/rendertest build/courses/Par386-Test.pdb $(ART)/obj.rcp
	rm -rf build/render && mkdir -p build/render
	build/rendertest build/courses/Par386-Test.pdb $(ART) build/render
	python3 tools/refcmp.py build/render $(DEBUGDIR)/m3 host/ref --update $(REFS)

build/courserender: host/courserender.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/courserender.c $(HOSTSRC) -lm -o $@

# Every hole of Auld Kirk from the tee, the landing area and near the green,
# and its map, at 160 2-bit and 320 8-bit, to $(DEBUGDIR)/m8/. Round-trips
# the course through the dump and checks every hole loads.
course-render: build/courserender build/coursetest build/courses/Par386-Auldkirk.pdb $(ART)/obj.rcp
	rm -rf build/course-render build/course-dump/auldkirk && mkdir -p build/course-render
	python3 tools/coursec.py dump build/courses/Par386-Auldkirk.pdb build/course-dump/auldkirk
	python3 tools/coursec.py build build/course-dump/auldkirk build/course-dump/Par386-Auldkirk.pdb
	build/coursetest --course build/courses/Par386-Auldkirk.pdb build/course-dump/Par386-Auldkirk.pdb
	build/courserender build/courses/Par386-Auldkirk.pdb $(ART) build/course-render
	python3 tools/refcmp.py build/course-render $(DEBUGDIR)/m8 -

build/physicstest: host/physicstest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/physicstest.c $(HOSTSRC) -lm -o $@

# Plays shots on flat, sloped and made-up holes and the test hole: the carry
# of each club in its range, wind, hook and slice, lies, water, out of
# bounds, rolls, the checksum over a run of shots and the swing meter. The
# strip over a blank scene goes to $(DEBUGDIR)/m5/host-*.png.
physics-test: build/physicstest build/courses/Par386-Test.pdb
	rm -rf build/physics && mkdir -p build/physics $(DEBUGDIR)/m6
	build/physicstest build/courses/Par386-Test.pdb build/physics
	python3 tools/topng.py build/physics $(DEBUGDIR)/m6 host-

build/rulestest: host/rulestest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/rulestest.c $(HOSTSRC) -lm -o $@

# Turn order, the honour, scores and totals, the saved round's record, two
# players through play.c on the test course, and the scorecard and players
# screen at 160 and 320 to $(DEBUGDIR)/m7/host-*.png.
rules-test: build/rulestest build/courses/Par386-Test.pdb $(ART)/art.rcp
	rm -rf build/rules && mkdir -p build/rules $(DEBUGDIR)/m7
	build/rulestest build/courses/Par386-Test.pdb $(ART) build/rules
	python3 tools/topng.py build/rules $(DEBUGDIR)/m7 host-

tables:
	python3 tools/mktrig.py
	python3 tools/mkfont.py
	python3 tools/mkpal.py

clean:
	rm -rf build

.PHONY: all release clean host-test course-test course-render auldkirk ui-test render-test render-ref physics-test rules-test courses tables art
