# Tally light dla RØDECaster Video

Ten fork dodaje obsługę **RØDECaster Video (RCV)** do firmware [ATEM_tally_light_with_ESP8266](https://github.com/AronHetLam/ATEM_tally_light_with_ESP8266).
Obsługa ATEM działa dalej bez zmian. Typ miksera wybiera się na stronie konfiguracji płytki.

## Jak to działa

```
RØDECaster Video ──TCP 10024 (OSC)──► Płytka „master” (typ: RODECaster Video)
                                          │  tally server (protokół ATEM, UDP)
                    ┌─────────────────────┼─────────────────────┐
                    ▼                     ▼                     ▼
              Płytka 2             Płytka 3             Płytka 4 ...
       (typ: ATEM / Tally server, IP = adres płytki master)
```

- Z mikserem łączy się **tylko jedna płytka** (master). Dzięki temu RCV obsługuje jedno połączenie, a nie jedno na każdą lampkę.
- Każda płytka ma wbudowany **tally server** (tak samo jak w wersji dla ATEM). Pozostałe lampki łączą się z masterem, jakby był ATEM-em. Mogą mieć nawet oryginalny firmware.
- Jeden serwer na ESP8266 obsługuje do 5 klientów. Przy większej liczbie lampek można je łączyć łańcuchowo, bo każda lampka-klient też jest serwerem dla kolejnych.

## Numeracja

| Tally Light number | Źródło w RCV |
|---|---|
| 1–6 | Input 1–6 (HDMI/USB/…) |

## Sceny i picture-in-picture

Master **sam odczytuje skład scen z RCV**. Po połączeniu mikser wysyła pełny opis show (XML), a w nim warstwy każdej sceny, np.:

```xml
<Scene name="SCENE A">
  <VideoLayers>
    <Layer source="videoInput" source_file="3" video_enabled="true" .../>
    <Layer source="videoInput" source_file="1" video_enabled="true" .../>
  </VideoLayers>
</Scene>
```

Scena A zawiera więc wejścia 1 i 3. Gdy jest:
- na programie, lampki 1 i 3 świecą na **czerwono**,
- na podglądzie (tryb studio), lampki 1 i 3 świecą na **zielono**.

Zasady:
- Liczą się tylko warstwy `videoInput` z `video_enabled="true"`. Warstwy z plikami (np. tło `tlo.jpg`) i overlaye nie zapalają kamer.
- Kamera, która jest jednocześnie w scenie na programie i w scenie na podglądzie, świeci na czerwono, tak samo jak w ATEM.
- Gdy zmienisz scenę na mikserze, RCV wysyła komunikat `/show/scene/...`, a master po sekundzie pobiera opis show od nowa.
- Wykryte wejścia widać na stronie mastera w kolumnie *Detected*.

Automatyczne wykrywanie włącza i wyłącza opcja *Detect inputs in scenes automatically* (domyślnie włączona).
Pole wyboru *In 1–6* przy każdej scenie pozwala **dodać** wejścia ręcznie. Przydaje się to, gdy wykrywanie jest wyłączone albo czegoś nie złapało. Wejścia ręczne i wykryte sumują się.
Wszystko ustawia się **tylko na masterze**, bo pozostałe lampki dostają gotowy stan.

Media (klipy) na programie nie zapalają żadnej kamery.

## Tryb instant i studio

- **Studio**: program jest czerwony, podgląd zielony.
- **Instant**: RCV nie ma podglądu, więc świeci tylko program.

## Tryb „On Air”

Dla RCV lampka w trybie On Air świeci na czerwono, gdy trwa **streaming lub nagrywanie**.

## Konfiguracja krok po kroku

1. Wgraj firmware na wszystkie płytki (instrukcja poniżej).
2. **Master:** otwórz stronę płytki (`http://<ip płytki>`) i ustaw:
   - *Switcher type* = **RODECaster Video**,
   - *Switcher IP* = IP miksera RCV (najlepiej ustaw w RCV stały adres albo rezerwację DHCP),
   - zostaw włączone *Detect inputs in scenes automatically*,
   - *Tally Light number*, jeśli master też jest lampką na kamerze.
3. **Pozostałe płytki:** *Switcher type* = **ATEM / Tally server**, *Switcher IP* = IP mastera. Masterowi warto ustawić stały IP (*Use static IP*).

Status na stronie mastera pokazuje, co jest na programie i podglądzie, oraz liczbę odebranych wiadomości.

## Kompilacja i wgrywanie

```bash
pio run -e ESP8266 -t upload        # Wemos D1 mini / ESP8266
pio run -e ESP32 -t upload          # ESP32
```

Na Macu z Apple Silicon toolchain dla ESP8266 wymaga Rosetty (`softwareupdate --install-rosetta`). ESP32 kompiluje się natywnie.
Można też użyć GitHub Actions (`.github/workflows/build_binaries.yml`), który buduje wszystkie warianty.

## Narzędzia (folder `tools/`)

Narzędzia wymagają tylko Pythona 3, bez dodatkowych pakietów.

- **`rcv_monitor.py <ip RCV>`** łączy się z prawdziwym RCV i wypisuje wszystko, co przychodzi. Zapisuje też pełny zrzut `/show` (XML) do `rcv_show.xml`.
  Uruchom go z prawdziwym mikserem, gdy coś nie działa. Pokaże, co dokładnie wysyła RCV.
- **`rcv_simulator.py`** to udawany RCV do testów lampek bez miksera. Ustaw na masterze IP komputera i wpisuj komendy, np. `p 2`, `ps B`, `v 3`, `cut`, `instant`.
- **`test_rcv_parser.cpp`** to testy jednostkowe parsera, uruchamiane na komputerze (komenda kompilacji jest w pliku). Jako argument można podać zapisany `rcv_show.xml`, a test wypisze wykryty skład scen.

## Co jest niepewne (do sprawdzenia na prawdziwym RCV)

Protokół nie jest oficjalnie udokumentowany. Wszystko pochodzi z kodu [modułu Bitfocus Companion](https://github.com/bitfocus/companion-module-rode-rcv).

- `/show/pgmcurrent` i `/show/pvwcurrent` są głównym źródłem stanu i mają numerację od 1. Moduł Companion polega na nich tak samo.
- Stan zaraz po połączeniu, zanim przyjdzie pierwsze `pgmcurrent`, jest zgadywany. Najpierw z kolorów przycisków wejść (`/device/buttons/8..13/colour`), potem z atrybutów `PgmScene`/`PvwScene` w XML.
  Prawdziwy RCV wysyła w nich jednak wartości ujemne (np. `PgmScene="-65"`) o nieznanym znaczeniu. Firmware traktuje je jako „brak sceny”, więc ich nie używa.
- Nie wiadomo, czy wiadomość `/remote` zmienia coś widocznego na samym mikserze. Companion wysyła ją zawsze.
