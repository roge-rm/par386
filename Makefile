# Builds build/Par386.prc with prc-tools-remix and PilRC.
#   make             debug PRC (build/debug/)
#   make release     release PRC (build/release/)
#   make host-test   builds and runs the core tests on the host, then course-test
#   make course-test compiles the test course, round-trips it and checks it loads
#   make courses     compiles every course in courses/ to build/courses/
#   make tables      makes src/core/trigtab.h, fonttab.h, pal.h and paltab.h again (the output is committed)
#   make art         makes the title and menu pictures and the renderer's sprites in build/art/ (part of the PRC)
#   make sounds      makes the sampled sounds in build/snd/ (part of the PRC)
#   make arm-test    checks the ARM 16-bit conversion's entry against the plain one on the host
#   make ui-test     draws the title and menu with the core UI kit on the host, to PNGs
#   make render-test draws the test hole from 3 places and compares with host/ref/
#   make course-render draws every hole of Auld Kirk from the tee, the landing area and near the green
#                    (CR=carrighead for Carrig Head, CR=sakurabay for Sakura Bay,
#                    CR=banksiaheath for Banksia Heath, CR=crocodilebend for Crocodile Bend,
#                    CR=cascadesprings for Cascade Springs)
#   make auldkirk    writes courses/auldkirk/ again from tools/mkauldkirk.py
#   make carrighead  writes courses/carrighead/ again from tools/mkcarrig.py
#   make sakurabay   writes courses/sakurabay/ again from tools/mksakura.py
#   make banksiaheath writes courses/banksiaheath/ again from tools/mkbanksia.py
#   make crocodilebend writes courses/crocodilebend/ again from tools/mkcroc.py
#   make cascadesprings writes courses/cascadesprings/ again from tools/mkcascade.py
#   make render-ref  makes host/ref/ again from the current renderer
#   make physics-test plays shots on the host and checks carries, wind, lies, water and the meter
#   make rules-test  turn order, honour, scores, the scorecard and the saved round
#   make cpu-round   computer golfers play a course (CR=carrighead, TEE=1 for the second tee set) and the scores are shown
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

PALMSRCS = main screen audio arm
CORESRCS = fixed trig vec hole gfx ui front render play ball rules card sound cpu records to16
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

$(ART)/art.rcp: tools/mkart.py tools/mkpal.py tools/mockup/mock1b.py tools/mockup/mock1b2.py src/core/uicol.h tools/mkpreview.py tools/mkstyle.py tools/mkobj.py art/golfer/f13-40.png art/golfer/f13-80.png $(SCENE)
	python3 tools/mkart.py $(ART)

$(ART)/art.ro: $(ART)/art.rcp
	pilrc -q -ro $(ART)/art.rcp $@

