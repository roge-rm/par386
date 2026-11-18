# Builds build/Par386.prc with prc-tools-remix and PilRC.
#   make             debug PRC (build/debug/)
#   make release     release PRC (build/release/)
#   make host-test   builds and runs the core tests on the host
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
CORESRCS = fixed
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

build/$(PRC): src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro
	build-prc -o $@ src/par386.def $(BUILD)/par386 $(BUILD)/par386.ro

host-test: build/hosttest
	build/hosttest

HOSTSRC  = src/core/fixed.c

build/hosttest: host/hosttest.c $(HOSTSRC) src/core/*.h | $(BUILD)
	gcc -Wall -Wextra -O2 -Isrc/core -fsanitize=address,undefined host/hosttest.c $(HOSTSRC) -o $@

clean:
	rm -rf build

.PHONY: all release clean host-test
