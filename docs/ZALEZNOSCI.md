# Zależności K1

Zasada (SPEC §2.7): każda zewnętrzna biblioteka i narzędzie wymaga uzasadnienia. Rozróżniamy to,
co trafia **do obrazu** (działa na komputerze użytkownika), od tego, co służy tylko **do budowy i testów**.

## W obrazie (baza zaufania)

| Zależność | Wersja | Licencja | Źródło i weryfikacja | Uzasadnienie |
|---|---|---|---|---|
| Jądro Linux | 6.18.60 (LTS) | GPL-2.0 | cdn.kernel.org; SHA-256 przypięta w `jadro/wersja.env`, podpis PGP wydania sprawdzany przy każdym pobraniu | fundament systemu (SPEC §4) |
| musl libc (statycznie w każdym programie) | 1.2.4-2 (Ubuntu 24.04) | MIT | archiwum Ubuntu (podpisane repozytoria apt) | mała, czytelna libc ze wsparciem statycznego linkowania (SPEC §4); ADR 0002 |
| IBM Plex Sans / Serif — **tylko bitmapy glifów** wbudowane w `gui` i aplikacje | 6.1.1 (`fonts-ibm-plex`) | SIL OFL 1.1 | archiwum Ubuntu | tekst z polskimi znakami bez parsera czcionek w systemie (ADR 0009) |
| Monocypher (statycznie w usłudze `skarbiec`) | 4.0.3 (tag `4.0.3`, commit `ab2b16dd`) | BSD-2-Clause albo CC0 (do wyboru) | github.com/LoupVaillant/monocypher; pliki w `src/zewnetrzne/monocypher/` bez zmian, SHA-256 (pełne sumy: `src/zewnetrzne/monocypher/SHA256SUMS`) | XChaCha20-Poly1305, BLAKE2b, Argon2id dla Blob Vault — mała, audytowana biblioteka, jeden plik (ADR 0015); aktualizacja: nowy tag, porównanie zmian, nowe sumy |

Nic więcej (poza Monocypherem w `skarbiec`): brak bibliotek współdzielonych, interpreterów, powłoki. Grafika bez bibliotek:
API KMS jądra przez ioctl (nagłówki UAPI z naszego jądra), renderer i przybornik GUI własne
(`src/grafika/`, `src/przybornik/`).

### Tylko w obrazie ISO (start w maszynie wirtualnej, do M10)
| Zależność | Wersja | Licencja | Źródło | Uzasadnienie |
|---|---|---|---|---|
| shim (UEFI, podpis Microsoftu) | 15.8 (`shim-signed` 1.58+15.8-0ubuntu1) | BSD-2-Clause | archiwum Ubuntu — pakiet tylko rozpakowany (`scripts/wspolne.sh: efi_podpisane`), wersje w `build/pobrane/efi/wersje.txt` | pierwszy program startowy z pendrive'a/płyty przez UEFI; podpisane pliki z każdej płyty Ubuntu zamiast własnego, niepodpisanego GRUB-a (Defender oznaczał go heurystyką jako trojana, 0.5.5) |
| GRUB dla nośników wymiennych (UEFI, `gcdx64.efi`, podpis Canonical) | 2.12 (`grub-efi-amd64-signed` 1.202.5+2.12-1ubuntu7.3) | GPL-3.0 | jw. | ładuje jądro K1 (Secure Boot musi być wyłączony do M10: jądro K1 nie jest podpisane) |
| GRUB BIOS (i386-pc, obraz El Torito z wbudowanymi modułami) | 2.12-1ubuntu7.3 | GPL-3.0 | archiwum Ubuntu (`grub-pc-bin`), `grub-mkimage` | start z płyty przez BIOS w maszynach wirtualnych; tymczasowo do M10 (ADR 0010, RYZYKA R25) |

## Do budowy (wpływają na wynik)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| GCC (+ `gcc-13-plugin-dev`) | 13.3.0 | Ubuntu 24.04 | kompilacja jądra (z wtyczkami KSTACK_ERASE, LATENT_ENTROPY) i programów K1 |
| `musl-tools` (`musl-gcc`) | 1.2.4-2 | Ubuntu 24.04 | wrapper kompilatora dla musl |
| binutils, make, flex, bison, bc, libelf, libssl, rsync, cpio, xz | Ubuntu 24.04 | Ubuntu | wymagania budowy jądra |
| `gen_init_cpio` | z jądra 6.18.60 | źródła jądra | budowa initramfs bez uprawnień roota, deterministycznie (`-t`) |
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
| OVMF | 2024.02 | Ubuntu 24.04 | firmware UEFI (M10: Secure Boot) |
| clang + `libclang-rt-18-dev` | 18 | Ubuntu 24.04 | libFuzzer + sanitizery (fuzzing parserów) |
| glibc + ASan/UBSan (gcc) | systemowe | Ubuntu 24.04 | testy jednostkowe na hoście |
| `sfdisk` (`fdisk`, util-linux) | 2.39.3 | Ubuntu 24.04 | tablice partycji (GPT, MBR) na dyskach testowych w QEMU (M5a) |
| `iasl` (`acpica-tools`) | 20230628 | Ubuntu 24.04 | sztuczna bateria laptopa dla QEMU (tablica ACPI z `testy/acpi/bateria.asl`, 0.5.3) |
| Python 3 | systemowy | Ubuntu 24.04 | zrzuty ekranu (PPM→PNG), sterowanie monitorem QEMU (mysz, klawiatura) i sprawdzanie zrzutów w testach |
| kernel-hardening-checker | v0.6.17.1 (`afc376f2…`) | GitHub | **tylko dane**: lista zaleceń KSPP czytana jako tekst przez `scripts/sprawdz-kspp.sh`; kod narzędzia nie jest uruchamiany |

## Planowane (do decyzji)
- `fortify-headers` (GitHub) — na razie nie (ADR 0007); ponownie przy własnym buildzie musl.
- Własny build musl 1.2.5 z poprawkami bezpieczeństwa (pakiet źródłowy Debiana).