# The renderer's sprites, golfer and backdrop, cut to the master palette.
$(ART)/obj.rcp: tools/mkobj.py tools/mkstyle.py tools/mkpal.py tools/mkart.py src/core/uicol.h art/golfer/*.png
	python3 tools/mkobj.py $(ART) $(DEBUGDIR)/m3/art

$(ART)/obj.ro: $(ART)/obj.rcp
	pilrc -q -ro $(ART)/obj.rcp $@

art: $(ART)/art.rcp $(ART)/obj.rcp

# The sampled sounds for OS 5 Palms (src/palm/audio.c), made by a script; WAVs of them go to $(DEBUGDIR)/m10/samples.
SND      = build/snd

$(SND)/snd.rcp: tools/mksound.py
	python3 tools/mksound.py $(SND) $(DEBUGDIR)/m10/samples

$(SND)/snd.ro: $(SND)/snd.rcp
	pilrc -q -ro $(SND)/snd.rcp $@

# The ARM code for OS 5 (src/arm/): one flat blob, the armc resource. It runs
# where it lies, so armcheck fails on any relocation but a pc-relative branch,
# and on data. Every build has it; EXTRA=-DNO_ARM only leaves it unused.
ARMCC    = arm-palmos-gcc
ARMFLAGS = -O2 -march=armv4t -mthumb-interwork -ffreestanding -fno-builtin -Wall -Isrc/core -Isrc/arm
ARMOBJS  = build/arm/to16arm.o build/arm/to16.o

build/arm:
	mkdir -p build/arm

build/arm/%.o: src/arm/%.c src/arm/*.h src/core/*.h | build/arm
	@echo ARMCC $<; $(ARMCC) $(ARMFLAGS) -c $< -o $@

build/arm/%.o: src/core/%.c src/core/*.h | build/arm
	@echo ARMCC $<; $(ARMCC) $(ARMFLAGS) -c $< -o $@

build/arm/armc.bin: $(ARMOBJS) src/arm/armc.ld
	@for o in $(ARMOBJS); do \
		if arm-palmos-readelf -r $$o | grep -E '^[0-9a-f]{8} ' | grep -v R_ARM_PC24; then \
			echo "armcheck: $$o has a relocation the blob can't take"; exit 1; fi; done
	arm-palmos-ld -nostdlib -T src/arm/armc.ld $(ARMOBJS) -o build/arm/armc.elf
	@if arm-palmos-objdump -h build/arm/armc.elf | grep -E ' \.data +0*[1-9a-f]'; then \
		echo "armcheck: the blob has data"; exit 1; fi
	arm-palmos-objcopy -O binary -j .text build/arm/armc.elf $@

build/arm/arm.ro: build/arm/armc.bin
	echo 'DATA "armc" ID 1000 "build/arm/armc.bin"' > build/arm/arm.rcp
	pilrc -q -ro build/arm/arm.rcp $@

build/$(PRC): src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro $(ART)/obj.ro $(SND)/snd.ro build/arm/arm.ro
	build-prc -o $@ src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro $(ART)/obj.ro $(SND)/snd.ro build/arm/arm.ro

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
	$(MAKE) arm-test

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
build/courses/Par386-Carrighead.pdb: $(wildcard courses/carrighead/*)
build/courses/Par386-Sakurabay.pdb: $(wildcard courses/sakurabay/*)
build/courses/Par386-Banksiaheath.pdb: $(wildcard courses/banksiaheath/*)
build/courses/Par386-Crocodilebend.pdb: $(wildcard courses/crocodilebend/*)
build/courses/Par386-Cascadesprings.pdb: $(wildcard courses/cascadesprings/*)

# The Auld Kirk sources are made by a script; run it after editing it.
auldkirk:
	python3 tools/mkauldkirk.py courses/auldkirk

carrighead:
	python3 tools/mkcarrig.py courses/carrighead

sakurabay:
	python3 tools/mksakura.py courses/sakurabay

banksiaheath:
	python3 tools/mkbanksia.py courses/banksiaheath

crocodilebend:
	python3 tools/mkcroc.py courses/crocodilebend

cascadesprings:
	python3 tools/mkcascade.py courses/cascadesprings

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
# The 320x320 grey screens (doubled 2-bit pictures) are compared with host/ref/.
UIREFS   = q4-title-320 q4-menu-320 q2-title-320 q2-menu-320

ui-test: build/uitest $(ART)/art.rcp
	mkdir -p $(DEBUGDIR)/m1b
	rm -rf build/ui
	build/uitest $(ART) build/ui
	python3 tools/topng.py build/ui $(DEBUGDIR)/m1b host-
	python3 tools/refcmp.py build/ui build/ui-png host/ref $(UIREFS)

ui-ref: build/uitest $(ART)/art.rcp
	rm -rf build/ui
	build/uitest $(ART) build/ui
	python3 tools/refcmp.py build/ui build/ui-png host/ref --update $(UIREFS)

build/rendertest: host/rendertest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/rendertest.c $(HOSTSRC) -lm -o $@

# Draws the test hole from the tee, mid-fairway, near the green and the
# green view for putting at
# 160x160 2-bit and 320x320 8-bit, 4-bit and 1-bit at 160, 16-bit output
# and the 320x480 and 480x320 screens, writes PNGs to $(DEBUGDIR)/m3 and
# compares them with host/ref/. render-ref stores the current ones there.
REFS     = tee-160 tee-320 fairway-160 fairway-320 approach-160 approach-320 green-160 green-320 \
           d4-tee-160 d4-green-160 d1-tee-160 d1-green-160 d8-tee-320-16 \
           tee-320x480-16 green-320x480-16 map-320x480 tee-480x320-16 green-480x320-16 map-480x320 \
           h4-tee-320 h4-map-320 h4-green-320 h2-tee-320 h8-tee-320 \
           tee-top-160 tee-top-320 tee-finish-160 tee-finish-320 h4-tee-top-320

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

# Every hole of a course (CR, Auld Kirk unless set) from the tee, the landing
# area and near the green, and its map, at 160 2-bit and 320 8-bit, to
# $(DEBUGDIR)/m8/ (Carrig Head to m13-carrig/, Sakura Bay to m13-sakura/,
# Banksia Heath to m13-banksia/, Crocodile Bend to m13-croc/, Cascade Springs to
# m13-cascade/). Round-trips the course
# through the dump and checks every hole loads.
CR       ?= auldkirk
CRPDB    = Par386-$(shell echo $(CR) | sed 's/./\u&/').pdb
CROUT    = $(if $(filter auldkirk,$(CR)),m8,$(if $(filter carrighead,$(CR)),m13-carrig,$(if $(filter sakurabay,$(CR)),m13-sakura,$(if $(filter banksiaheath,$(CR)),m13-banksia,$(if $(filter crocodilebend,$(CR)),m13-croc,$(if $(filter cascadesprings,$(CR)),m13-cascade,course-$(CR)))))))
course-render: build/courserender build/coursetest build/courses/$(CRPDB) $(ART)/obj.rcp
	rm -rf build/course-render build/course-dump/$(CR) && mkdir -p build/course-render
	python3 tools/coursec.py dump build/courses/$(CRPDB) build/course-dump/$(CR)
	python3 tools/coursec.py build build/course-dump/$(CR) build/course-dump/$(CRPDB)
	build/coursetest --course build/courses/$(CRPDB) build/course-dump/$(CRPDB)
	build/courserender build/courses/$(CRPDB) $(ART) build/course-render
	python3 tools/refcmp.py build/course-render $(DEBUGDIR)/$(CROUT) -

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

# Computer golfers (Easy, Fair, Good, Pro) play 8 rounds of a course (CR, as
# course-render) with its wind: mean scores per hole, water, out of bounds
# and pick-ups. Fails if a round doesn't finish or Good or Pro picks up.
cpu-round: build/rulestest build/courses/$(CRPDB)
	build/rulestest --cpu build/courses/$(CRPDB) 8 $(or $(TEE),0)

build/armtest: host/armtest.c src/arm/to16arm.c $(HOSTSRC) src/core/*.h src/arm/*.h | $(BUILD)
	$(HOSTCC) -Isrc/arm host/armtest.c src/arm/to16arm.c $(HOSTSRC) -lm -o $@

# Runs the 16-bit conversion through the ARM entry (built for the host, with
# its big endian job) and the plain path, and checks they match.
arm-test: build/armtest
	build/armtest

tables:
	python3 tools/mktrig.py
	python3 tools/mkfont.py
	python3 tools/mkpal.py

clean:
	rm -rf build

sounds: $(SND)/snd.rcp

.PHONY: sounds all release clean host-test course-test course-render cpu-round auldkirk carrighead sakurabay banksiaheath crocodilebend cascadesprings ui-test ui-ref arm-test render-test render-ref physics-test rules-test courses tables art
