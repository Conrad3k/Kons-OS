#!/usr/bin/env python3
# Odświeża tabelę pakietów obrazu strefy przeglądarki (0.10.0 krok 2, ADR 0043): strefy/przegladarka.txt.
#
#   python3 -I scripts/odswiez-strefe.py            — najnowsze wersje z Debiana 13 (trixie, -updates, -security)
#
# Łańcuch zaufania: InRelease każdej gałęzi sprawdza gpgv kluczami Debiana 13 z repo (strefy/klucze/debian-trixie.gpg,
# skąd są — strefy/README.md); spis pakietów (Packages.xz) — sumą SHA-256 z podpisanego InRelease; każdy pakiet — sumą
# z tabeli (scripts/zbuduj-strefe.sh). Zależności: pierwsza możliwa z alternatyw, bez wersji (bierzemy najnowsze).
# Wynik zmienia się tylko, gdy Debian wyda nowe wersje — wtedy nowa wersja K1 (PATCH, poprawki przeglądarki).
import lzma, os, re, subprocess, sys, tempfile, time, urllib.request, hashlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KLUCZE = os.path.join(REPO, "strefy", "klucze", "debian-trixie.gpg")
TABELA = os.path.join(REPO, "strefy", "przegladarka.txt")
# (archiwum, adres, gałąź) — późniejsza gałąź wygrywa (poprawki bezpieczeństwa nad zwykłymi)
GALEZIE = [("debian", "https://deb.debian.org/debian", "trixie"),
           ("debian", "https://deb.debian.org/debian", "trixie-updates"),
           ("debian-security", "https://security.debian.org/debian-security", "trixie-security")]
# Co ma być w obrazie: przeglądarka, jej polski interfejs, dane lokalizacji C.UTF-8 (libc-bin); od 0.10.5 (ADR 0046)
# curl i zaufane certyfikaty — strefa pobierania aktualizacji (ten sam obraz, inny program startowy: gosc/pobieranie.c);
# od 0.10.9 krok 6 libdrm2 — dla naszej Mesy (szybka grafika: scripts/zbuduj-mese.sh; sama Mesa nie jest z Debiana);
# od 0.13.2 krok 2 libva2 i libva-drm2 — dla naszego FFmpeg (filmy H.264: scripts/zbuduj-ffmpeg.sh, ADR 0063 pkt 7).
KORZENIE = ["firefox-esr", "firefox-esr-l10n-pl", "libc-bin", "curl", "ca-certificates", "libdrm2", "libva2",
            "libva-drm2"]
# Czego nie bierzemy, choć pakiety tego chcą: demony i narzędzia systemu (w strefie nie ma init-u Debiana ani
# instalatora pakietów), powłoka i narzędzia, które jej używają (S14: strefa bez powłoki).
POMIN = {"debconf", "dpkg", "perl-base", "systemd", "systemd-sysv", "dbus", "dbus-user-session", "dbus-bin",
         "dbus-daemon", "dbus-system-bus-common", "dbus-session-bus-common", "libpam-systemd", "procps", "adduser",
         "base-files", "gawk", "mawk", "debianutils", "dash", "bash", "coreutils", "libc-dev-bin", "sensible-utils",
         "init-system-helpers", "passwd", "login",
         "openssl"}  # ca-certificates wants it only for its script (the bundle is made by zbuduj-strefe.sh)


