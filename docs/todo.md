# Wardrive 2.0 — integracja w Tab5

Bazuje na commicie janOS / projectZero:
`3b4d6f4144c7357e6b855d255bb1bfb3e2073c8a` — *"Wardrive 2.0: band/channel selection, RSSI re-log, eviction, opsec, anti-surveillance"*
(lokalnie: `C:\Users\mati\Documents\GitHub\projectZero\ESP32C5`)

Cel: rozbudować kafel/widok **Wardrive** w Tab5 (`main/main.c`) o nowe opcje konfiguracyjne firmware'u — przycisk **Setup** i nowy widok ustawień, plus tryb **Anti-Surveillance**.

---

## 1. Rozpoznanie — co przyszło w Wardrive 2.0

Firmware (janOS na ESP32-C5) dostał blok konfiguracji wardrive trzymany w **NVS** (przeżywa reboot). Wzorzec działania: *skonfiguruj raz → `start_wardrive_promisc`*. Engine czyta config przy starcie.

### Nowe komendy CLI (UART 115200 8N1, `\r\n`)

| Komenda | Składnia | Opis |
|---|---|---|
| `get_wardrive_config` | — | Wypisuje aktywny config, format maszynowy `[WDCFG] ...`, zakończony `[WDCFG] END` |
| `set_wardrive_bands` | `<wifi24\|wifi5\|ble>[,...]` | Wybór radia. Same `ble` = tryb BLE-only (bez hoppingu). Domyślnie wszystkie |
| `set_wardrive_channels` | `<popular\|all\|custom> [c1:c2:...]` | `popular` = 1/6/11 + 5 GHz non-DFS; `all` = wszystko z DFS (domyślne); `custom` = lista po `:` |
| `set_wardrive_rssi_delta` | `<wifi\|ble> <0-50>` | Próg re-logowania w dBm. `0` = log once (legacy). Domyślnie wifi=5, ble=15 |
| `set_wardrive_memcap` | `<1000-200000>` | Limit wpisów WiFi w RAM zanim wyparowują najstarsze już-zapisane. Domyślnie 40000 |
| `set_wardrive_cooldown` | `<0-600>` | Odrzuca skany przez pierwsze N sek. po fixie GPS (nie loguj domu). `0` = off |
| `wardrive_blacklist` | `<add\|remove\|list\|clear> [MAC]` | Blacklista MAC (max 64) w NVS. Wykluczane z wyników/eksportu/anti-surv. `list` kończy się `Blacklist END` |
| `start_antisurveillance` | — | Detektor "ogona" BLE (urządzenie poruszające się z tobą). Wymaga GPS + ruchu. Alert: `[FOLLOWER] MAC=... name=... type=... rssi=... seen=...s travel=...m` |
| `set_antisurv_sensitivity` | `<low\|med\|high>` | Czułość detekcji ogona (patrz tabela niżej). NVS |

### Format `get_wardrive_config`
```
[WDCFG] bands=wifi24,wifi5,ble
[WDCFG] channels=popular
[WDCFG] custom=
[WDCFG] wifi_rssi_delta=5
[WDCFG] ble_rssi_delta=15
[WDCFG] startup_cooldown=0
[WDCFG] mem_cap=40000
[WDCFG] antisurv_sensitivity=med
[WDCFG] END
```

### Czułość Anti-Surveillance
| Poziom | Min czas | Min dystans | Losowe MAC-i |
|---|---|---|---|
| low  | 300 s | 1000 m | wykluczone (tylko stabilne) |
| med  | 180 s | 500 m  | wykluczone |
| high | 120 s | 300 m  | włączone |

### Mechanika engine'u (do tooltipów/UI)
- **Bands** → mapuje na ESP-IDF band-mode (2.4-only / 5-only / auto); po `stop` wraca do auto.
- **Re-log** → sieć przepisywana do CSV gdy RSSI zmieni się o ≥ delta LUB po przejechaniu dystansu (trilateracja WiGLE). `0` = log once.
- **Memcap** → eviction najstarszych już-zapisanych (bezpieczne, są na SD) → drive bez limitu.
- **Cooldown** → liczony PO fixie GPS.
- **Status periodyczny** (do parsowania w monitor task):
  `Wardrive promisc: 58 unique networks, 48 BT devices, 12 relogs, D-UCB best ch: 1 (34 visits), GPS: valid, sats: 5, dist: 1240.0m`
  → **nowość: `relogs` i `D-UCB best ch`** względem starego formatu.
