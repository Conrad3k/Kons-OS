# K1 — budowa programów natywnych (C, musl, statycznie) i testów na hoście.
#
#   make            programy K1 → build/bin/ (wymaga nagłówków jądra: scripts/zbuduj-jadro.sh)
#   make testy-host testy jednostkowe na hoście (glibc + ASan/UBSan); równolegle: make -j4 -O testy-host
#   make fuzz       fuzzing parserów (clang + libFuzzer, lista FUZZ), FUZZ_CZAS sekund każdy; równolegle: make -j4 -O fuzz
#
# Flagi: SPEC §11, zweryfikowane w ADR 0002; scripts/sprawdz-binaria.sh sprawdza wynik.
# Biblioteka C: nasz musl (0.9.0 krok 4: musl/, scripts/zbuduj-musl.sh → build/musl), kontrola przepełnień w funkcjach
# biblioteki: fortify-headers (src/zewnetrzne/fortify-headers, _FORTIFY_SOURCE=3; korekta ADR 0007).

BUDOWA    ?= build
BIN       := $(BUDOWA)/bin
OBJ       := $(BUDOWA)/obj
NAGLOWKI  ?= $(BUDOWA)/naglowki/include
MUSL      := $(BUDOWA)/musl/bin/musl-gcc
CC_K1     ?= $(MUSL)
CC_HOST   ?= gcc
CC_FUZZ   ?= clang
FUZZ_CZAS ?= 30
GEN       := $(BUDOWA)/gen
CZCIONKI  ?= /usr/share/fonts/truetype/ibm-plex
WERSJA    := $(shell cat VERSION)
# The build time (skarbiec's lower bound for the clock, 0.8.1): the time of the commit, as scripts/wspolne.sh sets it.
CZAS_BUDOWY := $(or $(SOURCE_DATE_EPOCH),$(shell git log -1 --format=%ct 2>/dev/null),0)

OSTRZEZENIA := -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wconversion -Wshadow \
               -Wstrict-prototypes -Wmissing-prototypes
# fortify-headers first (they overlay musl's headers with #include_next), as system headers (no warnings from them)
CFLAGS_K1   := -std=c11 -O2 -D_GNU_SOURCE -D_FORTIFY_SOURCE=3 $(OSTRZEZENIA) -fstack-protector-strong \
               -fstack-clash-protection -fPIE -ftrivial-auto-var-init=zero -ffile-prefix-map=$(CURDIR)=. \
               -isystem src/zewnetrzne/fortify-headers/include -isystem $(NAGLOWKI) -Isrc/wspolne -Isrc/grafika -Isrc/przybornik \
               '-DK1_WERSJA="$(WERSJA)"'
LDFLAGS_K1  := -static-pie -Wl,-z,relro,-z,now,-z,noexecstack -Wl,--build-id=none -s
CFLAGS_HOST := -std=c11 -O1 -g -D_GNU_SOURCE $(OSTRZEZENIA) -fsanitize=address,undefined \
               -fno-sanitize-recover=all -fno-omit-frame-pointer -D_FORTIFY_SOURCE=3 -Isrc/wspolne -Isrc/grafika \
               -Isrc/przybornik '-DK1_WERSJA="$(WERSJA)"'

# The user's files: entries, types, names (k1_dokument.h) — linked by services, Pliki and host tests.
DOK_C   := src/wspolne/k1_dokument.c src/wspolne/k1_nazwy.c
WSPOLNE := src/wspolne/k1_log.c src/wspolne/k1_cmdline.c src/wspolne/k1_zdarzenie.c
# Every program depends on all shared headers: one list to keep, a header change rebuilds a little more.
NAGL    := $(wildcard src/wspolne/*.h)
# The message of the services and its checks, one file per group of channels (0.9.0 step 2).
KOMUNIKAT := src/wspolne/k1_komunikat.c src/wspolne/k1_kom_ekran.c src/wspolne/k1_kom_skarb.c \
             src/wspolne/k1_kom_nadzorca.c src/wspolne/k1_kom_pliki.c src/wspolne/k1_kom_siec.c src/wspolne/k1_siec.c \
             src/wspolne/k1_kom_wifi.c src/wspolne/k1_wifi.c src/wspolne/k1_kom_strefa.c src/wspolne/k1_opis.c
# Services (M3): protocol, channels, sandbox.
USLUGI  := src/wspolne/k1_uslugi.c $(KOMUNIKAT) src/wspolne/k1_kanal.c src/wspolne/k1_klatka.c src/wspolne/k1_memfd.c \
           src/wspolne/k1_skarb_klient.c src/wspolne/k1_odbior.c src/wspolne/k1_bat_arch.c \
           $(DOK_C) src/wspolne/k1_pendrive.c
# Blob Vault (M5c, ADR 0015): format + Monocypher (src/zewnetrzne, docs/ZALEZNOSCI.md). Monocypher is
# compiled as is; on the host (ASan build) -Wconversion warns about its code, so only there it is off.
MONOCYPHER := src/zewnetrzne/monocypher/monocypher.c
MAGAZYN_C := src/skarbiec/magazyn.c src/skarbiec/naglowek.c src/skarbiec/odcinki.c src/skarbiec/spis.c
MAGAZYN := $(MAGAZYN_C) src/wspolne/k1_indeks.c $(MONOCYPHER)
GRAFIKA := src/grafika/k1_grafika.c src/grafika/k1_grafika.h
# GUI toolkit (M4): drawing, fonts (generated at build time), time, keyboard, client, windows.
PRZYB   := src/przybornik/k1_rysuj.c src/przybornik/k1_wachlarz.c src/przybornik/k1_liczba.c src/przybornik/k1_czas.c src/przybornik/k1_klawiatura.c \
           src/przybornik/k1_okno.c src/przybornik/k1_klient.c src/przybornik/k1_opis_klient.c src/przybornik/k1_aplikacja.c \
           src/przybornik/k1_dekodowanie.c src/przybornik/k1_menu.c src/przybornik/k1_przeciaganie.c \
           src/przybornik/k1_zaznaczenie.c src/wspolne/k1_skaluj.c src/grafika/k1_grafika.c \
           $(GEN)/k1_czcionki_dane.c
# Host: without the sandbox, the app skeleton and what uses it (they need our kernel's headers).
HOST_PRZYB := $(filter-out src/przybornik/k1_aplikacja.c src/przybornik/k1_dekodowanie.c,$(PRZYB)) $(KOMUNIKAT) $(DOK_C) src/wspolne/k1_kanal.c \
              src/wspolne/k1_log.c src/wspolne/k1_uslugi.c src/wspolne/k1_memfd.c
NAGL_P  := $(wildcard src/przybornik/*.h) src/grafika/k1_grafika.h src/grafika/k1_czcionka.h VERSION
# Native applications: every directory src/aplikacje/<program> (its line in k1_uslugi.h K1_APLIKACJE names it).
APLIKACJE := $(patsubst src/aplikacje/%/main.c,$(BIN)/%,$(wildcard src/aplikacje/*/main.c))