def pobierz(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def podpisane(dane):
    """The verified text of an InRelease (gpgv with Debian 13's keys), or an exception."""
    with tempfile.TemporaryDirectory() as d:
        we, wy = os.path.join(d, "InRelease"), os.path.join(d, "tresc")
        open(we, "wb").write(dane)
        # gpgv ends with 2 when one of the signatures is by a key we do not keep (Debian 12's, still on Debian 13's
        # files): at least one good signature by Debian 13's keys and no bad one are enough — like apt.
        st = subprocess.run(["gpgv", "--status-fd", "1", "--keyring", KLUCZE, "--output", wy, we],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout.split("\n")
        if not any(w.startswith("[GNUPG:] GOODSIG ") for w in st) or any(w.startswith("[GNUPG:] BADSIG ") for w in st):
            sys.exit("InRelease bez dobrego podpisu Debiana 13")
        return open(wy, encoding="utf-8").read()


def spis(adres, galaz):
    tresc = podpisane(pobierz(f"{adres}/dists/{galaz}/InRelease"))
    sekcja = tresc.split("\nSHA256:\n", 1)[1]
    for w in sekcja.splitlines():
        if not w.startswith(" "):
            break
        suma, rozmiar, nazwa = w.split()
        if nazwa == "main/binary-amd64/Packages.xz":
            dane = pobierz(f"{adres}/dists/{galaz}/main/binary-amd64/Packages.xz")
            if hashlib.sha256(dane).hexdigest() != suma or len(dane) != int(rozmiar):
                sys.exit(f"{galaz}: Packages.xz niezgodny z InRelease")
            return lzma.decompress(dane).decode("utf-8")
    sys.exit(f"{galaz}: brak main/binary-amd64/Packages.xz w InRelease")


def wpisy(tekst, archiwum):
    for blok in tekst.split("\n\n"):
        p, klucz = {}, None
        for w in blok.split("\n"):
            if w.startswith(" ") and klucz:
                p[klucz] += "\n" + w
            elif ":" in w:
                klucz, v = w.split(":", 1)
                p[klucz] = v.strip()
        if "Package" in p:
            p["_archiwum"] = archiwum
            yield p


def rozwiaz(pakiety):
    dostarcza = {}
    for n, p in pakiety.items():
        for d in p.get("Provides", "").split(","):
            d = d.strip().split(" ")[0]
            if d:
                dostarcza.setdefault(d, []).append(n)
    wybrane, kolejka = {}, list(KORZENIE)
    while kolejka:
        n = kolejka.pop(0)
        if n in wybrane or n in POMIN:
            continue
        wybrane[n] = pakiety[n]
        for alt in (pakiety[n].get("Pre-Depends", "") + "," + pakiety[n].get("Depends", "")).split(","):
            if not alt.strip():
                continue
            wybor = None
            for a in alt.split("|"):
                a = re.sub(r"\(.*?\)|\[.*?\]|:any", "", a).strip()
                if a in POMIN:
                    wybor = ""
                    break
                if a in wybrane or a in pakiety:
                    wybor = a
                    break
                if a in dostarcza:
                    wybor = sorted(dostarcza[a])[0]
                    break
            if wybor is None:
                sys.exit(f"brak zależności „{alt.strip()}” pakietu {n}")
            if wybor:
                kolejka.append(wybor)
    return wybrane


def main():
    pakiety = {}
    for archiwum, adres, galaz in GALEZIE:
        for p in wpisy(spis(adres, galaz), archiwum):
            pakiety[p["Package"]] = p
    wybrane = rozwiaz(pakiety)
    chwila = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    with open(TABELA, "w", encoding="utf-8") as t:
        t.write("# Pakiety Debiana 13 obrazu strefy przeglądarki (0.10.0 krok 2, ADR 0043). Nie edytuj ręcznie:\n"
                "# python3 -I scripts/odswiez-strefe.py. Budowa: scripts/zbuduj-strefe.sh (suma każdego pliku).\n"
                f"# migawka {chwila} (zapasowe źródło: snapshot.debian.org/archive/<archiwum>/{chwila}/)\n"
                "# pakiet wersja archiwum sha256 plik\n")
        for n in sorted(wybrane):
            p = wybrane[n]
            t.write(f"{n} {p['Version']} {p['_archiwum']} {p['SHA256']} {p['Filename']}\n")
    print(f"{len(wybrane)} pakietów, {sum(int(p['Size']) for p in wybrane.values()) >> 20} MiB → {TABELA}")


if __name__ == "__main__":
    main()