- Plik wyjściowy: `/sdcard/lab/wardrives/wN.log`; trace KML: `wN_track.kml`.

> Pełny manual: `projectZero/ESP32C5/docs/command-manual.md` (328 linii).

---

## 2. Stan obecny w Tab5 (`main/main.c`, ~31k linii)

Widok wardrive: `show_wardrive_page()` @ `main/main.c:20199`. Layout (flex column):
- **Header**: Back + tytuł "Wardrive" | przyciski: Start, Stop, Trace, GPS, Upload
- **Status row**: label statusu + `WiFi: x  BT: x  SAT: x  x.xx km`
- **Tabela** (scroll) ostatnich sieci (`WARDRIVE_MAX_NETWORKS 100`, wyświetla 30)

Istotne punkty kodu:
- Struktura kontekstu: `tab_context_t` pola `wardrive_*` @ `main/main.c:516-587`
- Start: `wardrive_start_cb()` @ `20095` — wysyła `start_wardrive_promisc[_trace]` przez `uart_send_command()` / `uart2_send_command()` (TAB_MBUS)
- Wzorzec "wyślij komendę + czytaj odpowiedź": `wardrive_send_gps_set_command()` @ `16956` (poll `transport_read_bytes_tab`, parsowanie po `\r\n`) — **wzorzec do `get_wardrive_config`**
- Overlay wyboru (model dla popupu Setup): `wardrive_gps_type_btn_cb()` @ `17059` (overlay budowany na `wardrive_page`)
- Monitor task parsujący status: `wardrive_monitor_task` (parsuje `WiFi/BT/SAT/dist`) — do rozszerzenia o `relogs`/`best ch`
- Kafel na ekranie głównym: `create_tile(..., "Wardrive", ...)` @ `12045`; routing kliknięcia @ `6496`

Komunikacja: brak gotowego helpera "set + ACK" poza GPS — re-use wzorca z `wardrive_send_gps_set_command`.

---

## 3. Plan implementacji (do konsultacji z @mati)

### A. Widok Wardrive — przycisk **Setup**
- [ ] Dodać przycisk `Setup` (ikona koła zębatego / `LV_SYMBOL_SETTINGS`) do `btn_cont` w nagłówku @ `show_wardrive_page` ~`20266`.
- [ ] Disable przy `wardrive_monitoring` (jak Trace/GPS/Upload — config czytany tylko przy starcie).
- [ ] Callback `wardrive_setup_btn_cb` → otwiera nowy widok/overlay ustawień.

### B. Nowy widok ustawień Wardrive (pełny ekran lub overlay)
Sekcje (każda mapuje na komendę `set_*`):
- [ ] **Bands** — 3 toggle/checkboxy: 2.4 GHz / 5 GHz / BLE → `set_wardrive_bands wifi24,wifi5,ble`. Walidacja: min. 1 wybrane.
- [ ] **Channels** — segmented `popular / all / custom`; przy `custom` pole tekstowe `1:6:11:36:149` → `set_wardrive_channels ...`.
- [ ] **RSSI delta** — dwa slidery/steppery 0–50: WiFi i BLE → `set_wardrive_rssi_delta wifi N`, `... ble N`. Hint "0 = log once".
- [ ] **Memcap** — stepper/slider 1000–200000 (krok np. 5000) → `set_wardrive_memcap N`.
- [ ] **Startup cooldown** — slider 0–600 s → `set_wardrive_cooldown N`.
- [ ] **Anti-surv sensitivity** — segmented `low/med/high` → `set_antisurv_sensitivity X`.
- [ ] **Blacklist** — sub-lista MAC (add/remove/clear) → `wardrive_blacklist ...`; render z `list` (parsuj do `Blacklist END`).
- [ ] Przycisk **Load current** → `get_wardrive_config`, parsuj `[WDCFG] ...` do `[WDCFG] END`, ustaw kontrolki. Wywołać przy otwarciu widoku.
- [ ] Przycisk **Apply / Save** → wyślij komendy `set_*` po kolei (z delayem jak `vTaskDelay(100)` między nimi); pokaż ACK.
- [ ] Stan trzymać w nowej strukturze np. `wardrive_config_t` w `tab_context_t` (pola: bands maska, channel_mode, custom[], wifi_delta, ble_delta, memcap, cooldown, antisurv).

