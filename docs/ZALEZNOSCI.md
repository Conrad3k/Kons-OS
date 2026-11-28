# Zależności K1

Zasada (SPEC §2.7): każda zewnętrzna biblioteka i narzędzie wymaga uzasadnienia. Rozróżniamy to,
co trafia **do obrazu** (działa na komputerze użytkownika), od tego, co służy tylko **do budowy i testów**.

## W obrazie (baza zaufania)

| Zależność | Wersja | Licencja | Źródło i weryfikacja | Uzasadnienie |
|---|---|---|---|---|
| Jądro Linux | 7.3.5 (linia stabilna; od 0.10.5 — wcześniej 6.18 LTS) | GPL-2.0 | cdn.kernel.org; SHA-256 przypięta w `jadro/wersja.env`, podpis PGP wydania sprawdzany przy każdym pobraniu | fundament systemu (SPEC §4) |
| musl libc (statycznie w każdym programie) | 1.2.5 + łatki CVE-2025-26519 (`iconv`, 2 commity musl) i `-static-pie` w `musl-gcc` (Debian) — od 0.9.0 nasz build | MIT | oryginalne wydanie `musl-1.2.5.tar.gz` z archiwum Ubuntu (pakiet źródłowy) albo musl.libc.org; SHA-256 przypięta i podpis PGP wydawcy sprawdzany przy każdym pobraniu (`musl/wersja.env`, `musl/klucze/`); łatki w `musl/latki/`; budowa `scripts/zbuduj-musl.sh` (`musl/README.md`) | mała, czytelna libc ze wsparciem statycznego linkowania (SPEC §4); ADR 0002; własny build — przypięta wersja i poprawki bezpieczeństwa (0.9.0 krok 4, RYZYKA R8). Poprawki po 1.2.5 wg Debiana/Ubuntu (do 1.2.5-3build1): tylko CVE-2025-26519 |
| fortify-headers (nagłówki, wbudowane w kod każdego programu przy kompilacji) | 3.0.1 (tag `3.0.1`, commit `eec2b5e0`, 2025-12-30) | 0BSD (ISC bez warunku) | github.com/jvoisin/fortify-headers (kontynuacja git.2f30.org/fortify-headers, jak w Alpine); pliki `include/` i `LICENSE` w `src/zewnetrzne/fortify-headers/` bez zmian, SHA-256 w `SHA256SUMS`; pobrane przez proxy.golang.org — suma modułu `h1:mGe2tFFxzSCPFdVtQboD+FSGJZWMU3/alrUO5RMgFZ4=` potwierdzona w publicznym rejestrze sum.golang.org (`go mod download`) | `_FORTIFY_SOURCE=3` z musl (0.9.0 krok 4, korekta ADR 0007, RYZYKA R7): sprawdzenie granic w `memcpy`, `read`, `snprintf`… w czasie działania; przepełnienie = pułapka (`ud2`, SIGILL), bez biblioteki i bez `__*_chk`; mały, czytelny kod (ok. 10 nagłówków) używany w Alpine i Chimera Linux; aktualizacja: nowy tag, porównanie zmian, nowe sumy, pełne testy |
| IBM Plex Sans / Serif — **tylko bitmapy glifów** wbudowane w `gui` i aplikacje | 6.1.1 (`fonts-ibm-plex`) | SIL OFL 1.1 | archiwum Ubuntu | tekst z polskimi znakami bez parsera czcionek w systemie (ADR 0009) |
| libva + intel-vaapi-driver (i965) + libdrm (rdzeń i `libdrm_intel`) — statycznie w usłudze `filmy` (0.10.9 krok 2, ADR 0049 pkt 3) | libva 2.24.1, intel-vaapi-driver 2.4.1+dfsg1 (z łatkami Debiana 0001–0004), libdrm 2.4.134 | MIT (wszystkie trzy) | pakiety źródłowe Debiana 13 z deb.debian.org; SHA-256 jak w podpisanych `.dsc` Debiana, w `src/zewnetrzne/va/zrodla.txt`; budowa `scripts/zbuduj-va.sh` (nasz musl, nasze flagi utwardzenia bez `-Werror`; ostrzeżenia liczone: libdrm 12, libdrm_intel 108, libva 254, i965 1177) | sprzętowe dekodowanie filmów kartą Intela na obu laptopach właściciela (HD 4000, HD 520): i965 to jedyny sterownik VA-API w C dla Gen7 i Gen9 (iHD — C++, tylko Gen8+, T2); libva bez łatek, `dlopen` zastępuje `src/filmy/obce.c` (sterownik wkompilowany); funkcje plikowe bibliotek kończą się błędem bez wywołania jądra (`--wrap`, `src/filmy/obce.txt`); libpciaccess zastąpiona zaślepką (używa jej tylko funkcja, której i965 nie woła). Ryzyka: R98, R99. Aktualizacja: nowy wiersz tabeli z sumą z `.dsc`, `test_filmy`, sprawdzenie na sprzęcie |
| Wuffs (statycznie w usłudze `dekoder`, tylko moduły PNG, JPEG, GIF, BMP, WebP z VP8 — od 0.8.2 — oraz zlib/deflate, LZW, sumy kontrolne) | 0.4.0-alpha.12 (tag `v0.4.0-alpha.12`, commit `7f70dda9`, 2026-10-20) | Apache-2.0 albo MIT (do wyboru) | github.com/google/wuffs; `release/c/wuffs-v0.4.c` w `src/zewnetrzne/wuffs/` bez zmian, SHA-256 w `src/zewnetrzne/wuffs/SHA256SUMS` | dekoder obrazów PNG i JPEG (0.8.0, ADR 0032), od 0.8.2 także GIF, BMP i WebP (te same gwarancje, moduły z tego samego pliku): napisany w języku, który przy kompilacji dowodzi braku wyjścia poza tablice i przepełnień liczb, sprawdzany fuzzingiem przez Google (OSS-Fuzz); sam nic nie przydziela poza dekoderem; kompiluje się z naszymi flagami bez ostrzeżeń. Własny dekoder JPEG (progresywny, podpróbkowanie) to kilka tysięcy wierszy nowego, niesprawdzonego kodu. Wersja „alpha” to jedyna linia z JPEG-iem; aktualizacja: nowy tag, porównanie zmian, nowe sumy, `test_dekoder` i `fuzz_dekoder`. HEIC (iPhone) — nie: Wuffs go nie ma, a obcego dekodera HEVC w C do świata natywnego nie bierzemy |
| Pliki od producentów kart Wi-Fi i — od 0.10.9 krok 2 — plik DMC karty graficznej Skylake (firmware) — **wbudowane w jądro** (`EXTRA_FIRMWARE`), działają w karcie, nie w K1 | linux-firmware 20261019; 33 pliki z `jadro/oprogramowanie-kart.txt` (27 MB) | różne licencje producentów (Intel, Qualcomm Atheros, Realtek, MediaTek — redystrybucja dozwolona, kod zamknięty; plik `WHENCE` w linux-firmware) | git.kernel.org (repozytorium linux-firmware, tag z `jadro/wersja.env`), pobierane pojedynczo przez `scripts/oprogramowanie-kart.sh`; SHA-256 każdego pliku przypięta w tabeli, zła suma przerywa budowę | bez nich karty Wi-Fi nie działają (decyzja właściciela D3 planu 0.9.1, ADR 0041); tylko wybrane rodziny kart (T2); kod zamknięty — RYZYKA R71 |
| Baza przepisów radiowych (`regulatory.db` + podpis `.p7s`) — wbudowana w jądro | wireless-regdb 2026.09.03 | ISC | cdn.kernel.org (archiwum, SHA-256 w `jadro/wersja.env`, zgodna z `sha256sums.asc` wydania); jądro sprawdza podpis bazy kluczem ze swoich źródeł | jakie kanały i moce wolno używać w danym kraju; bez niej jądro stosuje ostrożne ustawienia „cały świat” (ADR 0041) |
| Monocypher (statycznie w usłudze `skarbiec`) | 4.0.3 (tag `4.0.3`, commit `ab2b16dd`) | BSD-2-Clause albo CC0 (do wyboru) | github.com/LoupVaillant/monocypher; pliki w `src/zewnetrzne/monocypher/` bez zmian, SHA-256 (pełne sumy: `src/zewnetrzne/monocypher/SHA256SUMS`) | XChaCha20-Poly1305, BLAKE2b, Argon2id dla Blob Vault — mała, audytowana biblioteka, jeden plik (ADR 0015); aktualizacja: nowy tag, porównanie zmian, nowe sumy |

