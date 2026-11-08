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
USLUGI  := src/wspolne/k1_uslugi.c src/wspolne/k1_komunikat.c src/wspolne/k1_kanal.c src/wspolne/k1_klatka.c src/wspolne/k1_memfd.c \
           src/wspolne/k1_skarb_klient.c src/wspolne/k1_odbior.c src/wspolne/k1_bat_arch.c
NAGL_U  := src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h src/wspolne/k1_klatka.h src/wspolne/k1_memfd.h \
           src/wspolne/k1_skarb_klient.h src/wspolne/k1_odbior.h src/wspolne/k1_bat_arch.h
# Blob Vault (M5c, ADR 0015): format + Monocypher (src/zewnetrzne, docs/ZALEZNOSCI.md). Monocypher is
# compiled as is; on the host (ASan build) -Wconversion warns about its code, so only there it is off.
MONOCYPHER := src/zewnetrzne/monocypher/monocypher.c
MAGAZYN := src/skarbiec/magazyn.c $(MONOCYPHER)
GRAFIKA := src/grafika/k1_grafika.c src/grafika/k1_grafika.h
# GUI toolkit (M4): drawing, fonts (generated at build time), time, keyboard, client, windows.
PRZYB   := src/przybornik/k1_rysuj.c src/przybornik/k1_liczba.c src/przybornik/k1_czas.c src/przybornik/k1_klawiatura.c \
           src/przybornik/k1_okno.c src/przybornik/k1_klient.c src/przybornik/k1_aplikacja.c \
           src/grafika/k1_grafika.c $(GEN)/k1_czcionki_dane.c
NAGL_P  := $(wildcard src/przybornik/*.h) src/grafika/k1_grafika.h src/grafika/k1_czcionka.h VERSION
# Native applications: every directory src/aplikacje/<program> (its line in k1_uslugi.h K1_APLIKACJE names it).
APLIKACJE := $(patsubst src/aplikacje/%/main.c,$(BIN)/%,$(wildcard src/aplikacje/*/main.c))

PROGRAMY := $(BIN)/k1-initramfs $(BIN)/init $(BIN)/nadzorca $(BIN)/wejscie $(BIN)/ekran $(BIN)/gui $(BIN)/skarbiec $(BIN)/logowanie $(BIN)/instalator $(APLIKACJE) \
            $(BIN)/k1-test-systemu

all: $(PROGRAMY) $(GEN)/start.png $(OBJ)/podpisz $(GEN)/sb/k1_sb_cert.h

# M6a (ADR 0021): manifests and signatures of system images. The key comes from the environment
# (K1_KLUCZ_PODPISU, the CI secret); without it: a developer build (unsigned, no key in K1).
MANIFEST := src/wspolne/k1_manifest.c src/wspolne/k1_manifest.h src/wspolne/k1_uklad.c src/wspolne/k1_uklad.h $(MONOCYPHER)
$(OBJ)/podpisz: narzedzia/podpisz.c $(MANIFEST) | $(OBJ)
	$(CC_HOST) -std=c11 -O2 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc/wspolne -Isrc/zewnetrzne/monocypher \
		-o $@ narzedzia/podpisz.c src/wspolne/k1_manifest.c src/wspolne/k1_uklad.c $(MONOCYPHER)

# regenerated on every build, replaced only when it changed (the key may differ between builds)
$(GEN)/k1_klucz.h: $(OBJ)/podpisz FORCE | $(GEN)
	$(OBJ)/podpisz klucz-publiczny > $@.tmp && { cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@; }
FORCE:

# M6d (ADR 0022): the Secure Boot key derived from the same seed (narzedzia/klucz_sb.py) — the private key for
# scripts/zbuduj-k1efi.sh (deleted there after signing), the certificate for nadzorca (MOK enrolment). The
# header is replaced only when it changed, so programs are not rebuilt every time.
$(GEN)/sb/k1_sb_cert.h: narzedzia/klucz_sb.py FORCE | $(GEN)
	python3 -I narzedzia/klucz_sb.py $(GEN)/sb.nowy >/dev/null && mkdir -p $(GEN)/sb && \
	mv -f $(GEN)/sb.nowy/k1-sb.key $(GEN)/sb.nowy/k1-sb.crt $(GEN)/sb.nowy/k1-sb.cer $(GEN)/sb/ && \
	{ cmp -s $(GEN)/sb.nowy/k1_sb_cert.h $@ || mv -f $(GEN)/sb.nowy/k1_sb_cert.h $@; } && rm -rf $(GEN)/sb.nowy

