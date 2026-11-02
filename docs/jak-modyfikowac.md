# Jak modyfikować Kons-OS

Każda zmiana wypchnięta na GitHuba w katalogu `config/` uruchamia nowy build ISO.

## Dodanie programu

Dopisz nazwę pakietu Debiana do jednej z list w `config/package-lists/`
(np. `kons-desktop.list.chroot`). Nazwy pakietów wyszukasz na
<https://packages.debian.org> (wybierz wersję *trixie*).

Przykład — pakiet biurowy LibreOffice:

```
libreoffice-writer
libreoffice-calc
libreoffice-l10n-pl
```

## Zmiana tapety

Podmień plik `config/includes.chroot/usr/share/backgrounds/kons-os/kons-wallpaper.png`
(najlepiej 1920×1080, ta sama nazwa).

## Własne polecenia

Plik wrzucony do `config/includes.chroot/usr/local/bin/` trafi do systemu jako polecenie
(pamiętaj o `chmod +x`). Przykładem jest `kons-info`.

## Zmiana wersji

Zmień numer w pliku `VERSION` — trafi do nazwy ISO i do `/etc/os-release`.

## Pomysły na kolejne kroki

- [ ] Instalator na dysk (Calamares) z własnym brandingiem
- [ ] Własny ekran startowy GRUB-a i Plymouth (logo przy uruchamianiu)
- [ ] Aplikacja powitalna po pierwszym uruchomieniu
- [ ] Własne repozytorium pakietów `.deb` z programami Kons-OS
- [ ] Wydania (Releases) na GitHubie przy tagu `v*`
