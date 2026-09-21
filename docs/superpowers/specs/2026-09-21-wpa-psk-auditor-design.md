# WPA PSK Auditor — projekt architektury

Stan: zatwierdzony 2026-09-21

## Cel

Zbudować jeden wspólny system audytu przechwyconych materiałów WPA/WPA2,
obsługiwany z dwóch miejsc:

1. szybkie wejście `Compromised Data -> Handshakes -> Crack/Audit`,
2. kafel główny `WPA PSK Auditor`, otwierający dashboard, katalog, aktywną
   sesję i historię.

Oba wejścia mają używać tego samego walidatora, trwałej sesji, schedulera
workerów, cache i zapisu wyników. Nie powstaje drugi cracker.

## Zakres pierwszego wydania

- Jeden aktywny lub resumowalny audyt naraz.
- Dowolna liczba zapisanych wpisów historii.
- Trwałe wznowienie po restarcie Tab5, również dla rozproszonego zadania.
- Katalog lokalnych i zdalnych plików z badge'ami źródeł.
- Maszynowa inwentaryzacja JanOS z zachowaniem kompatybilności ze starszymi
  firmware.
- Strukturalna walidacja wejścia przed utworzeniem sesji i przygotowaniem
  workerów.
- Niepoprawne pliki pozostają widoczne i nie są kasowane; mają stan oraz
  stabilny kod powodu, aby mogły później służyć do diagnostyki Handshakera.
- `Sync to Tab5` używa resume transferu, nie nadpisuje zweryfikowanej kopii i
  nie usuwa pliku źródłowego.

Pierwsze wydanie nie dodaje batcha ani wielu równocześnie zapauzowanych
audytów. PMKID może być rozpoznany w katalogu jako osobny typ, ale bieżący
silnik nie może oznaczyć go jako gotowego do audytu, dopóki jego weryfikator
nie zostanie zaimplementowany. WPA3/SAE pozostaje poza zakresem.

## Niezmienne reguły poprawności

- Walidacja materiału następuje przed zapisem aktywnej sesji, podziałem
  wordlisty i wysłaniem komendy start do workera.
- Awaria transportu oznacza `unavailable` lub `unknown`, nigdy `invalid`.
- `invalid` oznacza zakończoną analizę tych samych, pełnych bajtów.
- Żaden shard nie może zostać uznany za ukończony poza jego monotonicznym
  `safe_offset`.
- Po restarcie dopuszczalne jest ponowne sprawdzenie kandydatów, lecz nie luka.
- `not_found` jest wynikiem tylko wtedy, gdy wszystkie zakresy ledgeru są
  bezpiecznie ukończone.
- Wynik zdalny `found` jest ponownie weryfikowany lokalnie.
- Stary lub nieznany protokół inwentaryzacji nie blokuje istniejącego JanOS;
  Tab5 wraca do `list_dir -s` oraz lokalnej walidacji.

## Architektura Tab5

### 1. Analizator materiału

Obecna logika `hs_crack_build_records_from_pcap()` zostaje wydzielona do
modułu niezależnego od LVGL i transportu. Zamiast liczby lub ogólnego błędu
zwraca raport:

```c
typedef enum {
    HS_CAPTURE_READY,
    HS_CAPTURE_INVALID,
    HS_CAPTURE_UNSUPPORTED,
    HS_CAPTURE_UNAVAILABLE,
    HS_CAPTURE_CANCELLED,
} hs_capture_state_t;

typedef enum {
    HS_CAPTURE_REASON_OK,
    HS_CAPTURE_REASON_IO_ERROR,
    HS_CAPTURE_REASON_BAD_HEADER,
    HS_CAPTURE_REASON_TRUNCATED,
    HS_CAPTURE_REASON_UNSUPPORTED_LINKTYPE,
    HS_CAPTURE_REASON_NO_EAPOL,
    HS_CAPTURE_REASON_NO_AP_NONCE,
    HS_CAPTURE_REASON_NO_STA_RESPONSE,
    HS_CAPTURE_REASON_NO_MATCHING_PAIR,
    HS_CAPTURE_REASON_MISSING_SSID,
    HS_CAPTURE_REASON_UNSUPPORTED_KEYVER,
    HS_CAPTURE_REASON_MALFORMED_EAPOL,
    HS_CAPTURE_REASON_PMKID_UNSUPPORTED,
    HS_CAPTURE_REASON_SAE_UNSUPPORTED,
    HS_CAPTURE_REASON_LIMIT_REACHED,
} hs_capture_reason_t;

typedef struct {
    hs_capture_state_t state;
    hs_capture_reason_t reason;
    uint32_t packet_count;
    uint32_t eapol_count;
    uint32_t ap_nonce_count;
    uint32_t sta_response_count;
    uint32_t malformed_count;
    uint32_t unsupported_keyver_count;
    uint32_t record_count;
} hs_capture_report_t;
```