Nic więcej (poza Monocypherem w `skarbiec`, Wuffs w `dekoder`, libva + i965 + libdrm w `filmy` i nagłówkami fortify-headers w każdym programie): brak bibliotek współdzielonych, interpreterów, powłoki. Grafika bez bibliotek:
API KMS jądra przez ioctl (nagłówki UAPI z naszego jądra), renderer i przybornik GUI własne
(`src/grafika/`, `src/przybornik/`).

### Pliki startowe (ISO, pendrive, zainstalowany K1)
| Zależność | Wersja | Licencja | Źródło | Uzasadnienie |
|---|---|---|---|---|
| shim (UEFI, podpis Microsoftu) | 15.8 (`shim-signed` 1.58+15.8-0ubuntu1) | BSD-2-Clause | archiwum Ubuntu — pakiet tylko rozpakowany (`scripts/wspolne.sh: efi_podpisane`), wersje w `build/pobrane/efi/wersje.txt` | pierwszy program startowy z pendrive'a/płyty przez UEFI; podpisane pliki z każdej płyty Ubuntu zamiast własnego, niepodpisanego GRUB-a (Defender oznaczał go heurystyką jako trojana, 0.5.5) |
| GRUB dla nośników wymiennych (UEFI, `gcdx64.efi`, podpis Canonical) | 2.12 (`grub-efi-amd64-signed` 1.202.5+2.12-1ubuntu7.3) | GPL-3.0 | jw. | ładuje `k1.efi` z miejsca A/B (`loopback`, `load_env`/`save_env`); z Secure Boot sprawdza jego podpis przez shim (klucz K1 w MOK, ADR 0022) |
| MokManager (`mmx64.efi`, podpis Canonical) | 15.8 (`shim-signed`) | BSD-2-Clause | jw. | wpisanie klucza K1 do MOK przy pierwszym włączeniu Secure Boot (0.7.0, ADR 0022) |
| GRUB BIOS (i386-pc, obraz El Torito z wbudowanymi modułami) | 2.12-1ubuntu7.3 | GPL-3.0 | archiwum Ubuntu (`grub-pc-bin`), `grub-mkimage` | start z płyty przez BIOS w maszynach wirtualnych; tymczasowo do 0.13 (ADR 0010, 0053, RYZYKA R25) |

