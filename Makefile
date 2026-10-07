DEBUG ?= no
ifeq ($(DEBUG),no)
	MUPDF_BUILD ?= release
	OPTIMIZE ?= 3
else
	MUPDF_BUILD ?= debug
	OPTIMIZE ?= g
endif

MUPDF_VERBOSE ?= no
MUPDF_DIR := mupdf
MUPDF_INC := $(MUPDF_DIR)/include
MUPDF_OUT := $(MUPDF_DIR)/build/$(MUPDF_BUILD)

# The TI-Nspire uses an ARM926EJ-S core (ARMv5TEJ). It has no FPU, no NEON and no
# DSP/Thumb-2 extensions, so code must not be allowed to use them.
#
# IMPORTANT: nspire-g++ is a very old GCC and does NOT understand -mcpu/-mtune for
# this core, so the arch flags are passed through to the MuPDF build via XCFLAGS
# only when the toolchain accepts them. Verify with:
#     nspire-g++ -mcpu=arm926ej-s -Q --help=target | grep '^  -march'
# If that prints armv5tej, the flag is honoured. If nspire-g++ silently ignores it,
# drop the flag rather than letting GCC fall back to its default (which is a
# newer/wider arch and can emit instructions the CPU cannot execute).
#
# -ffunction-sections/-fdata-sections are required for the -Wl,--gc-sections in
# NPDF_LDFLAGS to actually remove unused code; without them the linker can only
# discard whole input sections, so almost nothing gets collected from libmupdf.a.
NPDF_ARCHFLAGS ?= -marm -ffunction-sections -fdata-sections

MUPDF_XCFLAGS := -DNOCJK $(NPDF_ARCHFLAGS)

CXX := nspire-g++
CXXFLAGS := -O$(OPTIMIZE) -Wall -Wextra -std=gnu++14 $(NPDF_ARCHFLAGS) -I $(MUPDF_INC)
ifeq ($(OPTIMIZE),g)
	CXXFLAGS += -g
endif
NPDF_LDFLAGS = -L $(MUPDF_OUT) -lmupdf -lmupdfthird -lfreetype -lz -lm -Wl,--gc-sections -Wl,--as-needed
ZEHNFLAGS := --compress --name "nPDF" --author "Legimet" --notice "Document viewer"
OBJS := $(patsubst %.cpp,%.o,$(wildcard *.cpp))
LIBS := $(patsubst %,$(MUPDF_OUT)/lib%.a,mupdf mupdfthird)
EXE := nPDF

all: $(EXE).tns

%.o: %.cpp $(MUPDF_DIR)

$(MUPDF_OUT)/libmupdf.a: $(MUPDF_DIR) generate

$(MUPDF_OUT)/%.a: $(MUPDF_DIR)
	$(MAKE) -C $< build/$(MUPDF_BUILD)/$(notdir $@) verbose=$(MUPDF_VERBOSE) build=$(MUPDF_BUILD) OS=ti-nspire XCFLAGS="$(MUPDF_XCFLAGS)"

$(EXE).elf: $(LIBS) $(OBJS)
	$(CXX) $(OBJS) -o $@ $(NPDF_LDFLAGS)

$(EXE).zehn.tns: $(EXE).elf
	genzehn --input $^ --output $@ $(ZEHNFLAGS)

$(EXE).tns: $(EXE).zehn.tns
	make-prg $^ $@

generate: $(MUPDF_DIR)
	$(MAKE) -C $< generate verbose=$(MUPDF_VERBOSE) build=$(MUPDF_BUILD)

clean: cleannolibs
	-$(MAKE) -C $(MUPDF_DIR) clean build=$(MUPDF_BUILD)

cleannolibs:
	$(RM) $(OBJS) $(EXE).elf $(EXE).zehn.tns $(EXE).tns

.PHONY: all generate clean cleannolibs