Raport jest cache'owany razem z pełną tożsamością pliku i wersją walidatora.
Zmiana rozmiaru, CRC lub wersji walidatora unieważnia wpis.

### 2. Katalog

Jeden `audit_capture_asset` reprezentuje materiał, który może mieć kilka
lokalizacji `LOCAL`, `GROVE`, `USB` i `MBUS`. Pełny rozmiar oraz CRC32 stanowią
bieżącą tożsamość zgodną z istniejącym transferem. Rekord zdalny bez pełnego
fingerprintu pozostaje prowizoryczny i nie jest automatycznie łączony tylko po
nazwie.

Stany katalogu:

- `Ready`,
- `Needs investigation`,
- `Inspecting`,
- `Unknown`,
- `Unavailable`,
- `Syncing`,
- `Possible duplicate`.

Niepoprawny plik jest widoczny z kodem i lokalizacją błędu. Automatyczny sync
nie kopiuje pliku, który zdalny walidator jednoznacznie oznaczył jako invalid;
szczegóły mogą później oferować `Copy anyway`. Plik `unknown` może zostać
skopiowany do Tab5 w celu ostatecznej analizy.

### 3. Trwała sesja

Nowy moduł `hs_crack_session` przechowuje aktywny stan w dwóch naprzemiennych
slotach:

```text
/sdcard/lab/handshakes/.crack_audit/active.a
/sdcard/lab/handshakes/.crack_audit/active.b
```

Każdy snapshot posiada magic, wersję schematu, numer sekwencji, identyfikator
sesji, długości, CRC nagłówka i payloadu oraz trailer. Nie zapisujemy surowego
obrazu struktury C.

Payload obejmuje:

- fingerprint i raport capture,
- źródło oraz ścieżkę lokalną/zdalną,
- manifest wordlist z pełnym CRC,
- bieżącą fazę oraz indeks listy,
- lokalny shard i wszystkie zdalne shardy,
- właściciela, poprzedniego właściciela, generację, job ID i retired job ID,
- monotoniczny `safe_offset` i rozliczone liczniki,
- stan recovery workerów,
- czas aktywnej pracy, ostatnią prędkość i ostatnie ETA,
- stan finalizacji oraz wynik.

Zapisywany jest starszy slot. Po zamknięciu pliku aktywny zostaje snapshot o
najwyższej poprawnej sekwencji. Przerwany zapis jednego slotu nie niszczy
poprzedniego.

Checkpoint powstaje przed wysłaniem zarezerwowanego job ID, po każdej zmianie
ledgeru, po potwierdzonym wzroście `safe_offset`, cyklicznie po bezpiecznym
opróżnieniu kolejki lokalnej oraz przed i po finalizacji.

### 4. Resume

Po starcie Tab5 aktywna sesja jest odczytywana, lecz nie wznawiana bez decyzji
użytkownika. Dashboard oraz szybkie wejście pokazują `Resume` i `Start over`.

Resume:

1. ponownie pobiera lub otwiera capture i sprawdza jego tożsamość,
2. sprawdza pełne tożsamości wymaganych wordlist,
3. odbudowuje scheduler z zapisanego ledgeru,
4. odpytuje zapisane job ID,
5. reattachuje działającą generację albo po `unknown_job` zleca tylko suffix od
   ostatniego potwierdzonego offsetu,
6. uruchamia lokalny shard od jego własnej bezpiecznej granicy,
7. przelicza ETA dopiero po uzyskaniu aktualnych prędkości.

Niezgodna tożsamość tworzy wpis historii `stale`; nie uruchamia workera.

### 5. Historia

Każdy zakończony epizod uruchomienia trafia do unikalnego, niezmiennego rekordu
w katalogu `history`. `Stop & preserve progress` zamyka epizod jako
`interrupted`, ale pozostawia aktywną sesję resumowalną; kolejne Resume tworzy
następny epizod z tym samym session ID i rosnącym numerem. Rekord zawiera daty
lub numer sekwencji, źródło, listy, skład i efektywną liczbę workerów, czasy,
rate, ostatnie ETA, liczbę wznowień, wynik i powód. Zwykły wiersz historii nie
pokazuje hasła; jego ujawnienie wymaga wejścia w szczegóły.

