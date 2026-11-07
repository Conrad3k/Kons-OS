# Zależności K1

Zasada (SPEC §2.7): każda zewnętrzna biblioteka i narzędzie wymaga uzasadnienia. Rozróżniamy to,
co trafia **do obrazu** (działa na komputerze użytkownika), od tego, co służy tylko **do budowy i testów**.

## W obrazie (baza zaufania)

| Zależność | Wersja | Licencja | Źródło i weryfikacja | Uzasadnienie |
|---|---|---|---|---|
| Jądro Linux | 6.18.60 (LTS) | GPL-2.0 | cdn.kernel.org; SHA-256 przypięta w `jadro/wersja.env`, podpis PGP wydania sprawdzany przy każdym pobraniu | fundament systemu (SPEC §4) |
| musl libc (statycznie w każdym programie) | 1.2.4-2 (Ubuntu 24.04) | MIT | archiwum Ubuntu (podpisane repozytoria apt) | mała, czytelna libc ze wsparciem statycznego linkowania (SPEC §4); ADR 0002 |
| IBM Plex Sans / Serif — **tylko bitmapy glifów** wbudowane w `gui` i aplikacje | 6.1.1 (`fonts-ibm-plex`) | SIL OFL 1.1 | archiwum Ubuntu | tekst z polskimi znakami bez parsera czcionek w systemie (ADR 0009) |

Nic więcej: brak bibliotek współdzielonych, interpreterów, powłoki. Grafika bez bibliotek:
API KMS jądra przez ioctl (nagłówki UAPI z naszego jądra), renderer i przybornik GUI własne
(`src/grafika/`, `src/przybornik/`).

### Tylko w obrazie ISO (start w maszynie wirtualnej, do M10)
| Zależność | Wersja | Licencja | Źródło | Uzasadnienie |
|---|---|---|---|---|
| GRUB (EFI x86_64 i BIOS i386-pc) | 2.12-1ubuntu7.3 | GPL-3.0 | archiwum Ubuntu (`grub-efi-amd64-bin`, `grub-pc-bin`) | start z płyty przez UEFI i BIOS w maszynach wirtualnych; tymczasowo do podpisanego UKI w M10 (ADR 0010, RYZYKA R25) |

## Do budowy (wpływają na wynik)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| GCC (+ `gcc-13-plugin-dev`) | 13.3.0 | Ubuntu 24.04 | kompilacja jądra (z wtyczkami KSTACK_ERASE, LATENT_ENTROPY) i programów K1 |
| `musl-tools` (`musl-gcc`) | 1.2.4-2 | Ubuntu 24.04 | wrapper kompilatora dla musl |
| binutils, make, flex, bison, bc, libelf, libssl, rsync, cpio, xz | Ubuntu 24.04 | Ubuntu | wymagania budowy jądra |
| `gen_init_cpio` | z jądra 6.18.60 | źródła jądra | budowa initramfs bez uprawnień roota, deterministycznie (`-t`) |
| squashfs-tools (`mksquashfs`) | 4.6.1 | Ubuntu 24.04 | budowa obrazu `/rdzen` (zstd, `SOURCE_DATE_EPOCH`) |
| FreeType (`libfreetype-dev`) + `pkg-config` | 2.13.2 | Ubuntu 24.04 | `narzedzia/czcionki.c`: rasteryzacja czcionek do bitmap przy budowie (ADR 0009); nie trafia do obrazu |
| `grub-mkrescue` (`grub-common`), `xorriso`, `mtools` | 2.12 / 1.5.6 / 4.0.43 | Ubuntu 24.04 | składanie obrazu ISO (ADR 0010) |

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
