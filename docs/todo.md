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

---

# Mesh Recon — integracja 802.15.4 w Tab5

Bazuje na implementacji janOS / projectZero:
`C:\Users\mati\Documents\GitHub\projectZero\ESP32C5\docs\zig.md`

Cel: dodać do Tab5 widok dla pasywnego skanowania IEEE 802.15.4 z ESP32-C5: Zigbee, Thread i ostrożne `Matter/Thread?`. Firmware już wystawia komendy i stabilny format `[ZIG] ... [ZIG] END`.

## 1. Proponowany UX

Decyzja namingowa: kafel użytkowy **Mesh Recon**. `IoT` jest poprawne jako parasol, ale za szerokie; tutaj skanujemy 802.15.4 mesh/low-power: Zigbee, Thread i ostrożnie Matter-over-Thread.

Rekomendacja: dodać kafel **Mesh Recon** na głównym ekranie tab5 dla zewnętrznych JanOS tabów (`Grove`, `USB`, `MBus`). Nie robić nowej dolnej zakładki. Techniczny podtytuł ekranu: `802.15.4 / Zigbee / Thread`.

MVP otwiera od razu ekran **Mesh Recon**. Jeśli później dojdą kolejne narzędzia, kafel może prowadzić do menu:
- `Mesh Recon`
- przyszłe: BLE IoT, Matter helpers, Zigbee export

Ekran `Mesh Recon`:
- header: Back, tytuł `Mesh Recon`, podtytuł/mały label `802.15.4 / Zigbee / Thread`, przyciski `Start/Stop`, `Clear`, opcjonalnie `All/Ch25`.
- status strip: `Channel`, `Packets`, `Networks`, `Dropped`, aktywny/idle.
- lista PAN: karty `PAN: 0x1A62`, badge `Zigbee?` / `Thread?` / `Matter/Thread?` / `802.15.4`, `nodes | packets | ch | RSSI | age`.
- rozwiniecie PAN: lista node'ów i prosta topologia bez fałszywych krawędzi.
- `PAN 0xFFFF` / `kind=broadcast` ukryty domyślnie; widoczny tylko w trybie debug/show broadcast.

## 2. Kontrakt komend JanOS

Komendy start/stop:
```
start_zig_recon
start_zig_recon all 250
start_zig_recon 25 500
stop
zig_recon_clear
```

Komendy pollingu:
```
zig_recon_status
zig_recon_list
zig_recon_list all
zig_recon_nodes all
```

Format maszynowy:
```
[ZIG] status active=1 channel=11 packets=55 pans=4 nodes=6 dropped=0 dwell_ms=250 channels=0x07fff800
[ZIG] pan id=0x1A62 kind=network proto=zigbee confidence=probable channels=0x00000800 nodes=2 packets=22 best_rssi=-46 last_rssi=-46 last_seen_ms=34416 age_ms=2000
[ZIG] node pan=0x1A62 addr_type=short short=0x0000 ext=na role=coordinator packets=18 last_rssi=-58 last_seen_ms=30187 age_ms=2000
[ZIG] END
```

Rozszerzony kontrakt obsługiwany przez Tab5, do dołożenia po stronie JanOS:
```
[ZIG] node pan=0x1A62 addr_type=short short=0x13D7 ext=na role=unknown packets=2 last_rssi=-66 best_rssi=-61 avg_rssi=-64 lqi=172 sample_count=2 last_channel=11 vendor=na device_hint=na battery=na last_seen_ms=... age_ms=...
[ZIG] edge pan=0x1A62 from=0x0000 to=0x13D7 kind=observed packets=5 rssi=-66 age_ms=...
```

Zasady pól:
- `last_rssi`, `best_rssi`, `avg_rssi` to sygnał, nie dystans w metrach.
- `lqi` pokazać tylko jeśli driver realnie raportuje Link Quality Indicator.
- `sample_count` i `last_channel` pomagają ocenić wiarygodność RSSI i pokazać kanał ostatniej obserwacji.
- `vendor` tylko z EUI-64/OUI albo znanego fingerprintu; dla short-only node zostaje `na`.
- `battery` tylko jeśli realnie złapane z payloadu/clusterów; nie zgadywać.
- `edge kind=observed` rysować przerywaną linią, `kind=confirmed/parent` może być linią mocniejszą.

