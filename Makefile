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
GEN       := $(BUDOWA)/gen
CZCIONKI  ?= /usr/share/fonts/truetype/ibm-plex
WERSJA    := $(shell cat VERSION)

OSTRZEZENIA := -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wconversion -Wshadow \
               -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_K1   := -std=c11 -O2 -D_GNU_SOURCE $(OSTRZEZENIA) -fstack-protector-strong \
               -fstack-clash-protection -fPIE -ftrivial-auto-var-init=zero \
               -ffile-prefix-map=$(CURDIR)=. -isystem $(NAGLOWKI) -Isrc/wspolne -Isrc/grafika -Isrc/przybornik \
               '-DK1_WERSJA="$(WERSJA)"'
LDFLAGS_K1  := -static-pie -Wl,-z,relro,-z,now,-z,noexecstack -Wl,--build-id=none -s
CFLAGS_HOST := -std=c11 -O1 -g -D_GNU_SOURCE $(OSTRZEZENIA) -fsanitize=address,undefined \
               -fno-sanitize-recover=all -fno-omit-frame-pointer -D_FORTIFY_SOURCE=3 -Isrc/wspolne -Isrc/grafika \
               -Isrc/przybornik '-DK1_WERSJA="$(WERSJA)"'

WSPOLNE := src/wspolne/k1_log.c src/wspolne/k1_cmdline.c src/wspolne/k1_zdarzenie.c
NAGL    := src/wspolne/k1_log.h src/wspolne/k1_cmdline.h src/wspolne/k1_zdarzenie.h src/wspolne/k1_uslugi.h
# Services (M3): protocol, channels, sandbox.
USLUGI  := src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c src/wspolne/k1_klatka.c src/wspolne/k1_memfd.c \
           src/wspolne/k1_skarb_klient.c
NAGL_U  := src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h src/wspolne/k1_klatka.h src/wspolne/k1_memfd.h \
           src/wspolne/k1_skarb_klient.h
# Blob Vault (M5c, ADR 0015): format + Monocypher (src/zewnetrzne, docs/ZALEZNOSCI.md). Monocypher is
# compiled as is; on the host (ASan build) -Wconversion warns about its code, so only there it is off.
MONOCYPHER := src/zewnetrzne/monocypher/monocypher.c
MAGAZYN := src/skarbiec/magazyn.c $(MONOCYPHER)
GRAFIKA := src/grafika/k1_grafika.c src/grafika/k1_grafika.h
# GUI toolkit (M4): drawing, fonts (generated at build time), time, keyboard, client, windows.
PRZYB   := src/przybornik/k1_rysuj.c src/przybornik/k1_czas.c src/przybornik/k1_klawiatura.c \
           src/przybornik/k1_okno.c src/przybornik/k1_klient.c src/przybornik/k1_aplikacja.c \
           src/grafika/k1_grafika.c $(GEN)/k1_czcionki_dane.c