## W obrazie strefy przeglądarki (0.10.0 krok 2, ADR 0043 — poza bazą zaufania K1: działa w mikro-maszynie)
| Zależność | Wersja | Licencja | Źródło i weryfikacja | Uzasadnienie |
|---|---|---|---|---|
| Firefox ESR (`firefox-esr`, polski interfejs `firefox-esr-l10n-pl`) i jego biblioteki z Debiana 13 (GTK 3, GLib, Cairo, Pango, fontconfig, czcionki DejaVu, glibc …) — 117 pakietów | wersje w `strefy/przegladarka.txt` (np. firefox-esr 153.7.0esr-1~deb13u1) | MPL 2.0 (Firefox), LGPL/GPL i inne (biblioteki — pliki `copyright` pakietów; przy wydaniu lista pakietów źródłowych) | deb.debian.org i security.debian.org (zapas: snapshot.debian.org); `InRelease` sprawdzany kluczami Debiana 13 z `strefy/klucze/` (`scripts/odswiez-strefe.py`), każdy pakiet — SHA-256 z tabeli (`scripts/zbuduj-strefe.sh`) | pełna przeglądarka w strefie (T8); WPE — później, jako silnik Przeglądarki K1 (ADR 0025). Nazwa i logo Firefoksa to znaki Mozilli: K1 pokazuje „Przeglądarka” z własną ikoną, program jest niezmieniony (S18) |
| Mesa (`libEGL.so.1`, `libGLESv2.so.2`, `libgbm.so.1`, `libgallium-26.2.6.so`, `gbm/dri_gbm.so`) — nasza budowa (0.10.9 krok 6, ADR 0049 pkt 7, plan S43) | 26.2.6 | MIT | źródła z deb.debian.org (`mesa_26.2.6.orig.tar.xz`, SHA-256 jak w podpisanym `.dsc` Debiana) w `src/zewnetrzne/mesa/zrodla.txt`; budowa `scripts/zbuduj-mese.sh`: tylko sterownik iris z `intel-virtio-experimental` (most native context do pośrednika grafiki K1), EGL na Waylandzie, GBM, OpenGL i GLES 2; bez LLVM, GLX, Vulkana, VA-API i narzędzi; okrojona | szybka grafika w sesji z przełącznikiem: Firefox rysuje kartą (WebRender, WebGL). Mesa Debiana nie ma `intel-virtio` (opcja eksperymentalna) i ciągnie LLVM (dziesiątki MB). Linkowana z bibliotekami Ubuntu 24.04 (komputer budowy), działa z bibliotekami Debiana 13 obrazu (glibc, libstdc++, libdrm2 — zgodne wstecz). Ryzyko: R101 (sterownik „eksperymentalny”) |
| FFmpeg (`libavcodec.so.61`, `libavutil.so.59`) — nasza budowa (0.13.2 krok 2, ADR 0049 pkt 8, ADR 0063 pkt 7) | 7.1.5 | LGPL-2.1+ (bez części GPL: `CONFIG_GPL 0` sprawdzane w skrypcie) | źródła z deb.debian.org (`ffmpeg_7.1.5.orig.tar.xz`, SHA-256 jak w `.dsc` Debiana `ffmpeg_7.1.5-0+deb13u1`) w `src/zewnetrzne/ffmpeg/zrodla.txt`; budowa `scripts/zbuduj-ffmpeg.sh`: tylko libavcodec i libavutil, dekoder i parser H.264, VA-API (`h264_vaapi`), `--disable-autodetect`, `--enable-small`, asembler x86 (nasm), bez programów; skrypt sprawdza, że biblioteki potrzebują tylko glibc i libva | filmy H.264 w przeglądarce (wbudowane ffvpx Firefoksa ma tylko VP8, VP9, AV1 — bez FFmpeg H.264 nie grał wcale); od kroku 4 — VA-API w sesji z filmami kartą. Pakiet Debiana ciągnie kilkadziesiąt bibliotek. Wersja z Debiana 13 (Firefox ESR 153 bierze libavcodec .62 … .53). Linkowana z bibliotekami Ubuntu 24.04 (nagłówki `libva-dev`), działa z glibc 2.41 i libva 2.22 obrazu. Robot jej nie aktualizuje — człowiek, jak Mesę. Ryzyko: R114 |
| libva (`libva2`, `libva-drm2`) z Debiana 13 | wersje w `strefy/przegladarka.txt` | MIT | jak pozostałe pakiety strefy (tabela, `odswiez-strefe.py`, robot) | FFmpeg z VA-API się bez nich nie wczyta; Firefox ładuje je sam w procesie RDD (`vaExportSurfaceHandle`, `vaGetDisplayDRM` — ADR 0063 L1); sterownika VA w obrazie nie ma do kroku 4 |
| Mapa klawiatury PL (XKB) — `strefy/klawiatura-pl.xkb` | xkeyboard-config z Debiana 13 (`xkb-data` w tabeli), skompilowana libxkbcommon 1.6 (Ubuntu 24.04) | MIT/X11 | wygenerowana raz z pakietu `xkb-data` obrazu (reguły evdev, pc105, pl); w repo | kompozytor strefy podaje ją przeglądarce (`wl_keyboard.keymap`) — bez libxkbcommon w kompozytorze |

