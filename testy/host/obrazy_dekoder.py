"""Obrazy do testu dekodera (test_dekoder, 0.8.0 krok 4, ADR 0032), zrobione Pillow — tak, jak zapisuje je aparat
albo inny program, a nie nasz kod. Użycie: python3 -I obrazy_dekoder.py KATALOG

  kolory.png / kolory.jpg / progresywny.jpg   64×48: lewa połowa czerwona, prawa niebieska
  alfa.png                                    32×32 RGBA: lewa połowa przezroczysta, prawa zielona
  paleta.png, szary16.png, szary.jpg          paleta, 16 bitów szarości, JPEG w odcieniach szarości
  obrot3.jpg / obrot6.jpg / obrot8.jpg        60×40, czerwony lewy górny róg, EXIF orientacja 3, 6, 8
  duzy.png                                    nagłówek 30000×30000 (za duży — dekoder ma odmówić)
  uciety.jpg, zla-suma.png, tekst.txt         uszkodzone albo nie-obraz
"""
import io
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