### C. Tryb Anti-Surveillance
- [ ] Decyzja: osobny kafel/widok czy przełącznik w widoku Wardrive? (rekomendacja: przycisk/zakładka w widoku Wardrive — współdzieli GPS/BLE i blacklistę).
- [ ] Start/Stop → `start_antisurveillance` / `stop`.
- [ ] Parser linii `[FOLLOWER] ...` → lista alertów + wyróżnienie (czerwony, `! FOLLOWER !`).
- [ ] Czułość ustawiana z widoku Setup (sekcja B).

### D. Status / monitor
- [ ] Rozszerzyć `wardrive_monitor_task` o parsowanie `relogs` i `D-UCB best ch` z linii statusu.
- [ ] Pokazać `relogs` i `best ch` w status row (np. `WiFi: x  BT: x  relog: x  ch: x  SAT: x  x.xx km`).

### E. Helper komunikacji
- [ ] Wydzielić generyczny `wardrive_send_setn_command(ctx, cmd, ack_prefix)` na bazie `wardrive_send_gps_set_command` (poll + parsowanie ACK) — re-use dla wszystkich `set_*` i `get_wardrive_config`.

### Decyzje (ustalone z @mati 2026-06-13)
1. **Anti-Surveillance → osobny kafel** na ekranie głównym + własny widok (`show_antisurv_page`). Parser `[FOLLOWER]` na żywo; „Devices seen" z linii podsumowania po Stop (firmware nie podaje na żywo po UART — main.c:6436 tylko OLED). Czułość ustawiana w widoku (cykl low/med/high) + z Setupu.
2. **Widok Setup → overlay/popup** budowany na `wardrive_page` (wzorzec jak `wardrive_gps_type_btn_cb`).
3. **~~Oba tryby~~ → tylko promisc 2.0.** Classic `start_wardrive` USUNIĘTY z UI — firmware nie emituje promisc-status (main.c:14695), więc liczniki/tabela by nie działały. Toggle nie istnieje.
4. **Blacklist → pełna edycja** — lista MAC z add/remove/clear, parsowanie `wardrive_blacklist list` do `Blacklist END`. Remove deferowany przez `lv_async_call` (usuwa swój własny wiersz w trakcie eventu).
5. **Apply → worker-task + spinner.** 7 komend `set_*` z czekaniem na ACK leci w osobnym tasku (`wardrive_apply_task`), spinner się animuje, przyciski zablokowane na czas Apply. Status „Applying N/7: ...".
6. **Mutual-exclusion Wardrive ↔ Anti-Surv** — start jednego zablokowany, gdy drugie działa (wspólny UART + jedna operacja na urządzeniu).

### Status implementacji (w `main/main.c`)
- [x] `wardrive_config_t` + pola w `tab_context_t`, defaults
- [x] `wardrive_send_set_command` (send+ACK poll), `wardrive_load_config` (`[WDCFG]` parser)
- [x] Setup overlay: bands (3 checkboxy) / channels (dropdown+custom textarea) / WiFi+BLE RSSI delta (slidery) / cooldown (slider) / memcap (dropdown presetów) / anti-surv sensitivity (dropdown) / Load / Apply(task+spinner) / Close
- [x] Edytor blacklisty MAC (add/remove/clear/list, własny overlay + klawiatura)
- [x] Przycisk Setup w nagłówku Wardrive (disabled w trakcie skanu)
- [x] Kafel „Anti-Surv" + `show_antisurv_page` + `antisurv_monitor_task` (parser `[FOLLOWER]`, podsumowanie po Stop)
- [x] Monitor task: parsowanie `relogs` i `D-UCB best ch`, rozszerzony status label
- [ ] **Kompilacja `idf.py build`** — niezrobione (brak zlokalizowanego env ESP-IDF; do uruchomienia po stronie @mati lub po wskazaniu ścieżki export.ps1)