## Do budowy (wpływają na wynik)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| sbsigntool (`sbsign`, `sbverify`) | 0.9.4-3.1ubuntu7 | Ubuntu 24.04 | podpis `k1.efi` do Secure Boot (0.7.0) |
| faketime | Ubuntu 24.04 | Ubuntu 24.04 | zamrożony zegar przy podpisie (czas podpisu = `SOURCE_DATE_EPOCH`, powtarzalność) |
| python3-virt-firmware (`virt-fw-vars`) | 24.1.1 | Ubuntu 24.04 | tylko testy: zmienne OVMF z kluczami MS i MOK (`test-m6`) |
| dpkg-deb, gpgv | Ubuntu 24.04 | Ubuntu 24.04 | obraz strefy przeglądarki: rozpakowanie pakietów Debiana (`scripts/zbuduj-strefe.sh`), sprawdzenie podpisów `InRelease` (`scripts/odswiez-strefe.py`) |
| python3-pil (Pillow) | 10.2.0 | Ubuntu 24.04 | tylko testy: zdjęcia JPEG i PNG na wirtualnym pendrivie (`test-pendrive`, 0.8.0 krok 3) i obrazy do `test_dekoder` / korpusu `fuzz_dekoder` (krok 4); `mtools`, `sfdisk` — obrazy FAT do `test_fat` i `test-pendrive` |
| `e2fsprogs` (`mke2fs`) | 1.47.0 | Ubuntu 24.04 | pusty dysk strefy `/rdzen/strefy/dysk.img` (0.10.0 krok 5, ADR 0042 T11): ext4 2 GiB robiony przy budowie — bez `mke2fs.conf` komputera (każda cecha podana), stały UUID i ziarno, czas z `SOURCE_DATE_EPOCH` (`E2FSPROGS_FAKE_TIME`) — powtarzalnie (`scripts/zbuduj-obraz.sh`) |
| `zstd` | Ubuntu 24.04 | Ubuntu 24.04 | kompresja jądra (`CONFIG_KERNEL_ZSTD`, od 0.9.1 — z plikami kart Wi-Fi gzip dawał 20 MB) |
| `dosfstools` (`fsck.fat`) | 4.2 | Ubuntu 24.04 | tylko testy (0.9.0 krok 3, ADR 0038): niezależne sprawdzenie FAT32 po wymianie plików startowych (`test_start`, `test-m5f`) |
| GCC (+ `gcc-13-plugin-dev`) | 13.3.0 | Ubuntu 24.04 | kompilacja jądra (z wtyczkami KSTACK_ERASE, LATENT_ENTROPY) i programów K1 |
| `musl-gcc` | z naszego musl (wyżej) | `build/musl/bin` | wrapper kompilatora dla musl (od 0.9.0 nie z `musl-tools` Ubuntu); `gnupg` — sprawdzenie podpisu wydania musl |
| binutils, make, flex, bison, bc, libelf, libssl, rsync, cpio, xz | Ubuntu 24.04 | Ubuntu | wymagania budowy jądra |
| `gen_init_cpio` | z jądra 7.3.5 | źródła jądra | budowa initramfs bez uprawnień roota, deterministycznie (`-t`) |
| meson (wheel z PyPI, suma w `src/zewnetrzne/mesa/zrodla.txt`), `ninja-build`, `python3-mako`, `-packaging`, `-yaml` | 1.7.2 / Ubuntu 24.04 | PyPI / Ubuntu 24.04 | budowa Mesy dla strefy (0.10.9 krok 6; meson Ubuntu 1.3 jest za stary dla Mesy 26) |
| `libva-dev`, `nasm` | Ubuntu 24.04 (libva 2.20) | Ubuntu 24.04 | budowa FFmpeg dla strefy (0.13.2 krok 2): nagłówki VA-API do linkowania, asembler x86 dekodera H.264 |
| LLVM 18, clang, libclc, tłumacz SPIR-V (`llvm-18-dev`, `libclang-18-dev`, `libclc-18-dev`, `llvm-spirv-18`, `libllvmspirvlib-18-dev`, `spirv-tools`); `libdrm-dev`, `libwayland-dev`, `libwayland-egl-backend-dev`, `wayland-protocols` (1.45) | Ubuntu 24.04 | Ubuntu 24.04 | Mesa 26 kompiluje swoje programy karty Intela z OpenCL C (`mesa_clc`, etap 1 `zbuduj-mese.sh`) — LLVM tylko przy budowie, nie trafia do strefy; nagłówki i biblioteki do linkowania Mesy |
| squashfs-tools (`mksquashfs`) | 4.6.1 | Ubuntu 24.04 | budowa obrazu `/rdzen` (zstd, `SOURCE_DATE_EPOCH`) |
| FreeType (`libfreetype-dev`) + `pkg-config` | 2.13.2 | Ubuntu 24.04 | `narzedzia/czcionki.c`: rasteryzacja czcionek do bitmap przy budowie (ADR 0009); nie trafia do obrazu |
| zlib (`zlib1g-dev`) | 1.3 | Ubuntu 24.04 | `narzedzia/ekran_startowy.c`: kompresja PNG logo startowego dla GRUB-a (0.5.8); nie trafia do systemu K1 |
| `grub-mkimage` (`grub-common`), `xorriso`, `mtools` | 2.12 / 1.5.6 / 4.0.43 | Ubuntu 24.04 | składanie obrazu ISO (ADR 0010; od 0.5.6 bez `grub-mkrescue` — powtarzalne) |
| `mtools`, `sfdisk` (`fdisk`), `xz`, `apt-get download` + `dpkg-deb` | 4.0.43 / 2.39.3 / 5.4 | Ubuntu 24.04 | obraz na pendrive z partycją danych (ADR 0016); podpisane pliki EFI |

