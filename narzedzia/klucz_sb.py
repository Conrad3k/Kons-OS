"""K1 — the Secure Boot key (M6d, ADR 0022). Runs on the build host: python3 -I narzedzia/klucz_sb.py KATALOG

Derives an RSA-2048 key and a self-signed X.509 certificate deterministically from the release seed
(environment K1_KLUCZ_PODPISU, 32 bytes in base64 — the same secret that signs the manifests; never
in the repository). Without the seed the key comes from a fixed, PUBLIC developer seed: such a key
signs developer builds for tests only, and K1 never offers to enrol it (it is marked "deweloperski").

Writes to KATALOG: k1-sb.key (PEM, 0600 — the signing tool reads it, the build deletes it), k1-sb.crt
(PEM), k1-sb.cer (DER — what MokManager enrols), k1_sb_cert.h (the certificate for nadzorca).

Why derived: shim's MOK accepts RSA certificates, the owner keeps one secret, and every build of a
release key gives the same certificate, so the key is enrolled once. The RSA primes come from
SHAKE-256 of the seed with a domain label (independent of the EdDSA manifest key), Miller-Rabin with
64 rounds; the certificate (validity 2026-01-01 .. 9999-12-31, serial 1) is assembled here in DER and
signed with RSASSA-PKCS1-v1_5 / SHA-256, which is deterministic — the same seed gives the same bytes."""
import base64
import hashlib
import os
import sys

E = 65537
DEV_SEED = hashlib.sha256("Kons K1: klucz deweloperski Secure Boot (publiczny, nie ufac)".encode()).digest()


class Strumien:
    """Deterministic bytes: SHAKE-256(label || seed || counter)."""

    def __init__(self, seed):
        self.seed, self.n = seed, 0

    def bajty(self, ile):
        self.n += 1
        return hashlib.shake_256(b"K1-SB-RSA-v1" + self.seed + self.n.to_bytes(8, "big")).digest(ile)


MALE = [p for p in range(3, 2000) if all(p % d for d in range(2, int(p ** 0.5) + 1))]


