# Build di CW.APP e PROBE.APP con l'SDK ufficiale ICAP (ext/icap-sdk, MIT).
#
#   make                 -> build/CW.APP e build/PROBE.APP (arm-none-eabi-gcc, come l'SDK)
#   make TOOLCHAIN=clang alternativa non ufficiale: clang + ld.lld, senza libgcc
#   make sim             -> CW.APP nell'emulatore ARM (tools/icap_sim.py) con segnale sintetico
#   make check-sdk       -> app/icap.h deve essere identico a quello dell'SDK
#   make disasm          -> disassembla l'app generata
#
# Dall'SDK: icap.h (copia in app/, mai modificarla), tools/app.ld, tools/icap_start.S,
# tools/mkapp.py. Flag da examples/hello/build.sh e apps/digi-common/flags.sh.

SDK       ?= ../ext/icap-sdk
T         := $(SDK)/tools
TOOLCHAIN ?= gnu
BUILD     := build
TOOLS     := ../tools
PYTHON    ?= python3

ifeq ($(wildcard $(T)/mkapp.py),)
  $(error SDK non trovato in $(SDK): esegui ./tools/fetch_upstream.sh)
endif

ifeq ($(TOOLCHAIN),gnu)
  CC      := arm-none-eabi-gcc
  # CF = flags.sh/hello; DF = CF + "-fbuiltin -fno-math-errno" (decoder e shell dell'app in digi-common)
  CF      := -mcpu=cortex-a9 -marm -mfpu=neon-vfpv3 -mfloat-abi=hard -O2 -ffreestanding -nostdlib \
             -fno-builtin -fno-pic -mword-relocations -fno-common -fno-tree-loop-distribute-patterns \
             -fno-section-anchors -Wall
  DF      := $(CF) -fbuiltin -fno-math-errno
  CFLAGS  := $(DF) -Wextra -Wno-unused-parameter -I.
  ASFLAGS := $(CF)
  LD      := arm-none-eabi-gcc
  # --no-warn-rwx-segments: immagine unica linkata a 0 (codice+dati), l'avviso RWX è atteso
  LDFLAGS := $(CF) -nostdlib -T $(T)/app.ld -Wl,--emit-relocs -Wl,--no-dynamic-linker -Wl,--no-warn-rwx-segments
  LIBS    := -lgcc
else
  # niente -mword-relocations/-fno-section-anchors in clang: -mno-movt dà lo stesso risultato;
  # mkapp.py rifiuta comunque MOVW/MOVT assoluti e puntatori fuori dall'app
  CC      := clang --target=armv7a-none-eabihf
  CF      := -mcpu=cortex-a9 -marm -mfpu=neon-vfpv3 -mfloat-abi=hard -O2 -ffreestanding -fno-pic \
             -mno-movt -fno-common -fno-builtin -Wall
  CFLAGS  := $(CF) -fno-math-errno -Wextra -Wno-unused-parameter -I.
  ASFLAGS := $(CF)
  LD      := ld.lld
  LDFLAGS := -T $(T)/app.ld --emit-relocs
  LIBS    :=
endif

CW_OBJS    := $(BUILD)/icap_start.o $(BUILD)/main.o $(BUILD)/support.o $(BUILD)/cwdec.o
PROBE_OBJS := $(BUILD)/icap_start.o $(BUILD)/probe.o $(BUILD)/support.o

# CW.APP: background (EXIT nasconde la finestra, il decoder continua) e modo decoder "CW"
# con radio_mode keep: la radio resta nel modo in cui è (vedi docs/ICAP_ABI.md, "Modo CW").
MKAPP_CW    := --name CW --models 3 --stack 8192 --abi 3 --background --mode CW:keep
MKAPP_PROBE := --name PROBE --models 3 --stack 8192 --abi 3

all: $(BUILD)/CW.APP $(BUILD)/PROBE.APP

$(BUILD):
	mkdir -p $@

$(BUILD)/icap_start.o: $(T)/icap_start.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/%.o: %.c icap.h ../cwdec/cwdec.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/cwdec.o: ../cwdec/cwdec.c ../cwdec/cwdec.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cw.elf: $(CW_OBJS) $(T)/app.ld
	$(LD) $(LDFLAGS) $(CW_OBJS) $(LIBS) -o $@
$(BUILD)/probe.elf: $(PROBE_OBJS) $(T)/app.ld
	$(LD) $(LDFLAGS) $(PROBE_OBJS) $(LIBS) -o $@

$(BUILD)/CW.APP: $(BUILD)/cw.elf
	$(PYTHON) $(T)/mkapp.py $< $@ $(MKAPP_CW)
	$(PYTHON) $(TOOLS)/icap_tool.py info $@
$(BUILD)/PROBE.APP: $(BUILD)/probe.elf
	$(PYTHON) $(T)/mkapp.py $< $@ $(MKAPP_PROBE)
	$(PYTHON) $(TOOLS)/icap_tool.py info $@

check-sdk:
	@cmp icap.h $(SDK)/icap.h && echo "icap.h identico all'SDK ($$(git -C $(SDK) log -1 --format=%h))"

# esegue CW.APP nell'emulatore ARM con un segnale sintetico
sim: $(BUILD)/CW.APP
	$(PYTHON) ../cwdec/test/gen_cw.py $(BUILD)/sim.wav "CQ CQ DE IZ7KHR IZ7KHR PSE K" --wpm 22 --pitch 650 --snr 10
	$(PYTHON) $(TOOLS)/icap_sim.py $(BUILD)/CW.APP --wav $(BUILD)/sim.wav --cfg CW.CFG --start-mode CW \
	    --expect "CQ CQ DE IZ7KHR IZ7KHR PSE K"
	@echo "--- out=line: su DECODE righe intere col prefisso invece di parole ---"
	sed 's/^out=.*/out=line/' CW.CFG > $(BUILD)/line.cfg
	$(PYTHON) $(TOOLS)/icap_sim.py $(BUILD)/CW.APP --wav $(BUILD)/sim.wav --cfg $(BUILD)/line.cfg --start-mode CW \
	    --expect "CQ CQ DE IZ7KHR IZ7KHR PSE K"
	@echo "--- avvio da Apps (niente decode_out, testo nella finestra), buco audio ogni ~200 ms ---"
	$(PYTHON) $(TOOLS)/icap_sim.py $(BUILD)/CW.APP --wav $(BUILD)/sim.wav --cfg CW.CFG --overrun-every 27 \
	    --expect "CQ CQ DE IZ7KHR IZ7KHR PSE K"
	@echo "--- solo rumore dopo il filtro FIL3 (250 Hz): nessun testo ---"
	$(PYTHON) ../cwdec/test/gen_cw.py $(BUILD)/noise.wav "" --len 30 --snr 10 --filter 250 --seed 3
	$(PYTHON) $(TOOLS)/icap_sim.py $(BUILD)/CW.APP --wav $(BUILD)/noise.wav --cfg CW.CFG --expect ""

disasm: $(BUILD)/CW.APP
	$(PYTHON) $(TOOLS)/icap_tool.py disasm $< --all | less

clean:
	rm -rf $(BUILD)

.PHONY: all sim check-sdk disasm clean
