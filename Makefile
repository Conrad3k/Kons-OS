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
               -ffile-prefix-map=$(CURDIR)=. -isystem $(NAGLOWKI) -Isrc/wspolne -Isrc/grafika
LDFLAGS_K1  := -static-pie -Wl,-z,relro,-z,now,-z,noexecstack -Wl,--build-id=none -s
CFLAGS_HOST := -std=c11 -O1 -g -D_GNU_SOURCE $(OSTRZEZENIA) -fsanitize=address,undefined \
               -fno-sanitize-recover=all -fno-omit-frame-pointer -D_FORTIFY_SOURCE=3 -Isrc/wspolne -Isrc/grafika

WSPOLNE := src/wspolne/k1_log.c src/wspolne/k1_cmdline.c src/wspolne/k1_zdarzenie.c
NAGL    := src/wspolne/k1_log.h src/wspolne/k1_cmdline.h src/wspolne/k1_zdarzenie.h src/wspolne/k1_uslugi.h
# Services (M3): protocol, channels, sandbox.
USLUGI  := src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c src/wspolne/k1_klatka.c
NAGL_U  := src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h src/wspolne/k1_klatka.h
GRAFIKA := src/grafika/k1_grafika.c src/grafika/k1_grafika.h

PROGRAMY := $(BIN)/k1-initramfs $(BIN)/init $(BIN)/nadzorca $(BIN)/wejscie $(BIN)/ekran $(BIN)/k1-test-systemu

all: $(PROGRAMY)

$(BIN)/k1-initramfs: src/initramfs/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/initramfs/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/init: src/init/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/init/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/nadzorca: src/nadzorca/main.c $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/nadzorca/main.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/wejscie: src/wejscie/main.c $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/wejscie/main.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/ekran: src/ekran/main.c $(GRAFIKA) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/ekran/main.c src/grafika/k1_grafika.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/k1-test-systemu: testy/system/k1_test_systemu.c $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ testy/system/k1_test_systemu.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN) $(OBJ):
	mkdir -p $@

# --- testy na hoście ---
$(OBJ)/test_cmdline: testy/host/test_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_cmdline.c src/wspolne/k1_cmdline.c

$(OBJ)/test_zdarzenie: testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c src/wspolne/k1_zdarzenie.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c

$(OBJ)/test_komunikat: testy/host/test_komunikat.c src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_komunikat.c src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c

$(OBJ)/podglad_grafiki: testy/host/podglad_grafiki.c $(GRAFIKA) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/podglad_grafiki.c src/grafika/k1_grafika.c -lm

testy-host: $(OBJ)/test_cmdline $(OBJ)/test_zdarzenie $(OBJ)/test_komunikat $(OBJ)/podglad_grafiki
	$(OBJ)/test_cmdline
	$(OBJ)/test_zdarzenie
	$(OBJ)/test_komunikat
	$(OBJ)/podglad_grafiki 1280 800 900 400 0 > $(OBJ)/podglad.ppm
	$(OBJ)/podglad_grafiki 13 7 -50 3 1 > /dev/null
	$(OBJ)/podglad_grafiki 1 1 0 0 0 > /dev/null
	@echo "podglad_grafiki: OK (ASan/UBSan, rozmiary 1280x800, 13x7, 1x1)"

$(OBJ)/fuzz_cmdline: testy/host/fuzz_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -o $@ testy/host/fuzz_cmdline.c src/wspolne/k1_cmdline.c

$(OBJ)/fuzz_zdarzenie: testy/host/fuzz_zdarzenie.c src/wspolne/k1_zdarzenie.c src/wspolne/k1_zdarzenie.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -o $@ testy/host/fuzz_zdarzenie.c src/wspolne/k1_zdarzenie.c

$(OBJ)/fuzz_komunikat: testy/host/fuzz_komunikat.c src/wspolne/k1_komunikat.c src/wspolne/k1_komunikat.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -o $@ testy/host/fuzz_komunikat.c src/wspolne/k1_komunikat.c

fuzz: $(OBJ)/fuzz_cmdline $(OBJ)/fuzz_zdarzenie $(OBJ)/fuzz_komunikat
	mkdir -p $(BUDOWA)/fuzz-korpus $(BUDOWA)/fuzz-korpus-zdarzenie $(BUDOWA)/fuzz-korpus-komunikat
	$(OBJ)/fuzz_cmdline -max_total_time=$(FUZZ_CZAS) -max_len=8192 $(BUDOWA)/fuzz-korpus testy/host/korpus
	$(OBJ)/fuzz_zdarzenie -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-zdarzenie
	$(OBJ)/fuzz_komunikat -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-komunikat

clean:
	rm -rf $(BIN) $(OBJ)

.PHONY: all testy-host fuzz clean