Ważne zasady parsera:
- czytać tylko linie `[ZIG]`, tekst ludzki ignorować.
- request kończy się na `[ZIG] END`.
- `last_seen_ms` to uptime firmware; UI używa `age_ms`.
- `kind=broadcast` nie jest normalną siecią.
- `short=na` oznacza extended-only node; użyć `ext` jako adresu/klucza.
- `proto=matter_thread` pokazać jako `Matter/Thread?`, nigdy jako potwierdzone Matter.

## 3. Stan obecny w Tab5 (`main/main.c`)

Istotne punkty:
- `tab_context_t` trzyma per-tab stan ekranów i tasków, okolice `main/main.c` ~`340-900`.
- `hide_all_pages()` chowa znane strony, okolice ~`1300`.
- deklaracje stron są przy forward declarations, okolice ~`1868-2215`.
- kafle główne zewnętrznych tabów są tworzone w `create_main_tiles()`, okolice ~`12170`; tam są `Bluetooth`, `Wardrive`, `Anti-Surv`, `Sub-GHz`.
- routing kafli jest w `main_tile_event_cb()`, okolice ~`6589-6614`.
- wzorzec ekran + UART worker:
  - `show_wardrive_page()` okolice ~`21615`
  - `wardrive_monitor_task()` i start/stop callbacki okolice ~`20060-20340`
  - `show_bluetooth_menu_page()` okolice ~`27771`
  - `show_ap_radar_page()` okolice ~`29131`
- istnieje helper zbierania odpowiedzi UART `home_collect_uart_response()` okolice ~`2432`; dla IoT/Zig Recon lepiej zrobić osobny worker/polling task, żeby nie mieszać odpowiedzi `[ZIG]` z innymi monitorami.

## 4. Modele danych do dodać

Proponowane limity MVP:
- `ZIG_UI_MAX_PANS 32`
- `ZIG_UI_MAX_NODES 128`

Struktury:
- [ ] `zig_ui_status_t`: `active`, `channel`, `packets`, `pans`, `nodes`, `dropped`, `dwell_ms`, `channel_mask`.
- [ ] `zig_ui_pan_t`: `id`, `kind`, `proto`, `confidence`, `channel_mask`, `channels_text`, `nodes`, `packets`, `best_rssi`, `last_rssi`, `age_ms`, `expanded`.
- [ ] `zig_ui_node_t`: `pan_id`, `addr_type`, `short_text`, `ext_text`, `role`, `packets`, `last_rssi`, `age_ms`.

Klucze cache:
- PAN: `id`.
- node short: `pan_id + short`.
- node ext: `pan_id + ext`.

## 5. Plan implementacji

### A. Kafel i routing
- [x] Dodać forward declaration `show_zig_recon_page()`.
- [x] Dodać pola `iot_recon_*` w `tab_context_t`: page, labels, list container, start/stop/clear btn, task handle, `volatile bool iot_recon_monitoring`.
- [x] Dodać cache PAN/node/edge w `tab_context_t` jako wskaźniki alokowane z PSRAM/heap, nie duże tablice na stacku/kontekście.
- [x] Rozszerzyć `hide_all_pages()` o `ctx->iot_page`.
- [x] Dodać kafel w `create_main_tiles()` obok `Bluetooth`/`Wardrive`: `Mesh Recon`.
- [x] Dodać routing w `main_tile_event_cb()`: wewnętrzny event/user_data `"IoT"` → `show_zig_recon_page()`; widoczna nazwa kafla to `Mesh Recon`.
- [x] Jeżeli ekran ma być tylko dla zewnętrznego JanOS, ukryć/disable kafel na `TAB_INTERNAL`.

### B. Parser `[ZIG]`
- [x] Fala 3: dodać proste helpery KV `key=value` dla linii `[ZIG]`.
- [x] Fala 3: parser/render `status`: `active`, `channel`, `packets`, `pans`, `dropped`.
- [x] Fala 3: parser/render `pan`: `id`, `kind`, `proto`, `channels`, `nodes`, `packets`, `best_rssi`, `last_rssi`, `age_ms`.
- [x] Fala 4: Parser/model `node`: `pan`, `addr_type`, `short`, `ext`, `role`, `packets`, `last_rssi`, `age_ms` do cache i rozwijanej listy.
- [x] Formatować kanały z maski, np. `0x02100800` → `11,20,25`.
- [x] Formatować `age_ms` → `Xs`, `Xm`, `Xh`.

