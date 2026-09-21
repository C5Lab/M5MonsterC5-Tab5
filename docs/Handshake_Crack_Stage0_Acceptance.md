# Handshake Cracker — Stage 0 acceptance

Stan: 2026-09-21, etap otwarty.

Ten dokument jest bieżącym rejestrem odbioru transportów. `partial` oznacza,
że dostarczony log potwierdza część warunku, ale nie cały scenariusz. Etap 0
może zostać zamknięty dopiero po zaliczeniu wszystkich wierszy.

## Zamrożony profil

- JanOS: `1.7.5`, CRACK/1 protocol `4` (the protocol remains unchanged;
  `ARTIFACT/1` is an independent additive capability).
- USB CH34x: konsola `115200`, synchronizacja domyślnie `921600`, bloki
  `1024 B`, ACK32/FIN32, powrót do `115200`.
- Grove i M-BUS: synchronizacja `2000000`, bloki `8192 B`, odpowiedź
  jednobajtowa, powrót do `115200`.
- Przygotowanie i start: maksymalnie trzy próby na etap.
- Synchronizacja plików jest sekwencyjna; zakresy słownika są przetwarzane
  współbieżnie dopiero po przygotowaniu workerów.

Audyt źródeł:

- `JANOS_FT_USB_DEFAULT = 921600U` w `main/transfer_speed_config.h`.
- `hs_remote_receive_block_size()` zwraca `1024U` dla USB i `8192U` dla
  UART w `main/hs_crack_remote_core.c`.
- `HS_REMOTE_STAGE_ATTEMPTS = 3U` w `main/hs_crack_remote_core.h`.
- CH34x `0x1A86:0x7523`, revision `0x35`; rejestry z testu kodera:
  `115200 -> 0xCC83`, `921600 -> 0xF387`.

## Macierz akceptacyjna

| ID | Scenario | Required evidence | Result | Log reference | Notes |
|---|---|---|---|---|---|
| U1 | USB cold capture | 921600, READY 1024/ACK32, FIN, SYNCED, 115200 restore | pass | serial 2026-09-21, końcówka `1182780-1190346` | FIN/ACK32 i potwierdzony powrót CH34x do 115200. |
| U2 | USB cold 15.6 MB run A | complete without worker loss or fallback | pass | serial 2026-09-21, `blk=15276-15279`, następnie worker ready | Pełny rozmiar `15645263`, bez local fallback. |
| U3 | USB cold 15.6 MB run B | complete without worker loss or fallback | pass | serial 2026-09-21, attachment `cecddf6d`, FIN `887624-887817`, restore/start `895018-896894` | Pełny rozmiar `15645263`, końcowy ACK32/FIN, powrót do 115200, ACK zadania i brak fallbacku. |
| U4 | USB warm cache | both inputs present and no data blocks | pass | serial 2026-09-21, `37558-40682` | Capture i wordlist `present=1`; brak READY, bloków i zmiany baud. |
| U5 | USB interrupted transfer | non-zero offset and matching prefix CRC | partial | serial 2026-09-21, resume od `47104` | Potwierdzony niezerowy offset i kontynuacja; potrzebny pełny log z odpowiadającym `prefix_crc`. |
| G1 | Grove cold/warm/resume | 2M/8192, cache hit, non-zero resume | partial | serial 2026-09-21, warm cache `36718-37031`, `39589-40067` | Warm capture i wordlist zaliczone; cold oraz kontrolowany resume do powtórzenia. |
| M1 | M-BUS cold/warm/resume | 2M/8192, cache hit, non-zero resume | partial | serial 2026-09-21, warm cache `38572-39041`, `40862-41331` | Warm capture i wordlist zaliczone; cold oraz kontrolowany resume do powtórzenia. |
| D1 | All workers | disjoint coverage and advancing status | pass | serial 2026-09-21, attachments `cecddf6d` i `2f48fcde`; ASSIGN/ACK `896103-897317`, końcowe STATUS `3059120-3071194` | Grove, USB i M-BUS pracowały stabilnie ponad 36 min; `checked` i `safe_offset` rosły monotonicznie. |
| D2 | Lost worker | three misses, ping/status reconciliation and safe-offset reassignment | partial, firmware retest | serial 2026-09-21, attachment `2f48fcde`, `4055409-4143409` | USB: dokładnie trzy MISS, LOST z `safe_offset=7827453`; Grove i M-BUS kontynuowały. Ledger, auto-recovery, work stealing i local drain przechodzą testy hostowe; wymagany log sprzętowy nowego firmware. |
| D3 | Cancel | remote cancel and no completed notfound | pass | serial 2026-09-21, attachments `0d70d18e` i `227881a1`, cancel `224917-226973`, restart `327405-344028` | Wszystkie trzy joby: `requested -> CANCELLING -> confirmed via STATUS`; brak `notfound`, timeoutu i warning chime. Bez rebootu ponowny Crack uzyskał capabilities, ACK i STATUS z Grove, USB oraz M-BUS. |
| D4 | Cancel podczas synchronizacji raw | END/prompt drained, 115200 restore, worker ponownie wykryty bez rebootu, resume `.part` | pending retest | serial 2026-09-21, Grove utracony po Cancel podczas cold wordlist | Przyczyna potwierdzona: cancel-aware reader przerywał obowiązkowe oczekiwanie na END/prompt. Poprawka i test hostowy gotowe; wymagany retest sprzętowy. |
| R1 | Known password | local verification and exact persistence | pending | | Wymagana kontrolowana lista ze znanym hasłem. |