# System services: one line each in K1_SYSTEM (src/wspolne/k1_uslugi.h, the only list): scripts/uslugi-systemu.sh.
USLUGI_SYSTEMU := $(shell sh scripts/uslugi-systemu.sh)
PROGRAMY := $(BIN)/k1-initramfs $(BIN)/init $(BIN)/nadzorca $(addprefix $(BIN)/,$(USLUGI_SYSTEMU)) $(APLIKACJE) \
            $(BIN)/strefa $(BIN)/gosc $(BIN)/kompozytor $(BIN)/przekaznik $(BIN)/k1-test-systemu

all: $(PROGRAMY) $(GEN)/start.png $(OBJ)/podpisz $(GEN)/sb/k1_sb_cert.h $(OBJ)/proba_fortify

# Our musl (0.9.0 step 4): built when musl/ changes; every program is built again with a new one.
$(MUSL): musl/wersja.env $(wildcard musl/latki/*.patch) scripts/zbuduj-musl.sh
	K1_BUDOWA=$(abspath $(BUDOWA)) sh scripts/zbuduj-musl.sh
$(PROGRAMY) $(OBJ)/proba_fortify: $(MUSL) $(wildcard src/zewnetrzne/fortify-headers/include/*.h src/zewnetrzne/fortify-headers/include/*/*.h)
# the probe of _FORTIFY_SOURCE with the flags of the K1 programs (scripts/sprawdz-binaria.sh runs it)
$(OBJ)/proba_fortify: testy/host/proba_fortify.c | $(OBJ)
	$(CC_K1) $(CFLAGS_K1) -o $@ testy/host/proba_fortify.c $(LDFLAGS_K1)