def pierwsza(s, bity):
    while True:
        c = int.from_bytes(s.bajty(bity // 8), "big") | (3 << (bity - 2)) | 1
        if any(c % p == 0 for p in MALE) or (c - 1) % E == 0:
            continue
        d, r = c - 1, 0
        while d % 2 == 0:
            d //= 2
            r += 1
        ok = True
        for _ in range(64):
            a = 2 + int.from_bytes(s.bajty(bity // 8), "big") % (c - 3)
            x = pow(a, d, c)
            if x in (1, c - 1):
                continue
            for _ in range(r - 1):
                x = pow(x, 2, c)
                if x == c - 1:
                    break
            else:
                ok = False
                break
        if ok:
            return c


# ---- DER ----
def dlugosc(n):
    if n < 128:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b


def tlv(tag, tresc):
    return bytes([tag]) + dlugosc(len(tresc)) + tresc


def liczba(v):
    b = v.to_bytes(max(1, (v.bit_length() + 8) // 8), "big")  # leading zero keeps it positive
    return tlv(0x02, b)


def oid(tekst):
    cz = [int(x) for x in tekst.split(".")]
    out = bytes([40 * cz[0] + cz[1]])
    for v in cz[2:]:
        k = [v & 0x7F]
        v >>= 7
        while v:
            k.append(0x80 | (v & 0x7F))
            v >>= 7
        out += bytes(reversed(k))
    return tlv(0x06, out)


def sekw(*cz):
    return tlv(0x30, b"".join(cz))


def nazwa(cn):
    return sekw(tlv(0x31, sekw(oid("2.5.4.10"), tlv(0x0C, "Kons K1".encode()))),
                tlv(0x31, sekw(oid("2.5.4.3"), tlv(0x0C, cn.encode()))))


def main():
    katalog = sys.argv[1]
    tekst = os.environ.get("K1_KLUCZ_PODPISU", "").strip()
    prawdziwy = bool(tekst)
    if prawdziwy:
        seed = base64.b64decode(tekst.replace("-", "+").replace("_", "/") + "=" * (-len(tekst) % 4))
        if len(seed) != 32:
            sys.exit("klucz_sb: K1_KLUCZ_PODPISU musi mieć 32 bajty")
    else:
        seed = DEV_SEED
    s = Strumien(seed)
    while True:
        p, q = pierwsza(s, 1024), pierwsza(s, 1024)
        n = p * q
        if p != q and n.bit_length() == 2048:
            break
    if p < q:
        p, q = q, p
    lam = (p - 1) * (q - 1) // __import__("math").gcd(p - 1, q - 1)
    d = pow(E, -1, lam)
    klucz_der = sekw(liczba(0), liczba(n), liczba(E), liczba(d), liczba(p), liczba(q), liczba(d % (p - 1)), liczba(d % (q - 1)),
                     liczba(pow(q, -1, p)))

    rsa_pub = sekw(liczba(n), liczba(E))
    alg_rsa = sekw(oid("1.2.840.113549.1.1.1"), b"\x05\x00")
    spki = sekw(alg_rsa, tlv(0x03, b"\x00" + rsa_pub))
    alg_podpisu = sekw(oid("1.2.840.113549.1.1.11"), b"\x05\x00")
    cn = "Kons K1 Secure Boot" if prawdziwy else "Kons K1 klucz deweloperski - NIE UFAC"
    kid = hashlib.sha1(rsa_pub).digest()
    rozszerzenia = sekw(
        sekw(oid("2.5.29.19"), tlv(0x01, b"\xff"), tlv(0x04, sekw())),                 # basicConstraints CA:FALSE
        sekw(oid("2.5.29.15"), tlv(0x01, b"\xff"), tlv(0x04, tlv(0x03, b"\x07\x80"))),  # keyUsage digitalSignature
        sekw(oid("2.5.29.37"), tlv(0x04, sekw(oid("1.3.6.1.5.5.7.3.3")))),              # extKeyUsage codeSigning
        sekw(oid("2.5.29.14"), tlv(0x04, tlv(0x04, kid))),                              # subjectKeyIdentifier
        sekw(oid("2.5.29.35"), tlv(0x04, sekw(tlv(0x80, kid)))),                        # authorityKeyIdentifier
    )
    tbs = sekw(tlv(0xA0, liczba(2)), liczba(1), alg_podpisu, nazwa(cn),
               sekw(tlv(0x17, b"260101000000Z"), tlv(0x18, b"99991231235959Z")), nazwa(cn), spki,
               tlv(0xA3, rozszerzenia))
    digest_info = sekw(sekw(oid("2.16.840.1.101.3.4.2.1"), b"\x05\x00"), tlv(0x04, hashlib.sha256(tbs).digest()))
    em = b"\x00\x01" + b"\xff" * (256 - 3 - len(digest_info)) + b"\x00" + digest_info
    podpis = pow(int.from_bytes(em, "big"), d, n).to_bytes(256, "big")
    cert = sekw(tbs, alg_podpisu, tlv(0x03, b"\x00" + podpis))

    def pem(rodzaj, der):
        b = base64.b64encode(der).decode()
        return f"-----BEGIN {rodzaj}-----\n" + "\n".join(b[i:i + 64] for i in range(0, len(b), 64)) + \
            f"\n-----END {rodzaj}-----\n"

    os.makedirs(katalog, exist_ok=True)
    sciezka = os.path.join(katalog, "k1-sb.key")
    fd = os.open(sciezka, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(pem("RSA PRIVATE KEY", klucz_der))
    with open(os.path.join(katalog, "k1-sb.crt"), "w") as f:
        f.write(pem("CERTIFICATE", cert))
    with open(os.path.join(katalog, "k1-sb.cer"), "wb") as f:
        f.write(cert)
    with open(os.path.join(katalog, "k1_sb_cert.h"), "w") as f:
        f.write("/* Generated by narzedzia/klucz_sb.py — the certificate of the key that signs k1.efi (ADR 0022). */\n")
        f.write(f"#define K1_SB_PRAWDZIWY {1 if prawdziwy else 0} /* 0: the public developer key — never enrolled */\n")
        f.write(f"#define K1_SB_CERT_BAJTY {len(cert)}u\n")
        f.write("static const unsigned char k1_sb_cert[K1_SB_CERT_BAJTY] = {")
        f.write(",".join(("\n    " if i % 16 == 0 else " ") + f"0x{b:02x}" for i, b in enumerate(cert)))
        f.write("\n};\n")
    print(f"klucz_sb: {'klucz wydania' if prawdziwy else 'klucz deweloperski (publiczny)'}, "
          f"certyfikat {hashlib.sha256(cert).hexdigest()[:16]}")


main()
