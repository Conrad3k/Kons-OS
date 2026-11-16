# 0020. Strefy: każda obca aplikacja w swojej mikro-maszynie wirtualnej

- Status: Przyjęty (kierunek); częściowo wdrożony w 0.10.0 — mikro-maszyny, zaufana ramka i przełącznik stała / jednorazowa
  (ADR 0042, 0043); przełącznik pełna izolacja / szybka grafika — później
- Data: 2026-11-07
- Dotyczy: SPEC §8 (strefa zgodności: zaplecze A i B), §9 (Gatekeeper), ADR 0019 (plan), RYZYKA R26

## Kontekst
Pomysł właściciela (2026-11-07): każdy program spoza K1 (przeglądarka, Discord, nawet Steam) ma
własne „pod-jądro”; system delikatnie to sygnalizuje innym kolorem; w Ustawieniach wybór, czy
środowisko jest stałe, czy usuwane po zamknięciu; bezpieczne przenoszenie plików z piaskownicy do
systemu. Dalej: „połączyć niezależność z wydajnością” i — ponieważ **X230 to tylko maszyna testowa,
celujemy wyżej** — wybór przy każdej aplikacji: pełna izolacja albo szybka grafika.

SPEC przewidywał dwa zaplecza strefy zgodności: A (przestrzenie nazw na jądrze K1, M7) i B
(mikro-VM, M11 — „dodatek na końcu”). W zapleczu A luka w jądrze = cały K1; w B luka w jądrze
gościa = tylko ta maszyna.

## Decyzja
1. **Zaplecze B jest głównym modelem**: każda obca aplikacja działa w **osobnej mikro-maszynie
   wirtualnej** (własne jądro Linux, minimalny system, jedna aplikacja). Zaplecze A (przestrzenie
   nazw) nie jest rozwijane jako sposób na obce aplikacje. Wymaga KVM (X230: VT-x/VT-d włączane w
   BIOS-ie). Wybór VMM (np. Cloud Hypervisor, crosvm, Firecracker) — ADR w M8.
2. **Dwa przełączniki przy każdej aplikacji** (Ustawienia → Aplikacje; zmiana działa od następnego
   uruchomienia):
   - **Trwałość:** *stała* (własny zaszyfrowany dysk maszyny w skarbcu: logowanie, zakładki zostają)
     albo *jednorazowa* (start z czystej kopii, po zamknięciu wszystko znika — warstwa zmian tylko w
     pamięci). Domyślnie: przeglądarka — wg decyzji o sesjach (M8), inne — stała.
   - **Grafika:** *pełna izolacja* (domyślnie: maszyna rysuje procesorem do pamięci współdzielonej,
     K1 tylko składa obraz — bez dostępu do karty) albo *szybka grafika* (karta graficzna przez
     pośrednika w K1: wirtualne GPU — na nowszym sprzęcie „native context”/Venus, na starszym
     virgl; pośrednik w osobnej klatce). Szybka grafika = słabsza izolacja: wyjaśnienie przy
     przełączniku i inny kolor ramki.
3. **Zaufana ramka rysowana przez ekran K1** (nie przez aplikację): każde okno strefy ma obwódkę w
   kolorze strefy i etykietę (nazwa aplikacji · „osobna maszyna”); inny odcień przy szybkiej
   grafice, inny przy maszynie jednorazowej. Aplikacja nie może narysować ramki K1 ani udawać okna
   systemu (domyka RYZYKA R26).
4. **Wydajność bez oddawania niezależności:** procesor przez wirtualizację sprzętową (strata ~2–5 %);
   wspólny, niezmienny obraz bazowy w pamięci tylko raz (tylko do odczytu); oddawanie nieużywanej
   pamięci (balon / free page reporting); start z zamrożonego stanu (snapshot) i — dla
   jednorazowych — gotowa czysta maszyna w zapasie. Opcjonalne **grupy** (kilka aplikacji w jednej
   maszynie) tylko na wyraźne życzenie, domyślnie każda osobno. [rozszerzone: ADR 0024 — wspólna strefa]
5. **Pliki i schowek tylko przez Gatekeepera (M9)**: z maszyny — folder „Do K1” → okno K1 z
   pytaniem (nazwa, rozmiar, skąd) → zapis do Plików jako dane (nigdy uruchamiane), z zapisanym
   pochodzeniem; podgląd przez jednorazowy dekoder w izolacji; do maszyny — „Wyślij do…” w Plikach;
   schowek między strefami — za każdym wklejeniem osobno.
6. **Sprzęt docelowy:** X230 (Ivy Bridge, HD 4000, 8 GB) jest maszyną testową — rozwiązania mają
   działać na nim w trybie pełnej izolacji; szybka grafika i gry są projektowane pod nowszy sprzęt
   (Intel Xe / AMD z obsługą wirtualnego GPU), bez obietnic dla X230.

## Konsekwencje
- Plan (ADR 0019): **M8 „Strefy”** = pierwsza mikro-maszyna + okno z niej na ekranie K1 + ramka +
  przełącznik trwałości (pierwsza aplikacja: przeglądarka; dawne M5.1/M8/M11 razem); przełącznik
  grafiki — po działającej pełnej izolacji, w M8 albo zaraz po; **M9 „Pliki”** = Gatekeeper.
  M7 „Sieć” daje maszynom sieć przez osobną usługę K1 (maszyna nie dotyka karty sieciowej).
- Nowe ryzyka: VMM i pośrednik GPU to duża powierzchnia ataku (klatki, ADR i testy w M8);
  pamięć RAM ogranicza liczbę maszyn naraz (X230: kilka).
- K1 potrzebuje KVM również w testach — CI z `/dev/kvm` (ADR 0019, 0.6.0) staje się konieczne.