# M6a (ADR 0021): manifests and signatures of system images. The key comes from the environment
# (K1_KLUCZ_PODPISU, the CI secret); without it: a developer build (unsigned, no key in K1).
MANIFEST := src/wspolne/k1_manifest.c src/wspolne/k1_uklad.c $(MONOCYPHER)
# the boot files of a K1-EFI as a tree (0.9.0 step 3, ADR 0038): the build and the installer
FAT32    := src/wspolne/k1_fat32.c
# what nadzorca and the installer read from a K1-EFI partition (k1_efi.h)
EFI      := $(MANIFEST) src/wspolne/k1_efi.c
$(OBJ)/podpisz: narzedzia/podpisz.c $(MANIFEST) $(FAT32) $(NAGL) | $(OBJ)
	$(CC_HOST) -std=c11 -O2 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc/wspolne -Isrc/zewnetrzne/monocypher \
		-o $@ narzedzia/podpisz.c $(MANIFEST) $(FAT32)

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
$(OBJ)/ekran_startowy: narzedzia/ekran_startowy.c $(HOST_PRZYB) $(NAGL_P) $(NAGL) | $(OBJ)
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
# 0.8.0 step 5: nadzorca grows an empty K1-DANE to the end of the stick with the installer's GPT code (powieksz.c)
$(BIN)/nadzorca: $(NADZORCA) $(wildcard src/nadzorca/*.h) src/wspolne/k1_nl.c src/wspolne/k1_nl.h src/instalator/gpt.c src/instalator/gpt.h $(EFI) $(GEN)/sb/k1_sb_cert.h src/wspolne/k1_sha256.c src/wspolne/k1_sha1.c $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -DK1_CZAS_BUDOWY=$(CZAS_BUDOWY) -Isrc/zewnetrzne/monocypher -Isrc/instalator -I$(GEN)/sb -o $@ $(NADZORCA) src/instalator/gpt.c $(EFI) \
		src/wspolne/k1_sha256.c src/wspolne/k1_sha1.c src/wspolne/k1_nl.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# M5f (ADR 0018): the program that writes the disk, and its partition table (host test: test_gpt)
# 0.9.0 step 3 (ADR 0038): start.c — the boot files swapped beside the old ones (host test: test_start)
INSTALATOR := $(sort $(wildcard src/instalator/*.c))
$(BIN)/instalator: $(INSTALATOR) $(wildcard src/instalator/*.h) $(EFI) $(FAT32) $(GEN)/k1_klucz.h $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/zewnetrzne/monocypher -I$(GEN) -o $@ $(INSTALATOR) \
		$(EFI) $(FAT32) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# 0.8.0 step 3 (ADR 0031): the USB stick reader — FAT in a sandbox (host test: test_fat, fuzzing: fuzz_fat)
# 0.8.0 step 4 (ADR 0032): the picture decoder — Wuffs (src/zewnetrzne, docs/ZALEZNOSCI.md) compiled as is inside
# src/dekoder/wuffs.c; obraz.c and wuffs.c are also built on the host (test_dekoder, fuzz_dekoder).
WUFFS   := src/zewnetrzne/wuffs/wuffs-v0.4.c
DEKODER_OBRAZ := src/dekoder/obraz.c src/dekoder/wuffs.c src/wspolne/k1_skaluj.c
$(BIN)/dekoder: src/dekoder/main.c $(DEKODER_OBRAZ) src/dekoder/obraz.h $(WUFFS) $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/zewnetrzne/wuffs -o $@ src/dekoder/main.c $(DEKODER_OBRAZ) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

CZYTNIK := $(sort $(wildcard src/czytnik/*.c))
CZYTNIK_FAT := src/czytnik/fat.c src/czytnik/nazwy.c
$(BIN)/czytnik: $(CZYTNIK) src/czytnik/fat.h src/przybornik/k1_czas.c src/wspolne/k1_indeks.c $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(CZYTNIK) src/przybornik/k1_czas.c src/wspolne/k1_indeks.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# skarbiec in modules (0.8.0): main.c (loop, channels), obiekty, haslo, dokumenty, dok_kanal + the format — skarbiec.h
SKARBIEC := $(filter-out $(MAGAZYN_C),$(sort $(wildcard src/skarbiec/*.c))) $(MAGAZYN)
$(BIN)/skarbiec: $(SKARBIEC) $(wildcard src/skarbiec/*.h) $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -DK1_CZAS_BUDOWY=$(CZAS_BUDOWY) -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ $(SKARBIEC) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# 0.9.0 step 6 (ADR 0039): the address from the router — DHCP packets and the client (host test: test_siec, fuzz_dhcp)
SIEC := $(sort $(wildcard src/siec/*.c))
$(BIN)/siec: $(SIEC) $(wildcard src/siec/*.h) src/wspolne/k1_sha1.c src/wspolne/k1_aes.c $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(SIEC) src/wspolne/k1_sha1.c src/wspolne/k1_aes.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# 0.10.0 step 1 (ADR 0042): the VMM of a zone — one process per micro machine (host tests: test_virtio, test_strefa_start;
# fuzzing: fuzz_virtq), its window drawn with a part of przybornik; gosc — /init of the test zone's image (it runs inside
# the machine, not in K1; our flags all the same)
STREFA := $(sort $(wildcard src/strefa/*.c))
STREFA_PRZYB := src/przybornik/k1_rysuj.c src/przybornik/k1_okno.c src/przybornik/k1_klient.c src/przybornik/k1_opis_klient.c src/grafika/k1_grafika.c \
                $(GEN)/k1_czcionki_dane.c
$(BIN)/strefa: $(STREFA) $(wildcard src/strefa/*.h) $(STREFA_PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(STREFA) $(STREFA_PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)
GOSC := $(sort $(wildcard src/strefa/gosc/*.c))
$(BIN)/gosc: $(GOSC) $(wildcard src/strefa/gosc/*.h) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(GOSC) $(LDFLAGS_K1)
# 0.10.0 step 2 (ADR 0043): the browser zone's own programs — the Wayland compositor (with gosc's display, kms.c) and
# the relay of the browser's connections; they run inside the machine, built like gosc
KOMPOZYTOR := $(sort $(wildcard src/strefa/kompozytor/*.c)) src/strefa/gosc/kms.c
$(BIN)/kompozytor: $(KOMPOZYTOR) $(wildcard src/strefa/kompozytor/*.h) src/strefa/gosc/kms.h | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -Isrc/strefa/gosc -o $@ $(KOMPOZYTOR) $(LDFLAGS_K1)
$(BIN)/przekaznik: src/strefa/przekaznik/main.c | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/strefa/przekaznik/main.c $(LDFLAGS_K1)

$(BIN)/wejscie: src/wejscie/main.c src/wejscie/touchpad.c src/wejscie/touchpad.h $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/wejscie/main.c src/wejscie/touchpad.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# 0.10.0 (ADR 0042): the frame of the zones' windows (strefy.c) is drawn with przybornik's shapes, icons and text
EKRAN := src/ekran/main.c src/ekran/scena.c src/ekran/okna.c src/ekran/powierzchnie.c src/ekran/wskaznik.c src/ekran/drm.c src/ekran/zapis.c \
         src/ekran/zdjecie.c src/ekran/zalegle.c src/ekran/strefy.c src/ekran/opis.c src/wspolne/k1_skaluj.c
EKRAN_PRZYB := src/przybornik/k1_rysuj.c src/przybornik/k1_okno.c src/przybornik/k1_opis_klient.c $(GEN)/k1_czcionki_dane.c
$(BIN)/ekran: $(EKRAN) src/ekran/ekran.h src/ekran/zalegle.h $(EKRAN_PRZYB) $(GRAFIKA) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(EKRAN) $(EKRAN_PRZYB) src/grafika/k1_grafika.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN)/logowanie: src/logowanie/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ src/logowanie/main.c $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# the desktop shell in modules (0.7.5): main.c (loop), okna, pasek_gorny, pasek_dolny, menu_start — gui.h
GUI := $(sort $(wildcard src/gui/*.c))
$(BIN)/gui: $(GUI) src/gui/gui.h $(PRZYB) $(WSPOLNE) $(USLUGI) $(NAGL) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(GUI) $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# an application is every .c file of its directory (the Kalkulator: main.c + its engine liczydlo.c)
.SECONDEXPANSION:
$(BIN)/%: src/aplikacje/%/main.c $$(wildcard src/aplikacje/$$*/*.c src/aplikacje/$$*/*.h) $(PRZYB) $(WSPOLNE) $(USLUGI) \
          $(NAGL) $(NAGL_P) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(wildcard src/aplikacje/$*/*.c) $(PRZYB) $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

# Fonts: rasterised on the host with FreeType (narzedzia/czcionki.c), compiled into the programs.
FT_CFLAGS = $(shell pkg-config --cflags freetype2)
FT_LIBS = $(shell pkg-config --libs freetype2)
$(OBJ)/czcionki: narzedzia/czcionki.c | $(OBJ)
	$(CC_HOST) -std=c11 -O1 -Wall -Wextra -Werror $(FT_CFLAGS) -o $@ narzedzia/czcionki.c $(FT_LIBS)

$(GEN)/k1_czcionki_dane.c: $(OBJ)/czcionki | $(GEN)
	$(OBJ)/czcionki $(CZCIONKI) > $@.tmp && mv $@.tmp $@

