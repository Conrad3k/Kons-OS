"""Obrazy do testu dekodera (test_dekoder, 0.8.0 krok 4, ADR 0032), zrobione Pillow — tak, jak zapisuje je aparat
albo inny program, a nie nasz kod. Użycie: python3 -I obrazy_dekoder.py KATALOG

  kolory.png / kolory.jpg / progresywny.jpg   64×48: lewa połowa czerwona, prawa niebieska
  alfa.png                                    32×32 RGBA: lewa połowa przezroczysta, prawa zielona
  paleta.png, szary16.png, szary.jpg          paleta, 16 bitów szarości, JPEG w odcieniach szarości
  obrot3.jpg / obrot6.jpg / obrot8.jpg        60×40, czerwony lewy górny róg, EXIF orientacja 3, 6, 8
  duzy.png                                    nagłówek 30000×30000 (za duży — dekoder ma odmówić)
  uciety.jpg, zla-suma.png, tekst.txt         uszkodzone albo nie-obraz
  (0.8.2 krok 3)
  kolory.gif / kolory.bmp / paleta.bmp        jak kolory.png; GIF animowany: pierwsza klatka czerwona
  kolory-stratny.webp / kolory.webp           WebP stratny (VP8) i bezstratny (VP8L), alfa-stratny.webp z alfą
  48mpx.jpg, 48mpx-prog.jpg                   8000×6000 (tryb 48 Mpx w telefonach), zwykły i progresywny
  100mpx.jpg, 100mpx-prog.jpg                 10000×10000 — dokładnie granica
  101mpx.jpg                                  nagłówek 10050×10000 (za duży — dekoder ma powiedzieć ile)
  cmyk-prog.jpg                               nagłówek 10000×10000 CMYK progresywny: nie mieści się w pamięci dekodera
"""
import io
import os
import struct
import sys
import zlib

from PIL import Image

OUT = sys.argv[1]


def pol(rozmiar, lewa, prawa, tryb="RGB"):
    im = Image.new(tryb, rozmiar, prawa)
    im.paste(lewa, (0, 0, rozmiar[0] // 2, rozmiar[1]))
    return im


def zapisz(nazwa, dane):
    with open(f"{OUT}/{nazwa}", "wb") as f:
        f.write(dane)


kolory = pol((64, 48), (255, 0, 0), (0, 0, 255))
kolory.save(f"{OUT}/kolory.png")
kolory.save(f"{OUT}/kolory.jpg", quality=95)
kolory.save(f"{OUT}/progresywny.jpg", quality=95, progressive=True)
pol((32, 32), (0, 0, 0, 0), (0, 255, 0, 255), "RGBA").save(f"{OUT}/alfa.png")
kolory.convert("P", palette=Image.ADAPTIVE, colors=4).save(f"{OUT}/paleta.png")
Image.new("I;16", (16, 16), 0x8080).save(f"{OUT}/szary16.png")
Image.new("L", (16, 16), 128).save(f"{OUT}/szary.jpg", quality=95)

for o in (3, 6, 8):
    im = Image.new("RGB", (60, 40), (255, 255, 255))
    im.paste((255, 0, 0), (0, 0, 20, 20))
    exif = Image.Exif()
    exif[0x0112] = o
    im.save(f"{OUT}/obrot{o}.jpg", quality=95, exif=exif.tobytes())


def kawalek(typ, dane):
    return struct.pack(">I", len(dane)) + typ + dane + struct.pack(">I", zlib.crc32(typ + dane))


zapisz("duzy.png", b"\x89PNG\r\n\x1a\n" + kawalek(b"IHDR", struct.pack(">IIBBBBB", 30000, 30000, 8, 2, 0, 0, 0)) +
       kawalek(b"IDAT", zlib.compress(b"")) + kawalek(b"IEND", b""))

with open(f"{OUT}/kolory.jpg", "rb") as f:
    jpg = f.read()
zapisz("uciety.jpg", jpg[: len(jpg) // 2])
b = io.BytesIO()
kolory.save(b, format="PNG")
png = bytearray(b.getvalue())
i = png.index(b"IDAT")
png[i + 8] ^= 0xFF  # a byte of the compressed data: the checksum no longer matches
zapisz("zla-suma.png", bytes(png))
zapisz("tekst.txt", "to nie jest obraz\n".encode())


# ---- 0.8.2 krok 3: GIF, BMP, WebP ----
kolory.save(f"{OUT}/kolory.gif")
klatki = [Image.new("RGB", (64, 48), (255, 0, 0)), Image.new("RGB", (64, 48), (0, 0, 255))]
klatki[0].save(f"{OUT}/animacja.gif", save_all=True, append_images=klatki[1:], duration=100, loop=0)
kolory.save(f"{OUT}/kolory.bmp")
kolory.convert("P", palette=Image.ADAPTIVE, colors=4).save(f"{OUT}/paleta.bmp")
kolory.save(f"{OUT}/kolory-stratny.webp", quality=95)
kolory.save(f"{OUT}/kolory.webp", lossless=True)
pol((32, 32), (0, 0, 0, 0), (0, 255, 0, 255), "RGBA").save(f"{OUT}/alfa-stratny.webp", quality=95)


def sof(dane, w, h):
    """The width and height in the first SOF marker of a JPEG changed (the data stays what it was)."""
    d = bytearray(dane)
    i = 2
    while d[i + 1] not in (0xC0, 0xC1, 0xC2):
        i += 2 + (d[i + 2] << 8 | d[i + 3])
    d[i + 5:i + 9] = struct.pack(">HH", h, w)
    return bytes(d)


b = io.BytesIO()
Image.new("CMYK", (16, 16), (0, 0, 0, 0)).save(b, format="JPEG", progressive=True)
zapisz("cmyk-prog.jpg", sof(b.getvalue(), 10000, 10000))
zapisz("101mpx.jpg", sof(jpg, 10050, 10000))


def zdjecie(w, h):
    """A photo-like picture: the left half red, the right half blue, with a gradient (more to decode than a flat one)."""
    im = pol((w, h), (255, 0, 0), (0, 0, 255))
    pas = Image.linear_gradient("L").resize((w, h // 8))
    im.paste(Image.merge("RGB", (pas, pas, pas)), (0, h - h // 8))
    return im


if os.environ.get("K1_DUZE_ZDJECIA", "1") == "1":
    for nazwa, w, h in (("48mpx", 8000, 6000), ("100mpx", 10000, 10000)):
        im = zdjecie(w, h)
        im.save(f"{OUT}/{nazwa}.jpg", quality=90)
        im.save(f"{OUT}/{nazwa}-prog.jpg", quality=90, progressive=True)
        del im
