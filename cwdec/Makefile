# Banco di prova su PC del decoder CW.
#   make          -> build/cwdec_wav
#   make test     -> suite di regressione (CER su segnali sintetici)
#   make sanitize -> build con ASan/UBSan
CC      ?= cc
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra -Wshadow -Wdouble-promotion -fno-math-errno
BUILD   := build

all: $(BUILD)/cwdec_wav

$(BUILD)/cwdec_wav: cwdec.c cwdec.h host/cwdec_wav.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ cwdec.c host/cwdec_wav.c

$(BUILD):
	mkdir -p $@

test: $(BUILD)/cwdec_wav
	python3 test/run_suite.py --bin $(BUILD)/cwdec_wav

sanitize: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: clean $(BUILD)/cwdec_wav

clean:
	rm -rf $(BUILD)

.PHONY: all test sanitize clean