TEST_SYS := $(sort $(wildcard testy/system/*.c))
$(BIN)/k1-test-systemu: $(TEST_SYS) testy/system/k1_test.h src/wspolne/k1_nl.c src/wspolne/k1_nl.h src/wspolne/k1_sha1.c src/wspolne/k1_aes.c $(WSPOLNE) $(USLUGI) $(NAGL) | $(BIN)
	$(CC_K1) $(CFLAGS_K1) -o $@ $(TEST_SYS) src/wspolne/k1_nl.c src/wspolne/k1_sha1.c src/wspolne/k1_aes.c $(WSPOLNE) $(USLUGI) $(LDFLAGS_K1)

$(BIN) $(OBJ) $(GEN):
	mkdir -p $@

# --- testy na hoście ---
$(OBJ)/test_cmdline: testy/host/test_cmdline.c src/wspolne/k1_cmdline.c src/wspolne/k1_cmdline.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_cmdline.c src/wspolne/k1_cmdline.c

$(OBJ)/test_zdarzenie: testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c src/wspolne/k1_zdarzenie.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_zdarzenie.c src/wspolne/k1_zdarzenie.c

# 0.10.0 step 4: a touchpad in its own mode (src/wejscie/touchpad.c — pure)
$(OBJ)/test_dysk_strefy: testy/host/test_dysk_strefy.c src/strefa/dysk.c src/strefa/dysk.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_dysk_strefy.c src/strefa/dysk.c
# 0.10.1 step 2 (ADR 0044, D2): the upper limit of a zone's memory
$(OBJ)/test_pamiec_stref: testy/host/test_pamiec_stref.c src/nadzorca/pamiec_stref.c src/nadzorca/pamiec_stref.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_pamiec_stref.c src/nadzorca/pamiec_stref.c
# 0.10.1 step 1 (S3): why a zone ended — the VMM's status and its sandbox's memory events
$(OBJ)/test_powod_strefy: testy/host/test_powod_strefy.c src/nadzorca/strefy_powod.c src/nadzorca/strefy_powod.h $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_powod_strefy.c src/nadzorca/strefy_powod.c
$(OBJ)/test_sesje: testy/host/test_sesje.c src/nadzorca/sesje.c src/nadzorca/sesje.h $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_sesje.c src/nadzorca/sesje.c $(KOMUNIKAT) $(DOK_C) src/wspolne/k1_uslugi.c
$(OBJ)/test_touchpad: testy/host/test_touchpad.c src/wejscie/touchpad.c src/wejscie/touchpad.h src/wspolne/k1_zdarzenie.c \
	src/wspolne/k1_zdarzenie.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/wejscie -o $@ testy/host/test_touchpad.c src/wejscie/touchpad.c src/wspolne/k1_zdarzenie.c

$(OBJ)/test_komunikat: testy/host/test_komunikat.c testy/host/test_komunikat_pliki.c testy/host/test_komunikat_strefy.c testy/host/test_komunikat.h $(KOMUNIKAT) src/wspolne/k1_kanal.c src/wspolne/k1_komunikat.h src/wspolne/k1_kanal.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_komunikat.c testy/host/test_komunikat_pliki.c testy/host/test_komunikat_strefy.c $(KOMUNIKAT) $(DOK_C) src/wspolne/k1_kanal.c

$(OBJ)/test_log: testy/host/test_log.c src/wspolne/k1_log.c src/wspolne/k1_log.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_log.c src/wspolne/k1_log.c

$(OBJ)/test_bateria: testy/host/test_bateria.c src/nadzorca/bateria.c src/nadzorca/bateria.h $(KOMUNIKAT) src/wspolne/k1_komunikat.h \
		src/wspolne/k1_io.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_bateria.c src/nadzorca/bateria.c $(KOMUNIKAT) $(DOK_C)

$(OBJ)/podglad_grafiki: testy/host/podglad_grafiki.c $(GRAFIKA) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/podglad_grafiki.c src/grafika/k1_grafika.c -lm

$(OBJ)/test_przybornik: testy/host/test_przybornik.c $(HOST_PRZYB) $(NAGL_P) $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_przybornik.c $(HOST_PRZYB) -lm

# 0.8.2 step 5: the menu under the right mouse button — its rows for every type and place, layout, drawing
$(OBJ)/test_menu: testy/host/test_menu.c $(HOST_PRZYB) $(NAGL_P) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_menu.c $(HOST_PRZYB) -lm

# 0.8.2 step 6: dragging — the table of rules (every source and target), the windows of a question, the selection of many
$(OBJ)/test_przeciaganie: testy/host/test_przeciaganie.c $(HOST_PRZYB) $(NAGL_P) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_przeciaganie.c $(HOST_PRZYB) -lm

# 0.8.0 (ADR 0030): a K1-DANE partition with notes in the format of 0.5.8–0.7.6 (scripts/test-pliki.sh)
$(OBJ)/notatki_w_plikach: testy/host/notatki_w_plikach.c $(DOK_TEST) $(wildcard src/skarbiec/*.h) $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -DK1_CZAS_BUDOWY=$(CZAS_TESTU) -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ \
		testy/host/notatki_w_plikach.c $(DOK_TEST)


# 0.8.2 (ADR 0036): format 2 of the vault — segments, the spis, compaction one segment at a time
# the measurement of the plan's step 1 (not in testy-host: it writes gigabytes): optimised, without ASan
$(OBJ)/pomiar_magazynu: testy/host/pomiar_magazynu.c $(MAGAZYN) src/skarbiec/magazyn.h | $(OBJ)
	gcc -std=c11 -O2 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc/wspolne -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/pomiar_magazynu.c $(MAGAZYN)
$(OBJ)/test_odcinki: testy/host/test_odcinki.c testy/host/k1_ram_dysk.h $(MAGAZYN) src/skarbiec/magazyn.h src/skarbiec/magazyn_wew.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_odcinki.c $(MAGAZYN)
$(OBJ)/test_odcinki_pelny: testy/host/test_odcinki_pelny.c testy/host/k1_ram_dysk.h $(MAGAZYN) src/skarbiec/magazyn.h src/skarbiec/magazyn_wew.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_odcinki_pelny.c $(MAGAZYN)

# 0.8.0 step 4: dekoder on pictures made by Pillow (testy/host/obrazy_dekoder.py).
$(OBJ)/test_dekoder: testy/host/test_dekoder.c $(DEKODER_OBRAZ) src/dekoder/obraz.h $(WUFFS) testy/host/k1_test_host.h $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/dekoder -Isrc/zewnetrzne/wuffs -o $@ testy/host/test_dekoder.c \
		$(DEKODER_OBRAZ) $(DOK_C) src/wspolne/k1_uslugi.c -lm

# 0.8.0 step 3: czytnik's FAT reader on images made by mtools/sfdisk (testy/host/obrazy_fat.sh), and the list format
$(OBJ)/test_fat: testy/host/test_fat.c $(CZYTNIK_FAT) src/czytnik/fat.h src/wspolne/k1_pendrive.c $(DOK_C) $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/czytnik -o $@ testy/host/test_fat.c $(CZYTNIK_FAT) src/wspolne/k1_pendrive.c \
		$(DOK_C) src/wspolne/k1_uslugi.c

$(OBJ)/test_instalator: testy/host/test_instalator.c src/instalator/gpt.c src/instalator/gpt.h src/nadzorca/dyski.c src/nadzorca/dyski.h \
		src/wspolne/k1_io.h src/wspolne/k1_bajty.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/instalator -Isrc/nadzorca -o $@ testy/host/test_instalator.c src/instalator/gpt.c src/nadzorca/dyski.c

# 0.9.0 step 8: the clock from the internet — SNTP packets, a round of several sources, the messages of the clock
$(OBJ)/test_czas: testy/host/test_czas.c $(SIEC) $(wildcard src/siec/*.h) $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/siec -o $@ testy/host/test_czas.c src/siec/sntp.c src/siec/czas.c $(KOMUNIKAT) $(DOK_C)

# 0.9.0 step 6 (ADR 0039): addresses, the messages of siec, DHCP packets and the client
# 0.9.1 step 1: the Wi-Fi networks from the air (src/siec/wifi.c) and the shared names and messages (k1_wifi.c)
$(OBJ)/test_krypto: testy/host/test_krypto.c src/wspolne/k1_sha1.c src/wspolne/k1_aes.c $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_krypto.c src/wspolne/k1_sha1.c src/wspolne/k1_aes.c
$(OBJ)/test_wpa: testy/host/test_wpa.c src/siec/wpa.c src/siec/eapol.c src/siec/wpa.h src/siec/eapol.h src/wspolne/k1_sha1.c src/wspolne/k1_aes.c src/wspolne/k1_wifi.c $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/siec -o $@ testy/host/test_wpa.c src/siec/wpa.c src/siec/eapol.c src/wspolne/k1_sha1.c src/wspolne/k1_aes.c src/wspolne/k1_wifi.c
$(OBJ)/test_wifi_polacz: testy/host/test_wifi_polacz.c src/siec/wifi.c src/siec/pamiec.c src/siec/wifi.h src/siec/pamiec.h $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/siec -o $@ testy/host/test_wifi_polacz.c src/siec/wifi.c src/siec/pamiec.c $(KOMUNIKAT) $(DOK_C)
# 0.10.0 step 1 (ADR 0042): the zones' virtqueues and virtio-mmio, the start of the zone kernel (fuzzing: fuzz_virtq)
$(OBJ)/test_virtio: testy/host/test_virtio.c src/strefa/virtq.c src/strefa/mmio.c src/strefa/linia.c $(wildcard src/strefa/*.h) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_virtio.c src/strefa/virtq.c src/strefa/mmio.c src/strefa/linia.c
# 0.10.0 step 2 (ADR 0043 T9): the zones' connector — SOCKS5, DNS, the D3 rule, the messages (fuzzing: fuzz_lacznik)
$(OBJ)/test_lacznik: testy/host/test_lacznik.c src/strefa/lacznik.c src/strefa/lacznik.h $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_lacznik.c src/strefa/lacznik.c $(KOMUNIKAT) $(DOK_C)
# 0.10.1 step 2 (ADR 0044): the balloon's lists, blocks and statistics (fuzzing: fuzz_balon)
$(OBJ)/test_palce: testy/host/test_palce.c src/strefa/kompozytor/palce.c src/strefa/kompozytor/palce.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa/kompozytor -o $@ testy/host/test_palce.c src/strefa/kompozytor/palce.c
$(OBJ)/test_tlo: testy/host/test_tlo.c src/strefa/tlo.c src/strefa/tlo.h src/strefa/virtq.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_tlo.c src/strefa/tlo.c
$(OBJ)/test_balon: testy/host/test_balon.c src/strefa/strony.c src/strefa/strony.h src/strefa/virtq.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_balon.c src/strefa/strony.c
$(OBJ)/test_strefa_start: testy/host/test_strefa_start.c src/strefa/start.c $(wildcard src/strefa/*.h) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/strefa -o $@ testy/host/test_strefa_start.c src/strefa/start.c

$(OBJ)/test_wifi: testy/host/test_wifi.c src/siec/wifi.c src/siec/wifi.h $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/siec -o $@ testy/host/test_wifi.c src/siec/wifi.c $(KOMUNIKAT) $(DOK_C)

$(OBJ)/test_siec: testy/host/test_siec.c $(SIEC) $(wildcard src/siec/*.h) $(KOMUNIKAT) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/siec -o $@ testy/host/test_siec.c src/siec/dhcp.c src/siec/klient.c src/siec/sntp.c src/siec/czas.c src/siec/wifi.c \
		$(KOMUNIKAT) $(DOK_C)

$(OBJ)/test_uevent: testy/host/test_uevent.c src/nadzorca/uevent.c src/nadzorca/uevent.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/nadzorca -o $@ testy/host/test_uevent.c src/nadzorca/uevent.c

$(OBJ)/test_odbior: testy/host/test_odbior.c src/wspolne/k1_odbior.c src/wspolne/k1_odbior.h src/wspolne/k1_kanal.c $(KOMUNIKAT) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_odbior.c src/wspolne/k1_odbior.c src/wspolne/k1_kanal.c $(KOMUNIKAT) $(DOK_C)

$(OBJ)/test_bat_arch: testy/host/test_bat_arch.c src/wspolne/k1_bat_arch.c src/wspolne/k1_bat_arch.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_bat_arch.c src/wspolne/k1_bat_arch.c

$(OBJ)/test_manifest: testy/host/test_manifest.c $(MANIFEST) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_manifest.c src/wspolne/k1_manifest.c $(MONOCYPHER)

$(OBJ)/test_uklad: testy/host/test_uklad.c src/wspolne/k1_uklad.c src/wspolne/k1_uklad.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_uklad.c src/wspolne/k1_uklad.c $(MONOCYPHER)

# 0.9.0 step 3 (ADR 0038): the boot files as a tree, swapped beside the old ones (images: testy/host/obrazy_efi.sh)
$(OBJ)/test_start: testy/host/test_start.c src/instalator/start.c src/instalator/start.h $(FAT32) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/instalator -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_start.c src/instalator/start.c \
		$(FAT32) src/wspolne/k1_log.c $(MONOCYPHER)

$(OBJ)/test_efi: testy/host/test_efi.c $(EFI) $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/zewnetrzne/monocypher -o $@ testy/host/test_efi.c $(EFI)

$(OBJ)/test_sha256: testy/host/test_sha256.c src/wspolne/k1_sha256.c src/wspolne/k1_sha256.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_sha256.c src/wspolne/k1_sha256.c

$(OBJ)/test_kalkulator: testy/host/test_kalkulator.c src/aplikacje/kalkulator/liczydlo.c src/aplikacje/kalkulator/liczydlo.h \
                       src/przybornik/k1_liczba.c src/przybornik/k1_liczba.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/aplikacje/kalkulator -o $@ testy/host/test_kalkulator.c \
		src/aplikacje/kalkulator/liczydlo.c src/przybornik/k1_liczba.c -lm

# 0.8.2 step 4: Podgląd — the order of the pictures (pure functions)
$(OBJ)/test_podglad: testy/host/test_podglad.c src/aplikacje/podglad/kolejnosc.c src/aplikacje/podglad/kolejnosc.h $(DOK_C) \
                     src/aplikacje/podglad/skala.c src/aplikacje/podglad/skala.h \
                     $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/aplikacje/podglad -o $@ testy/host/test_podglad.c src/aplikacje/podglad/kolejnosc.c \
		src/aplikacje/podglad/skala.c $(DOK_C)

# 0.8.5 step 2: the measurement of Podgląd's view (not in testy-host: a measurement, optimised like the image, no ASan)
$(OBJ)/pomiar_podgladu: testy/host/pomiar_podgladu.c src/aplikacje/podglad/skala.c src/aplikacje/podglad/skala.h \
                        src/wspolne/k1_skaluj.c src/przybornik/k1_rysuj.c $(GRAFIKA) $(GEN)/k1_czcionki_dane.c | $(OBJ)
	gcc -std=gnu11 -O2 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc/wspolne -Isrc/grafika -Isrc/przybornik -Isrc/aplikacje/podglad \
		'-DK1_WERSJA="$(WERSJA)"' -o $@ testy/host/pomiar_podgladu.c src/aplikacje/podglad/skala.c src/wspolne/k1_skaluj.c \
		src/grafika/k1_grafika.c src/przybornik/k1_rysuj.c $(GEN)/k1_czcionki_dane.c -lm

# 0.8.5 step 1: ekran's queue of what a client has not taken yet (pure functions)
$(OBJ)/test_zalegle: testy/host/test_zalegle.c src/ekran/zalegle.c src/ekran/zalegle.h $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/ekran -o $@ testy/host/test_zalegle.c src/ekran/zalegle.c $(KOMUNIKAT) $(DOK_C)

# 0.9.0 step 2: skarbiec's queue of replies a client has not taken yet (on a real channel)
$(OBJ)/test_skarb_zalegle: testy/host/test_skarb_zalegle.c src/skarbiec/zalegle.c src/skarbiec/zalegle.h $(NAGL) testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/skarbiec -o $@ testy/host/test_skarb_zalegle.c src/skarbiec/zalegle.c src/wspolne/k1_kanal.c $(KOMUNIKAT) $(DOK_C)

# 0.7.5: the desktop arrangement of gui (pure rules and the saved bytes)
$(OBJ)/test_rozmieszczenie: testy/host/test_rozmieszczenie.c src/gui/rozmieszczenie.c src/gui/rozmieszczenie.h $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Isrc/gui -o $@ testy/host/test_rozmieszczenie.c src/gui/rozmieszczenie.c

# 0.8.0: the user's files — names, types, the entry layout, folders, Kosz, the rights of the channel; a fixed build time
# (2026-11-18) so that the tests' clock (2027) stays after it whenever they run
CZAS_TESTU := 1795000000
DOK_TEST := src/skarbiec/dokumenty.c $(wildcard src/skarbiec/dok_*.c) src/skarbiec/dostep.c src/skarbiec/obiekty.c src/skarbiec/przerwy.c src/skarbiec/miejsce.c src/skarbiec/strefy.c $(MAGAZYN) $(DOK_C) \
            src/wspolne/k1_log.c $(KOMUNIKAT) src/wspolne/k1_kanal.c src/wspolne/k1_memfd.c src/wspolne/k1_uslugi.c
$(OBJ)/test_dokumenty: $(wildcard testy/host/test_dokumenty*.c) testy/host/test_dokumenty.h $(DOK_TEST) $(wildcard src/skarbiec/*.h) $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -DK1_CZAS_BUDOWY=$(CZAS_TESTU) -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ \
		$(wildcard testy/host/test_dokumenty*.c) $(DOK_TEST)

# 0.8.1: Notatki's own code (pliki.c, spis.c) against skarbiec's over a socketpair
NOTATKI_TEST := src/aplikacje/notatki/pliki.c src/aplikacje/notatki/spis.c $(DOK_TEST)
$(OBJ)/test_notatki: testy/host/test_notatki.c $(NOTATKI_TEST) $(wildcard src/skarbiec/*.h src/aplikacje/notatki/*.h) $(NAGL) | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -DK1_CZAS_BUDOWY=$(CZAS_TESTU) -Isrc/skarbiec -Isrc/aplikacje/notatki \
		-Isrc/zewnetrzne/monocypher -o $@ testy/host/test_notatki.c $(NOTATKI_TEST)

# 0.8.1 (ADR 0034): a vault with a long log, or almost full (scripts/test-skarbiec.sh)
$(OBJ)/magazyn_testowy: testy/host/magazyn_testowy.c $(MAGAZYN) src/skarbiec/magazyn.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -Wno-conversion -Isrc/skarbiec -Isrc/zewnetrzne/monocypher -o $@ testy/host/magazyn_testowy.c $(MAGAZYN)

# 0.10.2 step 2 (ADR 0045): the screen's description for the tests
$(OBJ)/test_opis: testy/host/test_opis.c $(KOMUNIKAT) $(DOK_C) src/wspolne/k1_opis.h testy/host/k1_test_host.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_opis.c $(KOMUNIKAT) $(DOK_C)

# 0.8.1 (ADR 0034): the index by key
$(OBJ)/test_indeks: testy/host/test_indeks.c src/wspolne/k1_indeks.c src/wspolne/k1_indeks.h | $(OBJ)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ testy/host/test_indeks.c src/wspolne/k1_indeks.c

# Every host test includes the shared helpers: a change there rebuilds them all.
$(addprefix $(OBJ)/,test_bat_arch test_bateria test_cmdline test_efi test_instalator test_kalkulator test_komunikat \
	test_dokumenty test_notatki test_indeks test_log test_odcinki test_odcinki_pelny test_manifest test_odbior test_przybornik test_rozmieszczenie test_sha256 test_uevent test_uklad \
	test_zdarzenie): testy/host/k1_test_host.h

# Host tests (0.10.0): one target per test, so `make -j… -O testy-host` runs them side by side (one after another they
# took most of test-m1's time); `-O` keeps the output of each one together. The slow ones come first, so they start
# first. A plain test (no arguments, no files): its name in TESTY_HOST. One that needs files: its own rule below,
# named host-<name>, and the name in TESTY_HOST_PLIKI.
TESTY_HOST := test_odcinki test_odcinki_pelny test_dokumenty test_indeks test_wpa test_krypto test_virtio \
	test_strefa_start test_wifi_polacz test_wifi test_siec test_czas test_zalegle test_skarb_zalegle test_notatki \
	test_rozmieszczenie test_kalkulator test_podglad test_manifest test_efi test_sha256 test_bat_arch test_odbior \
	test_uevent test_cmdline test_log test_bateria test_zdarzenie test_komunikat test_przybornik test_menu \
	test_przeciaganie test_lacznik test_touchpad test_dysk_strefy test_sesje test_powod_strefy test_pamiec_stref test_balon test_tlo test_palce \
	test_opis
TESTY_HOST_PLIKI := test_dekoder test_fat test_start test_uklad test_instalator podglad_grafiki

# notatki_w_plikach and magazyn_testowy are not run here: scripts/test-*.sh use them
testy-host: $(TESTY_HOST:%=host-%) $(TESTY_HOST_PLIKI:%=host-%) $(OBJ)/notatki_w_plikach $(OBJ)/magazyn_testowy

$(TESTY_HOST:%=host-%): host-%: $(OBJ)/%
	$(OBJ)/$*

host-test_uklad: $(OBJ)/test_uklad
	rm -f $(OBJ)/grubenv.ref && grub-editenv $(OBJ)/grubenv.ref create && \
		grub-editenv $(OBJ)/grubenv.ref set k1_slot=a k1_start=a k1_nowy= k1_proba=
	$(OBJ)/test_uklad $(OBJ)/grubenv.ref

host-test_instalator: $(OBJ)/test_instalator
	$(OBJ)/test_instalator $(OBJ)/gpt.img
	sfdisk -d $(OBJ)/gpt.img | grep -q 'start=        2048, size=     1048576, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B.*name="K1-EFI"'
	sfdisk -d $(OBJ)/gpt.img | grep -q 'start=     1050624, size=     7335936, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4.*name="K1-DANE"'
	sfdisk --verify $(OBJ)/gpt.img >/dev/null
	sfdisk -d $(OBJ)/gpt.img-wiekszy | grep -q 'start=     1050624, size=    32501760, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4.*name="K1-DANE"'
	sfdisk --verify $(OBJ)/gpt.img-wiekszy >/dev/null
	rm -f $(OBJ)/gpt.img $(OBJ)/gpt.img-wiekszy
	@echo "test_instalator: tablica GPT odczytana przez sfdisk (K1-EFI, K1-DANE; K1-DANE powiększona do 16 GiB)"

host-test_fat: $(OBJ)/test_fat
	sh testy/host/obrazy_fat.sh $(OBJ)/fat && $(OBJ)/test_fat $(OBJ)/fat && rm -rf $(OBJ)/fat

host-test_start: $(OBJ)/test_start
	sh testy/host/obrazy_efi.sh $(OBJ)/efi && $(OBJ)/test_start $(OBJ)/efi && sh testy/host/obrazy_efi.sh $(OBJ)/efi sprawdz && \
		rm -rf $(OBJ)/efi

host-test_dekoder: $(OBJ)/test_dekoder
	rm -rf $(OBJ)/obrazy && mkdir -p $(OBJ)/obrazy && python3 -I testy/host/obrazy_dekoder.py $(OBJ)/obrazy && \
		$(OBJ)/test_dekoder $(OBJ)/obrazy && rm -rf $(OBJ)/obrazy

host-podglad_grafiki: $(OBJ)/podglad_grafiki
	$(OBJ)/podglad_grafiki 1280 800 900 400 0 > $(OBJ)/podglad.ppm
	$(OBJ)/podglad_grafiki 13 7 -50 3 1 > /dev/null
	$(OBJ)/podglad_grafiki 1 1 0 0 0 > /dev/null
	@echo "podglad_grafiki: OK (ASan/UBSan, rozmiary 1280x800, 13x7, 1x1)"

# Fuzzing (libFuzzer): one program per parser of untrusted bytes, testy/host/fuzz_<name>.c with the sources
# in FUZZ_ZR_<name>; its corpus in build/fuzz-korpus-<name> (plus testy/host/korpus-<name> when there is one),
# inputs up to FUZZ_DL_<name> bytes. A new one: its .c file and these two lines.
FUZZ := cmdline zdarzenie komunikat magazyn uevent bat_arch manifest uklad rozmieszczenie dokument fat pendrive dekoder fat32 dhcp sntp wifi eapol virtq \
        lacznik balon opis
FUZZ_ZR_cmdline   := src/wspolne/k1_cmdline.c
FUZZ_DL_cmdline   := 8192
FUZZ_ZR_zdarzenie := src/wspolne/k1_zdarzenie.c
FUZZ_DL_zdarzenie := 64
FUZZ_ZR_komunikat := $(KOMUNIKAT) $(DOK_C)
FUZZ_ZR_opis      := $(KOMUNIKAT) $(DOK_C)
FUZZ_DL_opis      := 4200
FUZZ_DL_komunikat := 64
FUZZ_ZR_magazyn   := $(MAGAZYN)
FUZZ_DL_magazyn   := 16384
FUZZ_ZR_uevent    := src/nadzorca/uevent.c
FUZZ_DL_uevent    := 8192
FUZZ_ZR_bat_arch  := src/wspolne/k1_bat_arch.c
FUZZ_DL_bat_arch  := 9000
FUZZ_ZR_manifest  := src/wspolne/k1_manifest.c $(MONOCYPHER)
FUZZ_DL_manifest  := 512
FUZZ_ZR_uklad     := src/wspolne/k1_uklad.c $(MONOCYPHER)
FUZZ_DL_uklad     := 1024
FUZZ_ZR_rozmieszczenie := src/gui/rozmieszczenie.c
FUZZ_DL_rozmieszczenie := 700
FUZZ_ZR_dokument  := $(DOK_C) src/wspolne/k1_uslugi.c
FUZZ_DL_dokument  := 1200
FUZZ_ZR_fat       := $(CZYTNIK_FAT) $(DOK_C) src/wspolne/k1_uslugi.c
FUZZ_DL_fat       := 65536
FUZZ_ZR_pendrive  := src/wspolne/k1_pendrive.c $(DOK_C) src/wspolne/k1_uslugi.c
FUZZ_DL_pendrive  := 1000
FUZZ_ZR_dekoder   := $(DEKODER_OBRAZ) $(DOK_C) src/wspolne/k1_uslugi.c
FUZZ_DL_dekoder   := 20000
FUZZ_ZR_fat32     := $(FAT32) src/instalator/start.c src/wspolne/k1_log.c $(MONOCYPHER)
FUZZ_DL_fat32     := 65536
FUZZ_ZR_dhcp      := src/siec/dhcp.c src/siec/klient.c src/wspolne/k1_siec.c
FUZZ_DL_dhcp      := 4096
FUZZ_ZR_sntp      := src/siec/sntp.c src/siec/czas.c src/wspolne/k1_siec.c
FUZZ_DL_sntp      := 4096
FUZZ_ZR_wifi      := src/siec/wifi.c src/wspolne/k1_wifi.c
FUZZ_DL_wifi      := 8192
FUZZ_ZR_eapol     := src/siec/eapol.c src/siec/wpa.c src/wspolne/k1_sha1.c src/wspolne/k1_aes.c src/wspolne/k1_wifi.c
FUZZ_DL_eapol     := 700
FUZZ_ZR_virtq     := src/strefa/virtq.c src/strefa/mmio.c
FUZZ_DL_virtq     := 40000
FUZZ_ZR_lacznik   := src/strefa/lacznik.c src/wspolne/k1_siec.c
FUZZ_DL_lacznik   := 700
FUZZ_ZR_balon     := src/strefa/strony.c
FUZZ_DL_balon     := 1100
FUZZ_FLAGI := -std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
              -Isrc/wspolne -Isrc/grafika -Isrc/przybornik -Isrc/nadzorca -Isrc/skarbiec -Isrc/gui -Isrc/czytnik -Isrc/dekoder -Isrc/instalator -Isrc/siec -Isrc/strefa -Isrc/zewnetrzne/monocypher \
              -Isrc/zewnetrzne/wuffs

$(OBJ)/fuzz_%: testy/host/fuzz_%.c $$(FUZZ_ZR_$$*) $(NAGL) $(wildcard src/nadzorca/*.h src/skarbiec/*.h src/gui/*.h src/czytnik/*.h src/dekoder/*.h src/siec/*.h src/strefa/*.h) | $(OBJ)
	$(CC_FUZZ) $(FUZZ_FLAGI) -o $@ $< $(FUZZ_ZR_$*)

# One target per fuzzer (0.8.6): `make -j… fuzz` runs them side by side — one after another they took most of the
# time of the host tests (13 × FUZZ_CZAS). `make -O` keeps the output of each one together.
fuzz: $(FUZZ:%=fuzz-%)
$(FUZZ:%=fuzz-%): fuzz-%: $(OBJ)/fuzz_%
	mkdir -p $(BUDOWA)/fuzz-korpus-$* $(BUDOWA)/fuzz-wyniki
	$(OBJ)/fuzz_$* -max_total_time=$(FUZZ_CZAS) -artifact_prefix=$(BUDOWA)/fuzz-wyniki/$*- \
		-max_len=$(FUZZ_DL_$*) $(BUDOWA)/fuzz-korpus-$* $(wildcard testy/host/korpus-$*)

clean:
	rm -rf $(BIN) $(OBJ)

.PHONY: all testy-host $(TESTY_HOST:%=host-%) $(TESTY_HOST_PLIKI:%=host-%) fuzz $(FUZZ:%=fuzz-%) clean FORCE