NAGL_P  := $(wildcard src/przybornik/*.h) src/grafika/k1_grafika.h src/grafika/k1_czcionka.h VERSION
APLIKACJE := $(BIN)/zegar $(BIN)/notatki $(BIN)/ustawienia $(BIN)/zasoby $(BIN)/instalacja

PROGRAMY := $(BIN)/k1-initramfs $(BIN)/init $(BIN)/nadzorca $(BIN)/wejscie $(BIN)/ekran $(BIN)/gui $(BIN)/skarbiec $(BIN)/logowanie $(BIN)/instalator $(APLIKACJE) \
            $(BIN)/k1-test-systemu

all: $(PROGRAMY) $(GEN)/start.png

# Boot screen for GRUB (0.5.8): the K1 mark drawn on the host with the same code, as a PNG.
$(OBJ)/ekran_startowy: narzedzia/ekran_startowy.c $(PRZYB) $(NAGL_P) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ narzedzia/ekran_startowy.c $(HOST_PRZYB) -lm -lz

$(GEN)/start.png: $(OBJ)/ekran_startowy | $(GEN)
	$(OBJ)/ekran_startowy > $@.tmp && mv $@.tmp $@

$(BIN)/k1-initramfs: src/initramfs/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/initramfs/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/init: src/init/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/init/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/nadzorca: src/nadzorca/main.c src/nadzorca/bateria.c src/nadzorca/bateria.h src/nadzorca/dyski.c src/nadzorca/dyski.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/nadzorca/main.c src/nadzorca/bateria.c src/nadzorca/dyski.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# M5f (ADR 0018): the program that writes the disk, and its partition table (host test: test_gpt)
$(BIN)/instalator: src/instalator/main.c src/instalator/gpt.c src/instalator/gpt.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/instalator/main.c src/instalator/gpt.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

SKARBIEC := src/skarbiec/main.c src/skarbiec/magazyn.c $(MONOCYPHER)
$(BIN)/skarbiec: $(SKARBIEC) src/skarbiec/magazyn.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ $(SKARBIEC) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/wejscie: src/wejscie/main.c $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/wejscie/main.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

EKRAN := src/ekran/main.c src/ekran/scena.c src/ekran/okna.c src/ekran/wskaznik.c src/ekran/drm.c src/ekran/zapis.c
$(BIN)/ekran: $(EKRAN) src/ekran/ekran.h $(GRAFIKA) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(EKRAN) src/grafika/k1_grafika.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/logowanie: src/logowanie/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/logowanie/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/gui: src/gui/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/gui/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/%: src/aplikacje/%/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $< $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# Fonts: rasterised on the host with FreeType (narzedzia/czcionki.c), compiled into the programs.
FT_CFLAGS = $(shell pkg-config --cflags freetype2)
FT_LIBS = $(shell pkg-config --libs freetype2)
$(OBJ)/czcionki: narzedzia/czcionki.c | $(OBJ)
	$(CC_HOST) -std=c11 -O1 -Wall -Wextra -Werror $(FT_CFLAGS) -o $@ narzedzia/czcionki.c $(FT_LIBS)

$(GEN)/k1_czcionki_dane.c: $(OBJ)/czcionki | $(GEN)
	$(OBJ)/czcionki $(CZCIONKI) > $@.tmp && mv $@.tmp $@

TEST_SYS := $(sort $(wildcard testy/system/*.c))
$(BIN)/k1-test-systemu: $(TEST_SYS) testy/system/k1_test.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(TEST_SYS) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN) $(OBJ) $(GEN):
	mkdir -p $@

# --- testy na hoście ---
$(OBJ)/test_cmdline: testy/host/test_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_cmdline.c src/wspolne/k1_cmdline.c

$(OBJ)/test_zdarzenie: testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c src/wspolne/k1_zdarzenie.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c

$(OBJ)/test_komunikat: testy/host/test_komunikat.c src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_komunikat.c src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c

$(OBJ)/test_log: testy/host/test_log.c src/wspolne/k1_log.c src/wspolne/k1_log.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_log.c src/wspolne/k1_log.c

$(OBJ)/test_bateria: testy/host/test_bateria.c src/nadzorca/bateria.c src/nadzorca/bateria.h src/wspolne/k1_komunikat.c src/wspolne/k1_komunikat.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_bateria.c src/nadzorca/bateria.c src/wspolne/k1_komunikat.c

$(OBJ)/podglad_grafiki: testy/host/podglad_grafiki.c $(GRAFIKA) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/podglad_grafiki.c src/grafika/k1_grafika.c -lm

# Host: without the sandbox and the app skeleton (they need our kernel's headers).
HOST_PRZYB := $(filter-out src/przybornik/k1_aplikacja.c,$(PRZYB)) src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c \
              src/wspolne/k1_log.c
$(OBJ)/test_przybornik: testy/host/test_przybornik.c $(PRZYB) $(NAGL_P) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_przybornik.c $(HOST_PRZYB) -lm

$(OBJ)/test_magazyn: testy/host/test_magazyn.c $(MAGAZYN) src/skarbiec/magazyn.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_magazyn.c $(MAGAZYN)

# Sketches of the boot password screen (M5e, for the owner; not in the image).
$(OBJ)/szkice_logowania: testy/host/szkice_logowania.c $(PRZYB) $(NAGL_P) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/szkice_logowania.c $(HOST_PRZYB) -lm

$(OBJ)/test_instalator: testy/host/test_instalator.c src/instalator/gpt.c src/instalator/gpt.h src/nadzorca/dyski.c src/nadzorca/dyski.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/instalator -Isrc/nadzorca -o $@ testy/host/test_instalator.c src/instalator/gpt.c src/nadzorca/dyski.c

testy-host: $(OBJ)/test_cmdline $(OBJ)/test_log $(OBJ)/test_bateria $(OBJ)/test_magazyn $(OBJ)/test_zdarzenie $(OBJ)/test_komunikat $(OBJ)/test_przybornik $(OBJ)/podglad_grafiki $(OBJ)/test_instalator
	$(OBJ)/test_instalator $(OBJ)/gpt.img
	sfdisk -d $(OBJ)/gpt.img | grep -q 'start=        2048, size=     1048576, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B.*name="K1-EFI"'
	sfdisk -d $(OBJ)/gpt.img | grep -q 'start=     1050624, size=     7335936, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4.*name="K1-DANE"'
	sfdisk --verify $(OBJ)/gpt.img >/dev/null
	rm -f $(OBJ)/gpt.img
	@echo "test_instalator: tablica GPT odczytana przez sfdisk (K1-EFI, K1-DANE)"
	$(OBJ)/test_cmdline
	$(OBJ)/test_log
	$(OBJ)/test_bateria
	$(OBJ)/test_magazyn
	$(OBJ)/test_zdarzenie
	$(OBJ)/test_komunikat
	$(OBJ)/test_przybornik
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

$(OBJ)/fuzz_magazyn: testy/host/fuzz_magazyn.c $(MAGAZYN) src/skarbiec/magazyn.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/fuzz_magazyn.c $(MAGAZYN)

fuzz: $(OBJ)/fuzz_cmdline $(OBJ)/fuzz_zdarzenie $(OBJ)/fuzz_komunikat $(OBJ)/fuzz_magazyn
	mkdir -p $(BUDOWA)/fuzz-korpus $(BUDOWA)/fuzz-korpus-zdarzenie $(BUDOWA)/fuzz-korpus-komunikat
	$(OBJ)/fuzz_cmdline -max_total_time=$(FUZZ_CZAS) -max_len=8192 $(BUDOWA)/fuzz-korpus testy/host/korpus
	$(OBJ)/fuzz_zdarzenie -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-zdarzenie
	$(OBJ)/fuzz_komunikat -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-komunikat
	mkdir -p $(BUDOWA)/fuzz-korpus-magazyn
	$(OBJ)/fuzz_magazyn -max_total_time=$(FUZZ_CZAS) -max_len=16384 $(BUDOWA)/fuzz-korpus-magazyn

clean:
	rm -rf $(BIN) $(OBJ)

.PHONY: all testy-host fuzz clean