### C. Komunikacja i polling
- [x] Fala 2: podpiąć realne TX z GUI dla `Start`/`Stop`/`Clear` przez aktywny transport taba (`Grove`/`USB`/`MBus`) z logami `[IoT][transport] TX`.
- [x] Fala 3: dodać `iot_recon_send_command_collect(ctx, cmd, out, out_size, timeout_ms)` na bazie `transport_write_bytes_tab()` + `transport_read_bytes_tab()`, kończące na `[ZIG] END`.
- [x] Fala 3: `Start`: wyczyścić UI, ustawić `monitoring=true`, uruchomić `iot_recon_monitor_task`.
- [x] Fala 3: `Stop`: `monitoring=false`, zostawić wyniki na ekranie.
- [x] Fala 3: `Clear`: wysłać `zig_recon_clear`, wyczyścić lokalną listę i metryki.
- [x] Fala 3: Polling active: `zig_recon_status` co ok. 1 s, `zig_recon_list all` + `zig_recon_nodes all` co ok. 2 s.
- [ ] Polling idle: nie spamować UART; odświeżać przy wejściu w ekran i po Clear/Stop.
- [ ] Obsłużyć `FAILED: radio busy (...)`: status czerwony, komunikat i przycisk `Stop current operation`.

### D. Widok
- [x] Header: Back, `Mesh Recon`, mały podtytuł `802.15.4 / Zigbee / Thread`, Start/Stop, Clear, opcjonalnie debug toggle `All`.
- [x] Status strip jako rząd małych paneli: Channel, Packets, Networks, Dropped/Status.
- [x] Lista PAN jako scroll container z kartami.
- [x] Fala 3: prosta karta PAN:
  - tytuł `PAN: 0x1A62`
  - badge protokołu według `proto/confidence`
  - linia szczegółów: `2 nodes | 22 pkts | ch 11 | -46 dBm | 2s`
- [x] Fala 4: klik PAN rozwija node listę.
- [x] Node list MVP:
  - `0x0000 Coordinator 18 pkts -58 dBm 2s`
  - dla `addr_type=ext`: pokazać skrócony extended address, np. `EXT ...A1B2`.
- [x] Fala 5 MVP: wizualizacja po kliknięciu PAN/karty sieci.
  - [x] Rozwinąć kartę PAN w miejscu.
  - [x] Sekcja `Topology`: rysunek node'ów jako punkty na panelu LVGL.
  - [x] Coordinator: pomarańczowy, większy punkt, etykieta `0000`.
  - [x] Router/known routing node: niebieski punkt.
  - [x] Unknown/end device: zielony punkt.
  - [x] Pod topologią lista node'ów jak u konkurencji: adres, rola, pakiety, RSSI, age.
  - [x] Pozycje node'ów deterministyczne z adresu zamiast slotów indeksowanych.
  - [x] RSSI wpływa orientacyjnie na promień od coordinatora.
  - [x] Delikatne przerywane linie od coordinatora jako `inferred layout`, bez udawania prawdziwych parent/child relacji.
  - [x] Linie są przycinane do promienia node'ów, żeby nie wchodziły pod kulki.
  - [x] Rozwinięta sieć zostaje w miejscu kliknięcia; UI robi tylko jednorazowy focus/scroll do widoku, bez zmiany kolejności listy.
  - [x] Polling zachowuje pozycję scrolla, żeby lista nie skakała przy odświeżaniu wyników.
  - [x] Klik node'a/kulki przypina panel `Locate`: PAN, addr, last/best/avg RSSI, LQI, sample count, channel, age i trend.
  - [x] Tryb `Track node` MVP: po kliknięciu node'a przypiąć go i odświeżać wskaźnik `last/best/avg RSSI`, trend oraz czas od ostatniego pakietu na podstawie pasywnego pollingu.