## Ostatnia weryfikacja warm-cache i Cancel

Warm-cache zaliczono dla wszystkich trzech transportów: każdy capture i wordlist
zwrócił `present=1`, po czym worker przeszedł bezpośrednio z `checking cache` do
`ready`. Nie wystąpiła negocjacja szybkiego baud ani wejście w transfer raw.

Cancel został potwierdzony sprzętowo dla wszystkich trzech zadań. Grove, USB i
M-BUS zwróciły skorelowane `CANCELLING`, a następnie terminalny `STATUS`;
Tab5 zalogował `CANCELLED ... confirmed via STATUS`. Nie wystąpiły `unparsed
CRACK line`, `cancel not confirmed`, zapis `notfound` ani warning chime. Po
zamknięciu popupu, bez rebootu, ponowne wybranie Crack uruchomiło nowe joby na
wszystkich trzech workerach i każdy zwrócił rosnący `STATUS`.

## Bramka hostowa 2026-09-21

Wykonano natywnie w WSL z GCC 14.2.0 i Pythonem 3.13.5. Firmware nie był
kompilowany.

Tab5:

- `test_usb_blocking_read_contract.py`: pass,
- `test_usb_ack32_upload_contract.py`: pass, 37 scenariuszy produkcyjnej pętli,
- `test_usb_cdc_transfer_config.py`: 4/4,
- `test_usb_fast_baud_contract.py`: pass,
- `test_fast_uart_sync_fallback_contract.py`: pass,
- `test_usb_probe_recovery_contract.py`: pass,
- `test_usb_ready_line_budget_contract.py`: pass,
- `test_usb_start_recovery_contract.py`: pass,
- `test_cancel_transfer_recovery_contract.py`: pass,
- `test_worker_stage_retry_contract.py`: 13/13,
- `test_worker_auto_recovery_contract.py`: pass,
- `test_worker_reassignment_contract.py`: 8 kontraktów i wykonany harness
  produkcyjnego ticka/reassignment,
- `usb_vcp_config_test.c`, `hs_crack_remote_core_test.c` oraz
  `transfer_speed_config_test.c` oraz `hs_crack_scheduler_test.c`: kompilacja
  `-Werror` i wykonanie zakończone kodem zero.

JanOS:

- `test_crack_worker_job_replay.py`: 5/5,
- `test_crack_worker_diagnostics.py`: 4/4,
- `crack_worker_core_test.c`: kompilacja `-Werror` i wykonanie zakończone kodem
  zero.

Pierwsze uruchomienie bramki wykryło martwy mock `hs_crack_remote_probe()` w
harnessie ACK32, pozostały po wydzieleniu probe z uploadu. Mock usunięto, a
następnie powtórzono całą bramkę z wynikiem zielonym.

## Następna partia odbioru

1. Przeprowadzić kontrolowany resume USB z pełnym logiem offsetu i prefix CRC.
2. Powtórzyć Cancel podczas cold sync Grove: oczekiwać bezpiecznego przerwania
   bloku, odebrania END/prompt, powrotu do 115200, ponownego wykrycia workera bez
   rebootu i resume od niezerowego offsetu przy następnym Crack.
3. Wgrać firmware z auto-recovery i zrestartować Grove podczas aktywnego joba.
   Oczekiwany ciąg: `LOST`, `RECOVERY_TICK`, `RECOVERY_ALIVE`, skorelowany
   `RECOVERY_STATUS`, następnie `REATTACH` albo `REASSIGN` od zapisanego
   `safe_offset`; rosnący STATUS nowego właściciela ma potwierdzić brak luki.
4. Powtórzyć scenariusz dla USB i M-BUS, a potem pozostawić wszystkie Monstery
   offline i potwierdzić ledger-driven local drain każdego suffixu.
