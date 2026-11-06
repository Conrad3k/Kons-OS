# Zależności K1

Zasada (SPEC §2.7): każda zewnętrzna biblioteka i narzędzie wymaga uzasadnienia. Rozróżniamy to,
co trafia **do obrazu** (działa na komputerze użytkownika), od tego, co służy tylko **do budowy i testów**.

## W obrazie (baza zaufania)

| Zależność | Wersja | Licencja | Źródło i weryfikacja | Uzasadnienie |
|---|---|---|---|---|
| Jądro Linux | 6.18.60 (LTS) | GPL-2.0 | cdn.kernel.org; SHA-256 przypięta w `jadro/wersja.env`, podpis PGP wydania sprawdzany przy każdym pobraniu | fundament systemu (SPEC §4) |
| musl libc (statycznie w każdym programie) | 1.2.4-2 (Ubuntu 24.04) | MIT | archiwum Ubuntu (podpisane repozytoria apt) | mała, czytelna libc ze wsparciem statycznego linkowania (SPEC §4); ADR 0002 |

Nic więcej: brak bibliotek współdzielonych, interpreterów, powłoki.

## Do budowy (wpływają na wynik)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| GCC (+ `gcc-13-plugin-dev`) | 13.3.0 | Ubuntu 24.04 | kompilacja jądra (z wtyczkami KSTACK_ERASE, LATENT_ENTROPY) i programów K1 |
| `musl-tools` (`musl-gcc`) | 1.2.4-2 | Ubuntu 24.04 | wrapper kompilatora dla musl |
| binutils, make, flex, bison, bc, libelf, libssl, rsync, cpio, xz | Ubuntu 24.04 | Ubuntu | wymagania budowy jądra |
| `gen_init_cpio` | z jądra 6.18.60 | źródła jądra | budowa initramfs bez uprawnień roota, deterministycznie (`-t`) |
| squashfs-tools (`mksquashfs`) | 4.6.1 | Ubuntu 24.04 | budowa obrazu `/rdzen` (zstd, `SOURCE_DATE_EPOCH`) |

Wersje użyte w danym buildzie zapisuje `build/wynik/srodowisko.txt`.

## Tylko do testów i developmentu (nie wpływają na obraz)

| Narzędzie | Wersja | Źródło | Po co |
|---|---|---|---|
| QEMU (`qemu-system-x86`) | 8.2.2 | Ubuntu 24.04 | uruchamianie K1 w testach |
| OVMF | 2024.02 | Ubuntu 24.04 | firmware UEFI (M10: Secure Boot) |
| clang + `libclang-rt-18-dev` | 18 | Ubuntu 24.04 | libFuzzer + sanitizery (fuzzing parserów) |
| glibc + ASan/UBSan (gcc) | systemowe | Ubuntu 24.04 | testy jednostkowe na hoście |
| Python 3 | systemowy | Ubuntu 24.04 | zrzuty ekranu (PPM→PNG), obsługa monitora QEMU w testach |
| kernel-hardening-checker | v0.6.17.1 (`afc376f2…`) | GitHub | **tylko dane**: lista zaleceń KSPP czytana jako tekst przez `scripts/sprawdz-kspp.sh`; kod narzędzia nie jest uruchamiany |

## Planowane (do decyzji)
- `fortify-headers` (GitHub) — `_FORTIFY_SOURCE` z musl; decyzja w M3 (ADR 0002).
- Własny build musl 1.2.5 z poprawkami bezpieczeństwa (pakiet źródłowy Debiana) — przed M3.
