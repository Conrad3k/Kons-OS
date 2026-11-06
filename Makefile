# K1 — budowa programów natywnych (C, musl, statycznie) i testów na hoście.
#
#   make            programy K1 → build/bin/ (wymaga nagłówków jądra: scripts/zbuduj-jadro.sh)
#   make testy-host testy jednostkowe na hoście (glibc + ASan/UBSan)
#   make fuzz       fuzzing parsera linii poleceń (clang + libFuzzer), FUZZ_CZAS sekund
#
# Flagi: SPEC §11, zweryfikowane w ADR 0002; scripts/sprawdz-binaria.sh sprawdza wynik.

BUDOWA    ?= build
BIN       := $(BUDOWA)/bin
OBJ       := $(BUDOWA)/obj
NAGLOWKI  ?= $(BUDOWA)/naglowki/include
CC_K1     ?= musl-gcc
CC_HOST   ?= gcc
CC_FUZZ   ?= clang
FUZZ_CZAS ?= 30

OSTRZEZENIA := -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wconversion -Wshadow \
               -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_K1   := -std=c11 -O2 -D_GNU_SOURCE $(OSTRZEZENIA) -fstack-protector-strong \
               -fstack-clash-protection -fPIE -ftrivial-auto-var-init=zero \
               -ffile-prefix-map=$(CURDIR)=. -isystem $(NAGLOWKI) -Isrc/wspolne
LDFLAGS_K1  := -static-pie -Wl,-z,relro,-z,now,-z,noexecstack -Wl,--build-id=none -s
CFLAGS_HOST := -std=c11 -O1 -g -D_GNU_SOURCE $(OSTRZEZENIA) -fsanitize=address,undefined \
               -fno-sanitize-recover=all -fno-omit-frame-pointer -D_FORTIFY_SOURCE=3 -Isrc/wspolne

WSPOLNE := src/wspolne/k1_log.c src/wspolne/k1_cmdline.c
NAGL    := src/wspolne/k1_log.h src/wspolne/k1_cmdline.h

PROGRAMY := $(BIN)/k1-initramfs $(BIN)/init $(BIN)/k1-test-m1

all: $(PROGRAMY)

$(BIN)/k1-initramfs: src/initramfs/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/initramfs/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/init: src/init/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/init/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/k1-test-m1: testy/m1/k1_test_m1.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ testy/m1/k1_test_m1.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN) $(OBJ):
	mkdir -p $@

# --- testy na hoście ---
$(OBJ)/test_cmdline: testy/host/test_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_cmdline.c src/wspolne/k1_cmdline.c

testy-host: $(OBJ)/test_cmdline
	$(OBJ)/test_cmdline

$(OBJ)/fuzz_cmdline: testy/host/fuzz_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -o $@ testy/host/fuzz_cmdline.c src/wspolne/k1_cmdline.c

fuzz: $(OBJ)/fuzz_cmdline
	mkdir -p $(BUDOWA)/fuzz-korpus
	$(OBJ)/fuzz_cmdline -max_total_time=$(FUZZ_CZAS) -max_len=8192 $(BUDOWA)/fuzz-korpus testy/host/korpus

clean:
	rm -rf $(BIN) $(OBJ)

.PHONY: all testy-host fuzz clean
