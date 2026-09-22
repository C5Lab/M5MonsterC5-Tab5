# Handshake Cracker — dalsza mapa prac

Stan zapisany: 2026-09-22

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

`WPA PSK Auditor` jest dostępny jako osobny kafel w zakładce INTERNAL.
Dashboard przechowuje niezależny checkpoint A/B dla każdej sesji i pokazuje do
ośmiu ostatnio aktualizowanych, resumowalnych audytów wraz z capture, wordlistą,
bezpiecznym postępem, liczbą prób, workerami i ETA. Nowy audyt nie usuwa
poprzednich; `Resume` wybiera dokładny `session_id`, a `Start over` po
potwierdzeniu wycofuje tylko wskazaną sesję. Stary globalny checkpoint A/B jest
nadal odczytywany. Oba flow korzystają z tego samego launchera i silnika.
Widok pokazuje również stan źródeł, ostatnie wpisy historii i połączony katalog
LOCAL/GROVE/USB/M-BUS. Skan jest serializowany per transport, używa `ARTIFACT/1`
z fallbackiem `list_dir -s`, pokazuje badge źródeł oraz akcje `Audit` i
`Sync to Tab5`. Sync korzysta ze wspólnego resumowalnego transferu; automatyczna
walidacja lokalnej kopii po zakończeniu synchronizacji pozostaje do domknięcia.
Katalog ma również `Sync all to Tab5`: buduje ograniczoną kolejkę brakujących
capture'ów ze wszystkich dostępnych workerów, pomija wpisy niepoprawne oraz już
lokalne/zaindeksowane, kopiuje kolejno z zachowaniem `.part` i resume, a po
udanym batchu automatycznie odświeża widok. Dokładne zdalne duplikaty są scalane
po rozmiarze i CRC32. Gdy starszy/fallbackowy wpis nie ma fingerprintu, batch
ostrożnie scala zgodną nazwę, rozmiar i format, kopiuje tylko jedną kanoniczną
sztukę, a wszystkie lokalizacje workerów zapisuje jako aliasy w indeksie sync.
Różne znane CRC nigdy nie są scalane, nawet przy tej samej nazwie.
Osobna wysoka sekcja źródeł została zastąpiona kompaktowym dropdownem w nagłówku
katalogu. Pokazuje wyłącznie dostępne LOCAL/GROVE/USB/M-BUS, filtruje wiersze i
ogranicza batch sync do wybranego źródła; `All sources` zachowuje widok zbiorczy.
Katalog startuje zwinięty jako `Workers (N) - tap to browse`, więc długa lista
nie rozciąga dashboardu dopóki operator nie wybierze workera albo świadomie
nie przełączy się na `All sources`.

## Punkt wznowienia na następną sesję

Ostatnia zakończona implementacja to wielosesyjny Resume oraz pierwszy zbiorczy
katalog w `WPA PSK Auditor`. Kod i kontrakty hostowe są gotowe, ale firmware nie
został przez agenta kompilowany ani sprawdzony na urządzeniu. Następną sesję
zaczynamy od:

0. [Zrobione, czeka na test sprzętowy] Arbitraż konsoli podczas skanu katalogu.
   Grove, USB i M-BUS mają osobne mutexy własności konsoli współdzielone przez
   odświeżanie metadanych, stare listy plików, transfery i nowy katalog. Skan
   źródeł odbywa się kolejno i publikuje do UI dopiero kompletny snapshot.

1. Skompilowania i wgrania Tab5 przez operatora. JanOS pozostaje `1.7.5`; obecny
   build zawiera już wymagane `artifact_inventory=1`.
2. Testu sprzętowego dwóch sesji: uruchom A -> Cancel, uruchom B -> Cancel,
   sprawdź dwa wiersze w kaflu, Resume A -> Cancel, następnie Resume B.
3. Testu izolacji: `Start over` albo ukończenie A nie może usunąć B. Po restarcie
   Tab5 oba nieukończone checkpointy muszą nadal istnieć.
4. Testu starego flow `Compromised Data -> Handshakes -> Crack`: ma wybrać
   najnowszy dokładnie pasujący capture+wordlist, również gdy checkpoint nie
   mieści się w ośmiu wierszach dashboardu.
5. Zebrania logów `RESUME session=...`, bezpiecznych offsetów i listy katalogów
   `/sdcard/lab/handshakes/.crack_audit/active/` przed i po `Start over`.

Po odbiorze multi-resume i katalogu kolejność dalszych prac:

1. Test sprzętowy `Scan all sources`: jeden snapshot ma zawierać wpisy z
   LOCAL/GROVE/USB/M-BUS bez odpowiedzi `/lab` lub `/vendors` przypisanych do
   listy handshake'ów.
2. Domknięcie Task 6: po `Sync to Tab5` uruchomić lokalną walidację kopii,
   zapisać raport i dokładny fingerprint, a następnie scalić pewne duplikaty.
3. Rozbudowa kafla o manager wielu handshake'ów: filtrowanie, wybór wielu
   pozycji, stan i historia każdego audytu.
4. Silnik batch oraz kolejka crackowania, dopiero gdy katalog i synchronizacja
   przejdą odbiór sprzętowy.
5. Naprawa cytowania/formatowania `crack_state.csv` oraz końcowa macierz
   regresji Grove/USB/M-BUS.

Pozostałe ograniczenie testowe: `hs_session_catalog_test.c` przeszedł ścisłe
sprawdzenie składni kompilatorem RISC-V, ale natywny test runtime z ASan/UBSan
czeka na dostępny hostowy kompilator. Nie blokuje to kompilacji wykonywanej przez
operatora, lecz pozostaje bramką przed finalnym zamknięciem etapu.

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

Dwa wejścia korzystają z tego samego modelu sesji i silnika:

- istniejąca akcja `Crack` przy pliku w `Compromised Data -> Handshakes`,
- kafel `WPA PSK Auditor` w zakładce INTERNAL.

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