- [x] Topologia MVP: nie rysować twardych krawędzi; tylko punkty/role. Krawędzie dopiero gdy firmware dostarczy relacje albo oznaczyć je jawnie jako inferred.

### E. Testy ręczne / akceptacja
- [ ] Fixture z realnego logu Zigbee: `PAN 0x1A62`, node `0x0000 Coordinator`, node `0x96A5 Unknown`.
- [ ] Fixture Thread: `PAN 0x77E4` albo `0x2786`, `proto=thread confidence=probable`.
- [ ] Fixture broadcast: `PAN 0xFFFF kind=broadcast nodes=0` ukryty domyślnie.
- [ ] Fixture extended-only node: `[ZIG] node ... addr_type=ext short=na ext=0x...` renderuje `EXT`, nie `0xFFFF`.
- [ ] Fixture Matter: `proto=matter_thread confidence=probable` renderuje `Matter/Thread?`.
- [ ] MVP gotowe, gdy po realnym skanie tab5 pokazuje PAN-y i nody tak samo jak konsola JanOS, bez blokowania reszty UI.

## 7. JanOS backlog — dane recon do emisji

Tab5 jest już przygotowany na rozszerzony kontrakt `[ZIG]`, ale JanOS musi zacząć emitować dodatkowe pola. Zasada: jeśli wartość nie jest realnie znana, wysyłać `na` albo nie wysyłać pola. Nie zgadywać baterii, vendora ani prawdziwych relacji mesh.

### A. Rozszerzyć `zig_recon_nodes all`
- [x] Dodać `best_rssi=<int>`: najlepszy RSSI widziany dla node'a.
- [x] Dodać `avg_rssi=<int>`: uśredniony RSSI, np. prosty EMA albo średnia z okna.
- [x] Dodać `lqi=<0..255|na>` jeśli ESP-IDF/radio callback dostarcza LQI.
- [x] Dodać `sample_count=<n>` i `last_channel=<11..26|na>` dla wiarygodności i namierzania.
- [ ] Dodać `vendor=<name|na>` jeśli node ma EUI-64 i OUI/vendor DB pozwala rozpoznać producenta.
- [ ] Dodać `device_hint=<hint|na>` dla ostrożnych heurystyk, np. `coordinator`, `router`, `end_device`, `bulb?`, `sensor?`; tylko jeśli mamy podstawę w ramkach.
- [ ] Dodać `battery=<value|na>` tylko jeśli realnie złapane z payloadu/clusterów; w pasywnym skanie najczęściej będzie `na`.
- [ ] Dodać `addr_type=ext short=na ext=0x...` dla extended-only node'ów, żeby Tab5 mógł pokazać `EXT ...ABCD`.

Docelowy przykład:
```
[ZIG] node pan=0x1A62 addr_type=short short=0x13D7 ext=na role=unknown packets=2 last_rssi=-66 best_rssi=-61 avg_rssi=-64 lqi=172 sample_count=2 last_channel=11 vendor=IKEA device_hint=bulb? battery=na last_seen_ms=123456 age_ms=4000
```

### B. Dodać opcjonalne `[ZIG] edge`
- [ ] Emitować relacje ruchu jako `kind=observed`, gdy widzimy ramki source -> destination w obrębie PAN.
- [ ] Emitować relacje potwierdzone jako `kind=parent` albo `kind=confirmed` tylko gdy JanOS naprawdę rozpozna parent/child/neighbor z ramek zarządzających.
- [ ] Dodać liczniki `packets`, `rssi`/`last_rssi`, `age_ms`, żeby Tab5 mogło odświeżać grubość/ważność linku.
- [ ] Nie emitować fake edge'ów coordinator -> każdy node tylko dlatego, że istnieje coordinator. To jest tylko inferred layout po stronie Tab5.

Docelowy przykład:
```
[ZIG] edge pan=0x1A62 from=0x0000 to=0x13D7 kind=observed packets=5 rssi=-66 age_ms=4000
[ZIG] edge pan=0x1A62 from=0x0000 to=0x96A5 kind=parent packets=2 rssi=-59 age_ms=8000
```

