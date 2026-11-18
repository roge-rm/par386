# Builds build/Par386.prc with prc-tools-remix and PilRC.
#   make             debug PRC (build/debug/)
#   make release     release PRC (build/release/)
#   make host-test   builds and runs the core tests on the host, then course-test
#   make course-test compiles the test course, round-trips it and checks it loads
#   make courses     compiles every course in courses/ to build/courses/
#   make tables      makes src/core/trigtab.h and fonttab.h again (the output is committed)
#   make art         makes the title and menu pictures in build/art/ (also part of the PRC)
#   make ui-test     draws the title and menu with the core UI kit on the host, to PNGs
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

PALMSRCS = main
CORESRCS = fixed trig vec hole gfx ui front
OBJS     = $(addprefix $(BUILD)/,$(addsuffix .o,$(PALMSRCS) $(CORESRCS)))

all: build/$(PRC)

release:
	$(MAKE) RELEASE=1 build/$(PRC)

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

$(ART)/art.rcp: tools/mkart.py tools/mockup/mock1b.py tools/mockup/mock1b2.py src/core/uicol.h art/golfer/f01-40.png $(SCENE)
	python3 tools/mkart.py $(ART)

$(ART)/art.ro: $(ART)/art.rcp
	pilrc -q -ro $(ART)/art.rcp $@

art: $(ART)/art.rcp

build/$(PRC): src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro
	build-prc -o $@ src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro $(ART)/art.ro

HOSTSRC  = $(addprefix src/core/,$(addsuffix .c,$(CORESRCS)))
HOSTCC   = gcc -Wall -Wextra -O2 -Isrc/core -fsanitize=address,undefined -fno-sanitize-recover=all
DEBUGDIR = build/evidence

host-test: build/hosttest
	build/hosttest
	$(MAKE) course-test
	$(MAKE) ui-test

build/hosttest: host/hosttest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/hosttest.c $(HOSTSRC) -lm -o $@

build/coursetest: host/coursetest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	$(HOSTCC) host/coursetest.c $(HOSTSRC) -lm -o $@

COURSES  = $(notdir $(wildcard courses/*))
courses: $(foreach c,$(COURSES),build/courses/Par386-$(shell echo $(c) | sed 's/./\u&/').pdb)

build/courses/Par386-%.pdb: tools/coursec.py
	python3 tools/coursec.py build courses/$(shell echo $* | tr A-Z a-z) $@

build/courses/Par386-Test.pdb: $(wildcard courses/test/*)

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

tables:
	python3 tools/mktrig.py
	python3 tools/mkfont.py

clean:
	rm -rf build

.PHONY: all release clean host-test course-test ui-test courses tables art
