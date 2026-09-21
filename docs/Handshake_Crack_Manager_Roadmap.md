# Handshake Cracker — dalsza mapa prac

Stan zapisany: 2026-09-21

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

USB poprawnie negocjuje obecnie 921600 dla synchronizacji, używa bloków 1024 B
z ACK32/FIN32 i wraca do konsoli 115200. Pierwsze logi sprzętowe potwierdziły
skrócenie czasu odpowiedzi ACK, ale Etap 0 pozostaje otwarty do zakończenia
pełnej macierzy cold-cache, warm-cache, resume, multi-worker, fallback i cancel.

Plan odbioru: `docs/superpowers/plans/2026-09-21-stage-0-transport-closure.md`.

Macierz wyników: `docs/Handshake_Crack_Stage0_Acceptance.md`.

Zaimplementowano rozpoznanie nieterminalnego komunikatu `CANCELLING` oraz probe
cache na 115200 przed ewentualną negocjacją szybkiego baud. Log sprzętowy
potwierdził warm-cache bez zmiany prędkości dla Grove, USB i M-BUS. Kolejny
firmware dodaje jawne potwierdzenia każdego etapu Cancel i nie odtwarza dźwięku
ostrzeżenia po świadomym anulowaniu. Plan techniczny:
`docs/superpowers/plans/2026-09-21-cancelling-and-warm-cache-probe.md`.

Koordynator Tab5 ma również hostowo przetestowany ledger shardów i automatyczne
recovery utraconego workera. Po trzech brakujących STATUS zachowuje ostatni
potwierdzony `safe_offset`, okresowo wykonuje `ping`, odpytuje stary job,
reattachuje go po przejściowym zerwaniu albo po `unknown_job` przygotowuje
workera i uruchamia nową generację. Wolny Monster może przejąć niedokończony
suffix, a końcowy fallback lokalny konsumuje ten sam ledger. Odbiór sprzętowy
restartu Grove/USB/M-BUS pozostaje otwarty; plan i specyfikacja znajdują się w
`docs/superpowers/plans/2026-09-21-worker-auto-recovery.md` oraz
`docs/superpowers/specs/2026-09-21-worker-auto-recovery-design.md`.

## Etap 0 — stabilizacja transportów

- Potwierdzić dwa pełne transfery capture oraz dużej wordlisty przez USB.
- Zachować osobne traktowanie USB: konsola 115200, synchronizacja 921600,
  bloki 1024 B i potwierdzony powrót do 115200; przy błędzie bezpieczny resumowalny
  fallback 115200.
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