`crack_attempts.csv` pozostaje cache'em wyników. `crack_state.csv` pozostaje
projekcją kompatybilności i nie jest źródłem Resume ani historii.

## Dwa flow UI

### Szybki flow

`Compromised Data -> Handshakes -> Crack/Audit` otwiera wspólne szczegóły
wybranego capture. Najpierw następuje analiza. Materiał gotowy prowadzi do
konfiguracji lub zgodnego checkpointu Resume. Materiał niepoprawny pokazuje
powód i nie tworzy sesji.

### WPA PSK Auditor

Nowy kafel główny otwiera dashboard:

- karta aktywnego lub resumowalnego audytu,
- stan źródeł `LOCAL/GROVE/USB/MBUS`,
- podsumowanie katalogu,
- ostatnie wpisy historii,
- przejście do pełnego katalogu i historii.

Karta aktywna pokazuje capture, listę, postęp, czas, rate, ETA i istniejące
wiersze workerów. Operacja zatrzymania nazywa się `Stop & preserve progress`,
ponieważ zatrzymanie jest kooperacyjne, a nie natychmiastowe.

## Rozszerzenie JanOS

JanOS pozostaje w wersji rozwojowej `1.7.5` do czasu merge'a. Istniejące `CRACK/1 protocol=4` pozostaje bez
zmian. Dodawane jest niezależne capability `artifact_inventory=1` i protokół
`ARTIFACT/1`:

```text
artifact_inventory capabilities
artifact_inventory list <request-id> <scope> <cursor> <limit>
artifact_inventory inspect <request-id> <snapshot-id> <entry-id>
artifact_inventory cancel <request-id>
```

Scope jest enumem mapowanym na dozwolone katalogi, nie dowolną ścieżką. Nazwy
plików są kodowane szesnastkowo. Odpowiedzi mają `BEGIN`, stronicowane `ITEM`,
`RESULT` i dokładnie jeden terminalny `END` skorelowany przez request ID.

Lista nie liczy CRC wszystkich plików. `inspect` wykonuje ograniczoną,
anulowalną pracę, podaje postęp, pełne CRC i dostępny raport strukturalny.
Pierwsze wydanie zdalnie waliduje HCCAPX. Raw PCAP bez nowego parsera zgłasza
`validation=unknown reason=unsupported_validator`; Tab5 wykonuje ostateczną
analizę po syncu. Cache inwentaryzacji jest oddzielony od markerów transferu i
nigdy nie usuwa pliku źródłowego.

Starszy JanOS lub brak capability powoduje fallback do `list_dir -s`.

## Testy odbiorcze

### Host

- każdy kod walidacji ma niezależny fixture i stabilny wynik,
- transport failure nie staje się `invalid`,
- invalid capture nie tworzy sesji ani historii,
- round-trip maksymalnego snapshotu,
- wybór ostatniego poprawnego slotu przy uszkodzonym A lub B,
- restart na granicach reserve/start/progress/finalize,
- zmiana capture lub wordlisty blokuje Resume,
- odbudowa generacji, job ID i offsetów wszystkich shardów,
- parser `ARTIFACT/1` odrzuca przepełnienia, błędne hex i obce request ID,
- starszy JanOS przechodzi przez fallback,
- inwentaryzacja nie usuwa pliku oznaczonego jako invalid.

### Sprzęt

- restart Tab5 przy trzech aktywnych workerach,
- restart Tab5, gdy stary job nadal działa,
- restart workera i Tab5 w różnych momentach,
- zerwanie zasilania podczas zapisu snapshotu i finalizacji,
- invalid PCAP z obu flow,
- zdalny katalog przez Grove, USB i M-BUS,
- Sync to Tab5 z duplikatem, resume i zerwanym transferem,
- działanie ze starszym JanOS bez `ARTIFACT/1`.

## Granice bezpieczeństwa i wydajności

- Inwentaryzacja jest tylko do odczytu; usuwanie nie jest częścią protokołu.
- Jedno połączenie ma jednego właściciela konsoli; tekst nie przeplata się z
  transferem binarnym.
- Każdy parser ma ograniczenia długości linii, ścieżki, liczby elementów i pól.
- CRC32 służy kompatybilności i wykrywaniu przypadkowych zmian, nie jest
  kryptograficzną gwarancją tożsamości.
- Skany SD działają poniżej priorytetu konsoli, okresowo yield'ują i można je
  anulować.