# Boot screen for GRUB (0.5.8): the K1 mark drawn on the host with the same code, as a PNG.
$(OBJ)/ekran_startowy: narzedzia/ekran_startowy.c $(PRZYB) $(NAGL_P) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ narzedzia/ekran_startowy.c $(HOST_PRZYB) -lm -lz

$(GEN)/start.png: $(OBJ)/ekran_startowy | $(GEN)
	$(OBJ)/ekran_startowy > $@.tmp && mv $@.tmp $@

$(BIN)/k1-initramfs: src/initramfs/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/initramfs/main.c $(WSPOLNE) $(LDFLAGS_K1)

$(BIN)/init: src/init/main.c $(WSPOLNE) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/init/main.c $(WSPOLNE) $(LDFLAGS_K1)

# nadzorca in modules (0.6.0, ADR 0019): main.c (loop), uslugi, profile, urzadzenia, skarb, instalacja, zasoby,
# test; pure parts with host tests: bateria, dyski, uevent.
NADZORCA := $(sort $(wildcard src/nadzorca/*.c))
$(BIN)/nadzorca: $(NADZORCA) $(wildcard src/nadzorca/*.h) $(MANIFEST) $(GEN)/sb/k1_sb_cert.h src/wspolne/k1_sha256.c src/wspolne/k1_sha256.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/zewnetrzne/monocypher -I$(GEN)/sb -o $@ $(NADZORCA) src/wspolne/k1_manifest.c \
		src/wspolne/k1_uklad.c src/wspolne/k1_sha256.c $(MONOCYPHER) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# M5f (ADR 0018): the program that writes the disk, and its partition table (host test: test_gpt)
$(BIN)/instalator: src/instalator/main.c src/instalator/gpt.c src/instalator/gpt.h $(MANIFEST) $(GEN)/k1_klucz.h $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_U) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/zewnetrzne/monocypher -I$(GEN) -o $@ src/instalator/main.c src/instalator/gpt.c \
		src/wspolne/k1_manifest.c src/wspolne/k1_uklad.c $(MONOCYPHER) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

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

# an application is every .c file of its directory (the Kalkulator: main.c + its engine liczydlo.c)
.SECONDEXPANSION:
$(BIN)/%: src/aplikacje/%/main.c $$(wildcard src/aplikacje/$$*/*.c src/aplikacje/$$*/*.h) $(PRZYB) $(WSPOLNE) $(USLUGI) \
          $(NAGL) $(NAGL_U) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(wildcard src/aplikacje/$*/*.c) $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

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

$(OBJ)/test_uevent: testy/host/test_uevent.c src/nadzorca/uevent.c src/nadzorca/uevent.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_uevent.c src/nadzorca/uevent.c

$(OBJ)/test_odbior: testy/host/test_odbior.c src/wspolne/k1_odbior.c src/wspolne/k1_odbior.h src/wspolne/k1_kanal.c src/wspolne/k1_komunikat.c | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_odbior.c src/wspolne/k1_odbior.c src/wspolne/k1_kanal.c src/wspolne/k1_komunikat.c

$(OBJ)/test_bat_arch: testy/host/test_bat_arch.c src/wspolne/k1_bat_arch.c src/wspolne/k1_bat_arch.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_bat_arch.c src/wspolne/k1_bat_arch.c

$(OBJ)/test_manifest: testy/host/test_manifest.c $(MANIFEST) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_manifest.c src/wspolne/k1_manifest.c $(MONOCYPHER)

$(OBJ)/test_uklad: testy/host/test_uklad.c src/wspolne/k1_uklad.c src/wspolne/k1_uklad.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_uklad.c src/wspolne/k1_uklad.c $(MONOCYPHER)

$(OBJ)/test_sha256: testy/host/test_sha256.c src/wspolne/k1_sha256.c src/wspolne/k1_sha256.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_sha256.c src/wspolne/k1_sha256.c

$(OBJ)/test_kalkulator: testy/host/test_kalkulator.c src/aplikacje/kalkulator/liczydlo.c src/aplikacje/kalkulator/liczydlo.h \
                       src/przybornik/k1_liczba.c src/przybornik/k1_liczba.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/aplikacje/kalkulator -o $@ testy/host/test_kalkulator.c \
		src/aplikacje/kalkulator/liczydlo.c src/przybornik/k1_liczba.c -lm

testy-host: $(OBJ)/test_kalkulator $(OBJ)/test_sha256 $(OBJ)/test_uklad $(OBJ)/test_manifest $(OBJ)/test_bat_arch $(OBJ)/test_odbior $(OBJ)/test_uevent $(OBJ)/test_cmdline $(OBJ)/test_log $(OBJ)/test_bateria $(OBJ)/test_magazyn $(OBJ)/test_zdarzenie $(OBJ)/test_komunikat $(OBJ)/test_przybornik $(OBJ)/podglad_grafiki $(OBJ)/test_instalator
	$(OBJ)/test_kalkulator
	rm -f $(OBJ)/grubenv.ref && grub-editenv $(OBJ)/grubenv.ref create && \
		grub-editenv $(OBJ)/grubenv.ref set k1_slot=a k1_start=a k1_nowy= k1_proba=
	$(OBJ)/test_uklad $(OBJ)/grubenv.ref
	$(OBJ)/test_manifest
	$(OBJ)/test_sha256
	$(OBJ)/test_bat_arch
	$(OBJ)/test_odbior
	$(OBJ)/test_uevent
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

$(OBJ)/fuzz_uevent: testy/host/fuzz_uevent.c src/nadzorca/uevent.c src/nadzorca/uevent.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/nadzorca -o $@ testy/host/fuzz_uevent.c src/nadzorca/uevent.c

$(OBJ)/fuzz_bat_arch: testy/host/fuzz_bat_arch.c src/wspolne/k1_bat_arch.c src/wspolne/k1_bat_arch.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -o $@ testy/host/fuzz_bat_arch.c src/wspolne/k1_bat_arch.c

$(OBJ)/fuzz_manifest: testy/host/fuzz_manifest.c $(MANIFEST) | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=all -Isrc/wspolne -Isrc/zewnetrzne/monocypher -o $@ testy/host/fuzz_manifest.c \
		src/wspolne/k1_manifest.c $(MONOCYPHER)

$(OBJ)/fuzz_uklad: testy/host/fuzz_uklad.c src/wspolne/k1_uklad.c src/wspolne/k1_uklad.h | $(OBJ)
	$(CC_FUZZ) -std=c11 -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -Isrc/wspolne \
		-Isrc/zewnetrzne/monocypher -o $@ testy/host/fuzz_uklad.c src/wspolne/k1_uklad.c $(MONOCYPHER)

fuzz: $(OBJ)/fuzz_uklad $(OBJ)/fuzz_manifest $(OBJ)/fuzz_cmdline $(OBJ)/fuzz_zdarzenie $(OBJ)/fuzz_komunikat $(OBJ)/fuzz_magazyn $(OBJ)/fuzz_uevent $(OBJ)/fuzz_bat_arch
	mkdir -p $(BUDOWA)/fuzz-korpus $(BUDOWA)/fuzz-korpus-zdarzenie $(BUDOWA)/fuzz-korpus-komunikat
	$(OBJ)/fuzz_cmdline -max_total_time=$(FUZZ_CZAS) -max_len=8192 $(BUDOWA)/fuzz-korpus testy/host/korpus
	$(OBJ)/fuzz_zdarzenie -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-zdarzenie
	$(OBJ)/fuzz_komunikat -max_total_time=$(FUZZ_CZAS) -max_len=64 $(BUDOWA)/fuzz-korpus-komunikat
	mkdir -p $(BUDOWA)/fuzz-korpus-magazyn
	$(OBJ)/fuzz_magazyn -max_total_time=$(FUZZ_CZAS) -max_len=16384 $(BUDOWA)/fuzz-korpus-magazyn
	mkdir -p $(BUDOWA)/fuzz-korpus-uevent
	$(OBJ)/fuzz_uevent -max_total_time=$(FUZZ_CZAS) -max_len=8192 $(BUDOWA)/fuzz-korpus-uevent
	mkdir -p $(BUDOWA)/fuzz-korpus-manifest
	$(OBJ)/fuzz_manifest -max_total_time=$(FUZZ_CZAS) -max_len=512 $(BUDOWA)/fuzz-korpus-manifest
	mkdir -p $(BUDOWA)/fuzz-korpus-bat-arch
	$(OBJ)/fuzz_bat_arch -max_total_time=$(FUZZ_CZAS) -max_len=9000 $(BUDOWA)/fuzz-korpus-bat-arch
	mkdir -p $(BUDOWA)/fuzz-korpus-uklad
	$(OBJ)/fuzz_uklad -max_total_time=$(FUZZ_CZAS) -max_len=1024 $(BUDOWA)/fuzz-korpus-uklad

clean:
	rm -rf $(BIN) $(OBJ)

.PHONY: all testy-host fuzz clean FORCE
