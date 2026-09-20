# Handshake Cracker — dalsza mapa prac

Stan zapisany: 2026-09-19

Ten dokument utrwala uzgodnione etapy, aby plan nie zależał od historii czatu.
Nie oznacza, że wszystkie punkty są już zaimplementowane.

## Stan obecny

Gotowa jest podstawa pojedynczego zadania crackowania:

- przygotowanie lokalnego pliku HCCAPX,
- lokalny worker na Tab5,
- zdalni workerzy Grove, USB i M-BUS,
- synchronizacja capture i wordlisty z cache po CRC,
- trwałe markery zweryfikowanych plików po stronie JanOS,
- podział słownika na zakresy i zbieranie wyniku,
- ekran postępu z osobnymi stanami workerów,
- zachowanie dotychczasowych akcji `Crack` przy pliku i `Crack latest`.

Aktualna blokada sprzętowa: binarna synchronizacja pierwszego bloku do workera USB
kończy się `USB Sync fail`, mimo że komendy tekstowe, `READY`, Grove i M-BUS działają.
To trzeba zamknąć przed rozbudową managera.

## Etap 0 — stabilizacja transportów

- Naprawić i potwierdzić transfer capture oraz wordlisty przez USB.
- Zachować osobne traktowanie USB: bez zmiany baudrate UART.
- Zweryfikować cache hit po ponownym uruchomieniu: worker nie może ponownie kopiować
  poprawnie oznaczonego capture ani wordlisty.
- Kryterium odbioru: Grove, USB i M-BUS przechodzą `probe -> receive -> SYNCED`, a
  następne uruchomienie przechodzi przez `state=present`.

## Etap 1 — przygotowanie danych i sterowania

- Naprawić format zbiorczego `crack_state.csv`, aby przecinki i cudzysłowy w polach
  nie niszczyły danych. Preferowany jest prawidłowo cytowany CSV albo budowanie
  podsumowania z dziennika prób bez stratnego sanitizowania.
- Dodać jawne akcje dla istniejącego zadania:
  - `Resume`,
  - `Force re-run` / `Start over`.
- Opcjonalny blob HCCAPX w PSRAM pozostaje optymalizacją, nie warunkiem managera.

## Etap 2 — ekran Crack Manager

Dwa wejścia prowadzą do tego samego ekranu:

- przycisk `Crack` w nagłówku listy Handshakes,
- kafelek `Crack` na ekranie Compromised Data.

Ekran ma zawierać hybrydowy katalog plików lokalnych i plików widocznych na
Monsterach:

- checkboxy do wyboru wielu handshake'ów,
- oznaczenie źródła pliku,
- badge `generic` / nazwa wordlisty,
- stan: nowe, w toku, nieznalezione, znalezione, błąd,
- sekcję `Cracked`,
- popup pokazujący znalezione hasło,
- wybór źródła pliku i workerów przy konflikcie lub braku lokalnej kopii.

Manager ma korzystać z istniejących wierszy workerów i obecnego modelu wyboru
źródła, zamiast tworzyć drugi, rozbieżny mechanizm.

## Etap 3 — silnik batch

Wyodrębnić wielokrotnego użytku warstwę uruchamiania:

- `hs_crack_run_records` dla jednego zestawu rekordów,
- `hs_crack_batch_task` dla kolejki wybranych plików.

Przepływ dla pliku dostępnego tylko na Monsterze:

- `Copy & crack`,
- `Crack local only` — gdy istnieje lokalna kopia,
- `Cancel`.

Batch ma zapewniać:

- postęp całej kolejki i bieżącego pliku,
- anulowanie między plikami i podczas aktywnego zadania,
- użycie lokalnego workera oraz dostępnych workerów zdalnych,
- zapis wyniku po każdym pliku,
- brak równoległego uruchomienia dwóch zadań crackera.

Blokada transportu powinna obejmować tylko operacje, które rzeczywiście korzystają
z konsoli (kopiowanie, komendy workera, `save_pass`). Długie liczenie CPU nie może
bez potrzeby blokować pozostałych ekranów komunikacyjnych.

## Etap 4 — odbiór całości

- Test pojedynczego pliku ze starego przycisku `Crack`.
- Test `Crack latest`.
- Test wielu zaznaczonych plików z managera.
- Test pliku lokalnego, zdalnego i dostępnego w obu miejscach.
- Test `Resume`, `Force re-run`, znalezienia hasła, braku hasła i anulowania.
- Test restartu Tab5 i Monsterów z zachowaniem stanu i cache.
- Test Grove, USB i M-BUS osobno oraz razem.

## Ustalenia zachowane

- Nie usuwamy szybkich dotychczasowych wejść do crackowania.
- Manager jest warstwą nad wspólnym silnikiem, nie osobną implementacją crackera.
- Cache identyfikujemy rozmiarem i CRC, a zaufanie daje trwały marker weryfikacji.
- WPA2/PMKID pozostaje bieżącym zakresem. SAE/WPA3 nie wchodzi do tej fazy.