Wersje użyte w danym buildzie zapisuje `build/wynik/srodowisko.txt`.

## Tylko do testów i developmentu (nie wpływają na obraz)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| QEMU (`qemu-system-x86`) | 8.2.2 | Ubuntu 24.04 | uruchamianie K1 w testach |
| OVMF | 2024.02 | Ubuntu 24.04 | firmware UEFI (testy Secure Boot, od 0.7.0) |
| clang + `libclang-rt-18-dev` | 18 | Ubuntu 24.04 | libFuzzer + sanitizery (fuzzing parserów) |
| glibc + ASan/UBSan (gcc) | systemowe | Ubuntu 24.04 | testy jednostkowe na hoście |
| `sfdisk` (`fdisk`, util-linux) | 2.39.3 | Ubuntu 24.04 | tablice partycji (GPT, MBR) na dyskach testowych w QEMU (M5a) |
| `iasl` (`acpica-tools`) | 20230628 | Ubuntu 24.04 | sztuczna bateria laptopa dla QEMU (tablica ACPI z `testy/acpi/bateria.asl`, 0.5.3) |
| Python 3 | systemowy | Ubuntu 24.04 | zrzuty ekranu (PPM→PNG), sterowanie monitorem QEMU (mysz, klawiatura) i sprawdzanie zrzutów w testach |
| kernel-hardening-checker | v0.6.17.1 (`afc376f2…`) | GitHub | **tylko dane**: lista zaleceń KSPP czytana jako tekst przez `scripts/sprawdz-kspp.sh`; kod narzędzia nie jest uruchamiany |

## Planowane (do decyzji)
- Dźwięk (0.12, `docs/archiwum/PLAN-0.12.0.txt`): bez obcej biblioteki w K1 — ALSA przez ioctl z nagłówków UAPI (jak DRM bez
  libdrm); w strefie przeglądarki biblioteka ALSA z Debiana już jest (`libasound2t64`, zależność Firefoksa).