### C. Ulepszyć rozpoznawanie protokołów
- [ ] `proto=zigbee confidence=probable|confirmed` na podstawie beacon/NWK/Zigbee-specific hints.
- [ ] `proto=thread confidence=probable|confirmed` dla Thread/MLE/Thread-specific hints.
- [ ] `proto=matter_thread confidence=probable` tylko jako ostrożna heurystyka, nie potwierdzony Matter.
- [ ] Zostawić `proto=ieee802154 confidence=unknown`, gdy nie mamy wyższej warstwy.

### D. Vendor/fingerprint
- [ ] Jeśli uda się zebrać `ext=0x...`, sprawdzać OUI/vendor prefix w istniejącej bazie vendorów albo osobnej małej tabeli IEEE OUI.
- [x] W Tab5 pokazywać vendor w wierszu node'a tylko gdy nie jest `na`.
- [x] W Tab5 pokazywać `device_hint` i `battery` w wierszu node'a tylko gdy nie są `na`.
- [x] W Tab5 pokazywać `last_channel`, `LQI`, `avg/best RSSI` i `sample_count` w kompaktowej linii szczegółów node'a.
- [ ] Dodać później opcjonalny szczegół node'a po kliknięciu: `PAN`, `short`, `ext`, `vendor`, `role`, `RSSI avg/best/last`, `LQI`, `last seen`.

### E. Co Tab5 ma jeszcze pokazać po rozszerzeniu JanOS
- [x] Node detail/locate panel po kliknięciu node'a: adres, role, pkts, best/avg/last RSSI, LQI, age, sample count i kanał.
- [ ] Ikonki/jasne etykiety sygnału: `Strong`, `Mid`, `Weak` plus RSSI.
- [x] Tracking node'a po RSSI: panel `Locate 0x13D7`, aktualny RSSI, `best`, `avg`, trend `closer/farther/steady`, `last seen`, LQI, sample count i kanał. To nie jest dystans w metrach, tylko pomoc do chodzenia z urządzeniem i szukania maksimum sygnału.
- [ ] Opcjonalny mini wykres ostatnich próbek RSSI/LQI dla przypiętego node'a, jeśli JanOS zacznie emitować sekwencję/okno próbek.
- [ ] Badge `Observed links` gdy przychodzą `[ZIG] edge`; bez edge zostaje `inferred layout`.
- [ ] Filtr widoku: `All`, `Zigbee`, `Thread`, `802.15.4`, opcjonalnie `Show broadcast`.
- [ ] Export snapshot do logu/SD: PAN-y, node'y, edges, timestamp.
- [ ] Dodać mały help tekst: `Lines are inferred unless marked observed/confirmed`.

### F. JanOS backlog — dane potrzebne do namierzania node'a
- [x] W `zig_recon_nodes all` utrzymywać `best_rssi`, `avg_rssi` i `last_rssi` per node, nie tylko ostatnią ramkę.
- [x] Dodać opcjonalny `sample_count=<n>` albo `seen_packets=<n>` dla oceny wiarygodności RSSI.
- [x] Dodać `last_channel=<11..26>` per node, jeśli node był widziany na konkretnym kanale.
- [x] Dodać `lqi=<0..255|na>` per node, jeśli radio/driver faktycznie to zwraca.
- [ ] Rozważyć komendę `zig_recon_track <pan> <short|ext>`: JanOS może wtedy szybciej odświeżać konkretny PAN/kanał albo emitować krótszy status tylko dla jednego node'a.
- [ ] Rozważyć preset `start_zig_recon <channel> <dwell>` z UI po wybraniu PAN, żeby po znalezieniu sieci zatrzymać hopping na jej kanale i uzyskać stabilniejszy RSSI do lokalizacji.

## 6. Otwarte decyzje przed kodem

- [x] Nazwa kafla: `Mesh Recon`.
- [ ] Czy MVP ma mieć wybór kanału/dwell? Rekomendacja: start `all/250`, a szybki preset `Ch25/500` dopiero w debug toggle.
- [ ] Czy kafel pokazywać na `TAB_INTERNAL`? Rekomendacja: nie, bo komendy są po stronie zewnętrznego JanOS/ESP32-C5.
- [ ] Czy robić osobne menu IoT już teraz? Rekomendacja: nie; kafel `Mesh Recon` otwiera od razu widok `Mesh Recon`.
