# PCAP na M5Stack Tab5 i JanOS — stan końcowy, analiza i rozszerzenia

> **Status implementacji ESPShark Offline v6:** **COMPLETE** — zakres opisany w sekcji 0 jest obecny w kodzie na gałęzi `development`.
> **Status transferu:** MITM PCAP i lokalna analiza MVP są potwierdzone sprzętowo.
> **Bramka wydania v6:** pełny build ESP-IDF 5.4.1 sprzed korekty FQDN zakończony sukcesem; po korekcie z sekcji 32 kompilację i test sprzętowy wykonuje właściciel projektu.
> **Ostatnia aktualizacja i audyt kodu:** 2026-08-11, `development` HEAD `1f2700f` oraz lokalna implementacja FQDN/cache v7, interaktywnego DNS drill-down i korekty wieloadresowych odpowiedzi DNS; ostatni bazowy commit ESPShark `1816750`.
> **Zakres dokumentu:** sekcja 0 jest kanonicznym bilansem funkcji, sekcja 29 opisuje bazę v5, sekcja 30 zmianę v6, sekcja 31 interaktywny DNS, sekcja 32 korektę FQDN, a sekcje 25–28 pokazują historię rozwoju; wcześniejsze plany i TODO nie zmieniają statusu końcowego.

## 0. Kanoniczny stan końcowy ESPShark Offline v6

Ta sekcja jest źródłem prawdy dla zakresu zaimplementowanego w firmware. Starsze
checklisty opisują drogę do v6 i nie mogą nadpisywać statusu z tej sekcji.

### 0.1. Zaimplementowane funkcje

- [x] Centralny hub `ESPShark`: odczyt capture'ów z Monstera, synchronizacja
  brakujących plików oraz otwieranie biblioteki z SD Tab5.
- [x] Strumieniowy reader classic-PCAP 2.4 z little/big endian, timestampami
  mikro-/nanosekundowymi, walidacją rekordów, zachowaniem poprawnego prefiksu
  uciętego pliku i czytelnym odrzuceniem PCAPNG.
- [x] Linktype 1 Ethernet, 105 IEEE 802.11, 230 IEEE 802.15.4 bez FCS oraz
  283 IEEE 802.15.4 TAP.
- [x] Dekodowanie i klasyfikacja Ethernet/VLAN, ARP, IPv4/IPv6, ICMP,
  TCP/UDP, DNS/mDNS, DHCP, HTTP, TLS, EAPOL, ramek zarządzających Wi-Fi oraz
  podstawowych ramek IEEE 802.15.4.
- [x] Biblioteka capture'ów, paginowana tabela pakietów, szczegóły nagłówków,
  podgląd HEX/ASCII i filtry protokołów.
- [x] Przełącznik `SHOW FQDN` w tabeli pakietów: Source/Destination pokazują
  nazwę z zaobserwowanego DNS nad zachowanym adresem IP i portem; brak
  mapowania automatycznie pozostawia pierwotny endpoint.
- [x] `OVERVIEW`, interaktywny `DNS`, dwukierunkowe `FLOWS`, `APPS` i quick
  filters. Top domeny otwiera klientów, rozwiązane adresy i skorelowane flow
  z akcjami `FILTER`, `FOLLOW` ASCII oraz `HEX`.
- [x] Inwentarz `LOCAL DEVICES` / `REMOTE ENDPOINTS`, scalanie lokalnych
  IPv4/IPv6 po MAC, zaobserwowane usługi i filtrowanie do dowodów.
- [x] `NETWORK HEALTH` z confidence i dowodami: port scan, host sweep,
  ARP conflict, DNS anomaly, beaconing, exfil candidate, cleartext services,
  worm-like spread, excessive broadcast, weak TLS i TCP quality.
- [x] `COMMUNICATION MAP` w trybach Traffic, Threats i Services, z limitami,
  grupowaniem LAN/WAN/multicast/broadcast i filtrowaniem wybranego węzła.
- [x] `OFFLINE INVESTIGATION`: Findings, Timeline, Device Dossier, risk score,
  lokalne IOC/rules, wykrywanie ekspozycji administracyjnej, cleartext auth,
  SMB1, MQTT cleartext, encrypted DNS i burstów deauthentication.
- [x] TLS ClientHello: SNI, ALPN, wersja, liczba cipherów/extensions oraz
  lokalny stabilny fingerprint `CH-FNV64`; metadane HTTP i BitTorrent.
- [x] Baseline `home`, bezpieczny zapis, porównanie urządzeń, endpointów,
  domen i usług oraz prezentacja różnic.
- [x] Cache analizy v7 z mapą `IP → FQDN` oraz atomowe eksporty: JSON v6,
  samodzielny HTML, filtered classic-PCAP i profil ostatniego filtra.
- [x] Ograniczone struktury w PSRAM i jawne flagi `INDEX LIMITED`, `LIMITED`,
  `TRUNCATED` lub `INCOMPLETE` zamiast ukrywania niepełnego wyniku.

### 0.2. Kryterium ukończenia

Zakres implementacyjny ESPShark Offline v6 jest zamknięty. Funkcja jest
`IMPLEMENTED`, gdy istnieje kompletna ścieżka backend + UI i kod znajduje się
w bieżącej gałęzi. Testy z sekcji 30.4, 31.3 i 32.3 są bramką jakości wydania
`HARDWARE-VERIFIED`, a nie częścią brakującej implementacji.

### 0.3. Świadomie poza zakresem v6

Poniższe pozycje są opcjonalnymi rozszerzeniami, nie brakami v6:

- pełna analiza każdego pakietu poza szczegółowym indeksem 4096 rekordów;
- PCAPNG, deszyfrowanie TLS/WPA i zgodność 1:1 z Zeekiem/Wiresharkiem;
- query builder AND/OR z CIDR, wiele nazwanych profili i import raportów;
- LRU/manager miejsca cache, osobny eksport Follow Stream i GraphML;
- Duplicate ACK, RTT danych, pełna analiza TCP window i wiele profili baseline;
- Near Live/True Live oraz aktywne skanowanie sieci.

Pełne limity interpretacyjne i bezpieczeństwa wyniku znajdują się w sekcji
29.7, a jedyny obowiązkowy krok przed oznaczeniem wydania jako
`HARDWARE-VERIFIED` znajduje się w sekcjach 30.4, 31.3 i 32.3.

## 1. Cel dokumentu

Celem projektu jest rozbudowanie zestawu Tab5 + Monster/ESP32-C5 o dwa powiązane mechanizmy:

1. zapis ruchu IEEE 802.15.4/Zigbee/Thread wykrywanego przez Mesh Recon do plików PCAP zgodnych z Wiresharkiem;
2. prostą przeglądarkę plików PCAP na M5Stack Tab5.

Docelowo **Tab5 ma być centrum wszystkich przechwyconych danych**. Każdy Monster wykonuje operacje radiowe i może używać własnej karty SD jako bufora roboczego, ale po zakończeniu capture plik ma zostać przeniesiony lub skopiowany na kartę SD Tab5, zweryfikowany, dodany do centralnej biblioteki i otwierany z poziomu interfejsu Tab5.

Dokument powstał na podstawie analizy lokalnych repozytoriów:

- Tab5: `C:\Users\mati\Documents\GitHub\M5MonsterC5-Tab5`
- JanOS/Monster: `C:\Users\mati\Documents\GitHub\projectZero\ESP32C5`
- analizowana gałąź JanOS: `main`, wydanie 1.7.0;
- analizowana gałąź Tab5: `development`.

## 2. Najważniejsze wnioski

Obie funkcje są możliwe do wykonania na obecnym sprzęcie.

- ESP32-P4 w Tab5 ma wystarczającą moc i PSRAM do indeksowania oraz dekodowania dużych plików PCAP.
- ESP32-C5 ma natywny odbiornik IEEE 802.15.4 i Mesh Recon otrzymuje surowe ramki przed ich parsowaniem.
- JanOS ma już strumieniowy zapis PCAP dla Wi-Fi oraz obsługę karty SD.
- JanOS ma już serwer administracyjny z endpointem pozwalającym strumieniowo pobrać plik z SD.
- Tab5 ma własną kartę SD oraz wewnętrzny moduł Wi-Fi C6 obsługiwany przez ESP-Hosted, więc może po zakończeniu capture połączyć się z `JanOS-Admin` i pobrać plik bez angażowania UART do transmisji dużej ilości danych.
- Obecny interfejs Mesh Recon na Tab5 dostaje przez UART tylko tekstowe podsumowania. Surowych ramek nie należy przesyłać przez ten kanał w czasie capture.

Docelowy przepływ danych:

```text
IEEE 802.15.4
      |
      v
Monster / ESP32-C5
  odbiór -> kolejka -> writer PCAP -> SD Monstera (.part)
                                      |
                                      | stop + zamknięcie pliku
                                      v
                                 gotowy .pcap
                                      |
                                      | HTTP przez JanOS-Admin
                                      v
Tab5 / ESP32-P4
  pobieranie -> SD Tab5 (.part) -> weryfikacja -> centralna biblioteka -> viewer
```

## 3. Stan obecny — JanOS

### 3.1. Mesh Recon

Komponent `components/zig_recon/zig_recon.c`:

- włącza radio IEEE 802.15.4 w trybie promiscuous;
- odbiera ramki w `esp_ieee802154_receive_done()`;
- kopiuje ramkę i metadane do kolejki FreeRTOS;
- przełącza kanały w tasku Mesh Recon;
- parsuje nagłówek MAC i aktualizuje listę PAN-ów oraz węzłów;
- udostępnia wyniki poleceniami `zig_recon_status`, `zig_recon_list` i `zig_recon_nodes`.

Obecna kolejka ma 32 elementy, a maksymalny bufor pojedynczej ramki ma 128 bajtów. Callback odbiornika używa funkcji `xQueueSendFromISR()`, więc kod jest już zorganizowany zgodnie z zasadą: callback radiowy ma być krótki, a przetwarzanie odbywa się w tasku.

Obecny recon uruchamiany przez Tab5 używa:

```text
start_zig_recon all 250
```

Kanały 11–26 są przełączane co 250 ms. Pełny cykl trwa około 4 sekund, dlatego recon na wszystkich kanałach jest dobry do wykrywania sieci, ale nie jest pełnym capture ruchu.

### 3.2. Obecny zapis PCAP

JanOS ma dwa różne mechanizmy PCAP.

#### Serializer handshake

`components/pcap_serializer` przechowuje cały wynik w RAM i wykonuje `realloc()` po dodaniu każdej ramki. Nadaje się do małego pliku handshake, ale nie do długiego, ciągłego capture.

Nie należy wykorzystywać go do Mesh PCAP.

#### Strumieniowy writer Wi-Fi

Polecenie:

```text
start_pcap radio|net
```

tworzy plik w `/sdcard/lab/pcaps`, używa kolejki 256 wskaźników i zapisuje ramki w osobnym tasku. To jest właściwy punkt wyjścia dla nowego, uniwersalnego komponentu PCAP.

Obecny writer wymaga jednak refaktoryzacji, ponieważ:

- obsługuje wyłącznie linktype 105 dla Wi-Fi i linktype 1 dla Ethernetu;
- wykonuje osobny `malloc()` dla każdej ramki;
- nie sprawdza wyników wszystkich wywołań `fwrite()`;
- błąd `malloc()` nie zwiększa licznika utraconych ramek;
- znaczniki czasu pochodzą z `esp_timer_get_time()`, czyli czasu od uruchomienia urządzenia, a nie czasu Unix;
- zatrzymanie może po około dwóch sekundach wymusić usunięcie taska writera;
- finalna nazwa `.pcap` jest używana jeszcze przed bezpiecznym zamknięciem pliku;
- nie ma rotacji plików ani limitu rozmiaru/czasu.

### 3.3. Dostęp HTTP do SD Monstera

JanOS ma endpoint:

```text
GET /api/download?path=<ścieżka względna względem /sdcard/lab>
```

Przykład:

```text
GET http://172.0.0.1/api/download?path=pcaps/zig_ch15_0001.pcap
```

Plik jest wysyłany strumieniowo w odpowiedzi chunked. Endpoint jest już zarejestrowany przez portal `JanOS-Admin`.

Obecnie endpoint:

- nie obsługuje `Range` i wznawiania transferu;
- nie zwraca sumy kontrolnej;
- nie powinien czytać aktywnie zapisywanego pliku;
- korzysta z 2 KB bufora po stronie JanOS.

Jest wystarczający dla pierwszego kompletnego transferu, ale powinien zostać rozszerzony przed obsługą bardzo dużych capture’ów.

## 4. Stan obecny — Tab5

### 4.1. Mesh Recon UI

Tab5 wysyła polecenia przez UART i odbiera wyłącznie tekstowe rekordy:

```text
[ZIG] status ...
[ZIG] pan ...
[ZIG] node ...
[ZIG] END
```

Task monitorujący używa bufora 8 KB. Nie otrzymuje danych binarnych ramek i nie powinien służyć do przesyłania PCAP.

Parser Tab5 rozpoznaje również rekord `[ZIG] edge`, ale aktualny JanOS takich rekordów nie generuje. Topologia pokazywana przez Tab5 nie ma obecnie rzeczywistych krawędzi dostarczonych przez JanOS.

### 4.2. Karty SD są niezależne

Ścieżka `/sdcard/...` na JanOS oznacza kartę zamontowaną w Monsterze. Ta sama ścieżka na Tab5 oznacza fizycznie inną kartę.

Obecne ekrany plików Tab5 wykonują `list_dir` po UART i wyświetlają nazwy plików Monstera. Nie mogą otworzyć zdalnej ścieżki przez lokalne `fopen()`.

Dlatego centralna biblioteka wymaga faktycznego transferu pliku na SD Tab5.

### 4.3. Zasoby do przeglądarki

Tab5 ma:

- ESP32-P4;
- PSRAM pracujący z wysoką częstotliwością;
- lokalny FATFS na SD;
- LVGL 9;
- wewnętrzny moduł Wi-Fi C6 przez ESP-Hosted;
- systemowy zegar synchronizowany z RTC RX8130CE.

To pozwala zbudować viewer działający strumieniowo, z indeksem rekordów w PSRAM, bez ładowania całego PCAP do pamięci.

## 5. Poprawny format ramki IEEE 802.15.4

To jest krytyczny element implementacji.

ESP-IDF przekazuje odebraną ramkę w buforze zawierającym:

```text
frame[0]      długość ramki PHY
frame[1...]   MHR i MAC payload
końcówka      RSSI i LQI w miejscach, które na antenie zajmował FCS
```

Sprzęt weryfikuje FCS, ale nie przekazuje jego oryginalnych dwóch bajtów. Zastępuje je RSSI i LQI.

Aktualny `parse_mac()` używa całego `frame[0]` jako długości danych MAC. W efekcie RSSI i LQI mogą być potraktowane jak końcówka payloadu. Należy to poprawić niezależnie od dodania PCAP.

Dla klasycznego PCAP z `LINKTYPE_IEEE802_15_4_NOFCS = 230` zapisujemy:

```text
packet_data = frame + 1
packet_len  = frame[0] - 2
```

Warunki bezpieczeństwa:

- odrzucić ramkę, jeśli `frame[0] < 2`;
- sprawdzić limit 127 bajtów PHY;
- oddzielić długość danych MAC od długości bufora sterownika;
- zachować RSSI, LQI, kanał i timestamp w osobnych polach metadanych;
- nie dołączać RSSI/LQI na końcu pakietu typu 230.

### 5.1. Wybór linktype

Pierwsza implementacja może obsługiwać dwa tryby:

| Tryb | Linktype | Zawartość | Zastosowanie |
|---|---:|---|---|
| `nofcs` | 230 | czysta ramka IEEE 802.15.4 bez FCS | prosty i pewny MVP |
| `tap` | 283 | nagłówek TAP + ramka bez FCS | rekomendowany format docelowy z kanałem, RSSI i LQI |

Format TAP wymaga poprawnego skonstruowania nagłówka i pól TLV. Przeglądarka Tab5 oraz Wireshark powinny obsługiwać oba warianty.

## 6. Co oznacza „cały ruch mesh”

ESP32-C5 może słuchać tylko jednego kanału IEEE 802.15.4 w danym momencie.

### Tryb Survey

- kanały 11–26;
- hopping według `dwell_ms`;
- dobry do wykrywania PAN-ów i ich kanałów;
- PCAP jest tylko próbką ruchu;
- plik powinien być oznaczony jako `survey/incomplete`.

### Tryb Locked Channel

- radio pozostaje na jednym wybranym kanale;
- zapisuje wszystkie odebrane w zasięgu ramki na tym kanale;
- jest właściwym trybem do analizowania konkretnego Zigbee/Thread/Matter mesh;
- nie gwarantuje ramek urządzeń znajdujących się poza zasięgiem odbiornika.

Po odnalezieniu PAN-u Tab5 powinien proponować przycisk `Capture channel XX`. Tab5 zatrzymuje hopping i uruchamia pełny capture na kanale zapisanym w wyniku recon.

Nie należy filtrować zapisu po PAN ID. ACK, część ramek sterujących i niektóre uszkodzone ramki nie zawierają kompletnego PAN ID. Filtracja powinna następować dopiero w viewerze lub Wiresharku.

Zaszyfrowane payloady Zigbee/Thread pozostaną zaszyfrowane, dopóki użytkownik nie dostarczy odpowiednich kluczy. Sam PCAP nadal będzie poprawny i użyteczny do analizy MAC, ruchu, adresów, częstotliwości oraz timingów.

## 7. Docelowa architektura JanOS

### 7.1. Uniwersalny komponent `pcap_writer`

Należy wydzielić obecny strumieniowy writer z `main/main.c` do osobnego komponentu, np.:

```text
components/pcap_writer/
  CMakeLists.txt
  include/pcap_writer.h
  pcap_writer.c
```

Proponowany interfejs:

```c
typedef struct {
    const char *path;
    uint32_t linktype;
    uint32_t snaplen;
    uint16_t queue_depth;
    uint16_t max_frame_len;
    uint32_t rotate_size_bytes;
    uint32_t rotate_time_seconds;
} pcap_writer_config_t;

typedef struct {
    bool active;
    uint64_t frames_received;
    uint64_t frames_written;
    uint64_t frames_dropped;
    uint64_t bytes_written;
    uint32_t write_errors;
    uint32_t crc32;
} pcap_writer_stats_t;

esp_err_t pcap_writer_start(const pcap_writer_config_t *config);
esp_err_t pcap_writer_submit(const void *data, uint16_t captured_len,
                             uint16_t original_len, int64_t timestamp_us);
esp_err_t pcap_writer_stop(TickType_t timeout);
void pcap_writer_get_stats(pcap_writer_stats_t *out);
```

Wymagania implementacyjne:

- żadnego `fwrite()` w callbacku radiowym;
- żadnego `malloc()` w ISR;
- prealokowana pula/ring ramek;
- nieblokujące dodawanie ramek do kolejki;
- osobny task zapisujący;
- buforowanie zapisów w blokach co najmniej 4 KB;
- sprawdzanie wyników `fwrite()`, `fflush()`, `fsync()` i `fclose()`;
- zatrzymanie przez handshake/event, bez wymuszonego `vTaskDelete()` podczas operacji SD;
- zapis do `nazwa.pcap.part`;
- rename do `nazwa.pcap` dopiero po poprawnym zamknięciu;
- statystyki dropów kolejki, dropów pamięci i błędów SD;
- rotacja pliku po rozmiarze lub czasie;
- obliczanie CRC32 całego pliku w trakcie zapisu.

### 7.2. Podłączenie Mesh Recon

Najbezpieczniejszy przepływ:

1. callback `esp_ieee802154_receive_done()` kopiuje surową ramkę i metadane do istniejącej kolejki ISR;
2. `zig_recon_task` odbiera ramkę;
3. jeśli capture jest aktywny, ramka zostaje przekazana do `pcap_writer` **przed** wywołaniem parsera MAC;
4. ta sama ramka trafia do obecnego `process_frame()` i aktualizuje dane recon;
5. błędy parsera nie usuwają ramki z PCAP.

Nie należy tworzyć dwóch niezależnych odbiorników lub próbować równocześnie uruchamiać starego `start_pcap` i `start_zig_recon`. Oba tryby muszą korzystać z jednego właściciela radia IEEE 802.15.4.

### 7.3. Proponowane polecenia UART

```text
start_zig_pcap <11..26|all> [dwell_ms] [nofcs|tap]
zig_pcap_status
stop
```

Przykłady:

```text
start_zig_pcap 15 250 tap
start_zig_pcap all 250 tap
zig_pcap_status
stop
```

Tryb `all` oznacza survey i musi zostać opisany jako niepełny capture wielokanałowy.

Odpowiedzi maszynowe powinny mieć stabilny format i jawny marker końca:

```text
[ZPCAP] status active=1 mode=locked channel=15 linktype=283 frames_rx=1200 frames_written=1198 dropped=2 bytes=182304 path=/sdcard/lab/pcaps/zig_ch15_0001.pcap.part
[ZPCAP] END
```

Po zatrzymaniu:

```text
[ZPCAP] saved path=/sdcard/lab/pcaps/zig_ch15_0001.pcap size=182304 crc32=89ABCDEF frames=1198 dropped=2 clock_synced=1
[ZPCAP] END
```

Tab5 powinien czekać na `[ZPCAP] END`, zamiast polegać wyłącznie na timeoutach.

### 7.4. Timestamp

Klasyczny PCAP oczekuje czasu Unix. `esp_timer_get_time()` daje czas od startu systemu i powoduje błędne daty w Wiresharku.

Rekomendacja:

- przed capture Tab5 wysyła do JanOS aktualny Unix time pobrany z RTC;
- JanOS zapisuje bazę `epoch_us - monotonic_us`;
- każda ramka otrzymuje stabilny czas Unix bez używania ciężkich operacji w callbacku;
- status pliku zawiera `clock_synced=0|1`;
- przy braku synchronizacji kolejność i odstępy czasowe pozostają poprawne, ale UI ostrzega o braku czasu absolutnego.

## 8. Centralny transfer Monster SD → Tab5 SD

### 8.1. Założenie podstawowe

Tab5 jest właścicielem centralnej biblioteki. Capture na SD Monstera jest kopią roboczą i awaryjną.

Domyślną operacją powinno być `Copy to Tab5`. Opcja `Move to Tab5` może usunąć plik z Monstera wyłącznie po pełnej weryfikacji lokalnej kopii.

### 8.2. Transport

UART pozostaje kanałem sterującym:

- start/stop capture;
- status;
- ścieżka pliku;
- rozmiar;
- CRC32;
- uruchomienie portalu administracyjnego.

HTTP przez `JanOS-Admin` przenosi dane binarne. Jest szybszy i nie blokuje parsera poleceń UART dużymi blokami danych.

### 8.3. Sekwencja transferu

1. Tab5 wysyła `stop` i czeka na `[ZPCAP] saved` oraz `[ZPCAP] END`.
2. Tab5 zapamiętuje ścieżkę, rozmiar, CRC32 i statystyki capture.
3. Tab5 uruchamia `JanOS-Admin` z hasłem sesyjnym.
4. Wewnętrzny C6 Tab5 łączy się z AP Monstera.
5. Tab5 wykonuje `GET /api/download?path=...`.
6. Odpowiedź jest zapisywana strumieniowo do lokalnego `plik.pcap.part`.
7. Podczas zapisu Tab5 liczy odebrane bajty i CRC32.
8. Po zakończeniu porównuje rozmiar oraz CRC32 z danymi JanOS.
9. Tab5 wykonuje `fflush()`, `fsync()` i zamyka plik.
10. Dopiero po pozytywnej weryfikacji zmienia nazwę `.part` na `.pcap`.
11. Capture trafia do centralnego katalogu i jest dostępny w viewerze.
12. Jeśli użytkownik wybrał `Move`, Tab5 wysyła polecenie usunięcia zdalnego pliku.
13. Tab5 zatrzymuje `JanOS-Admin` i przywraca poprzedni stan swojego Wi-Fi.

### 8.4. Wznawianie

Pierwszy działający wariant może pobierać cały plik od początku. Docelowo należy dodać:

- obsługę nagłówka HTTP `Range` po stronie JanOS;
- odpowiedzi `206 Partial Content`;
- `Content-Length`, `Content-Range` i stabilny identyfikator pliku;
- wznowienie na podstawie rozmiaru lokalnego `.part`;
- odrzucenie wznowienia, jeśli rozmiar, mtime lub identyfikator zdalnego pliku się zmienił.

### 8.5. Bezpieczeństwo i niezawodność

- capture musi być zamknięty przed rozpoczęciem downloadu;
- portal powinien używać losowego hasła sesyjnego przekazanego z Tab5 przez UART;
- hasło `12345678` nie powinno być używane automatycznie dla transferów;
- zdalny plik nie może być automatycznie usuwany przed weryfikacją;
- brak miejsca na SD Tab5 musi być wykryty przed pobraniem;
- przerwanie zasilania pozostawia wyłącznie `.part`, nigdy pozornie poprawny `.pcap`;
- po starcie Tab5 może wyszukać osierocone `.part` i zaproponować wznowienie lub usunięcie;
- wszystkie ścieżki i nazwy plików muszą być sanitizowane.

### 8.6. Alternatywa awaryjna

W przyszłości można dodać binarny transfer przez UART z blokami, numerami sekwencyjnymi i CRC. Nie powinien to być główny transport, ale może być użyteczny, gdy połączenie z `JanOS-Admin` jest niemożliwe.

## 9. Organizacja centralnej biblioteki Tab5

Proponowana struktura:

```text
/sdcard/lab/pcaps/
  <monster_id>/
    <YYYY-MM-DD>/
      zig_ch15_20260808_142530.pcap
      zig_ch15_20260808_142530.json
```

Jeżeli czas nie jest dostępny:

```text
zig_ch15_capture_000001.pcap
```

Plik sidecar JSON powinien zawierać informacje, których klasyczny PCAP nie przechowuje wygodnie:

```json
{
  "source_device": "monster-c5-a1b2c3",
  "source_transport": "grove",
  "capture_type": "ieee802154",
  "capture_mode": "locked",
  "channel": 15,
  "linktype": 283,
  "pan_id": "0x1234",
  "started_at": 1786208730,
  "duration_ms": 60000,
  "frames_received": 1200,
  "frames_written": 1198,
  "frames_dropped": 2,
  "size": 182304,
  "crc32": "89ABCDEF",
  "clock_synced": true,
  "remote_deleted": false
}
```

PAN ID jest metadaną opisującą wybór użytkownika i nie oznacza, że PCAP był filtrowany wyłącznie do tego PAN-u.

Tab5 powinien utrzymywać lekki indeks biblioteki albo odbudowywać go ze struktury katalogów i sidecarów. Źródłowy plik PCAP pozostaje najważniejszym artefaktem i musi być czytelny bez indeksu.

## 10. Prosta przeglądarka PCAP na Tab5

### 10.1. Zakres pierwszej wersji

Obsługiwany format:

- klasyczny PCAP 2.4;
- little-endian i big-endian;
- timestampy mikrosekundowe i nanosekundowe;
- bez PCAPNG w pierwszej wersji.

Obsługiwane linktype:

| Linktype | Protokół | Minimalne dekodowanie |
|---:|---|---|
| 1 | Ethernet | MAC, EtherType, ARP, IPv4, IPv6, TCP, UDP, ICMP |
| 105 | IEEE 802.11 | typ/subtyp, adresy, BSSID, sequence |
| 230 | IEEE 802.15.4 bez FCS | FCF, typ, sequence, PAN, adresy, payload |
| 283 | IEEE 802.15.4 TAP | metadane TAP, kanał, RSSI, LQI oraz MAC |

### 10.2. Widoki UI

#### Biblioteka capture

- źródło/Monster;
- typ capture;
- data i czas;
- kanał;
- liczba pakietów;
- rozmiar;
- informacja o dropach;
- przyciski `Open`, `Details`, `Delete`, `Export`.

#### Lista pakietów

Kolumny:

```text
No. | Time | Channel | Source | Destination | Protocol | Length | Info
```

#### Szczegóły pakietu

- nagłówek rekordu PCAP;
- drzewo Ethernet/802.11/802.15.4;
- podstawowe pola Zigbee/Thread, jeśli można je rozpoznać;
- HEX + ASCII;
- oznaczenie payloadu zaszyfrowanego;
- poprzedni/następny pakiet.

#### Filtry

Pierwsza wersja:

- numer pakietu;
- typ/protokół;
- kanał;
- PAN ID;
- adres źródłowy/docelowy;
- długość;
- tylko beacon/data/ACK/command.

Nie należy próbować implementować pełnego języka filtrów Wiresharka w pierwszej wersji.

### 10.3. Model pamięci

Viewer nie ładuje całego pliku.

Podczas otwierania:

1. odczytuje i waliduje 24-bajtowy global header;
2. wykrywa endian i rozdzielczość timestampów;
3. skanuje rekordy sekwencyjnie;
4. zapisuje offsety rekordów lub indeks rzadki w PSRAM;
5. sprawdza `incl_len`, `orig_len`, `snaplen` i granice pliku;
6. aktualizuje pasek postępu indeksowania;
7. ładuje payload tylko dla aktualnie wyświetlanych wierszy i wybranego pakietu.

Dla bardzo dużych plików można przechowywać jeden offset co 64/128 pakietów, a pozostałe rekordy odnajdywać krótkim skanem sekwencyjnym. Przy dostępnej pamięci P4 pełny indeks 32-bitowych offsetów również jest praktyczny dla plików mniejszych niż 4 GB.

Warto rozważyć włączenie `CONFIG_FATFS_USE_FASTSEEK` na Tab5 po wykonaniu testów regresji SD.

### 10.4. Walidacja pliku

Viewer musi odrzucać plik z czytelnym komunikatem, jeśli:

- magic nie jest obsługiwany;
- jest to PCAPNG zamiast PCAP;
- global header jest ucięty;
- linktype nie jest obsługiwany;
- rekord wychodzi poza koniec pliku;
- `incl_len` przekracza `snaplen` lub ustalony bezpieczny limit;
- rozmiar pliku zmienia się podczas indeksowania.

Uszkodzenie końcowego rekordu nie powinno blokować wyświetlenia wszystkich wcześniejszych poprawnych rekordów. UI powinien oznaczyć plik jako częściowo uszkodzony.

## 11. Zmiany w UI Mesh Recon

Po zakończeniu survey każdy PAN powinien pokazywać:

- wykryty kanał lub listę kanałów;
- `Start capture`;
- możliwość wyboru `Locked` albo `Survey`;
- format `TAP` lub `No FCS` w ustawieniach zaawansowanych;
- bieżące: packets, written, dropped, bytes, czas;
- `Stop and transfer to Tab5`.

Rekomendowany główny scenariusz:

```text
Mesh Recon
  -> Start survey
  -> wybór PAN-u
  -> Capture channel 15
  -> podgląd liczników
  -> Stop and transfer
  -> postęp pobierania
  -> weryfikacja
  -> Open in PCAP Viewer
```

Operacje UART i HTTP muszą działać w taskach FreeRTOS. Aktualizacje LVGL z tasków muszą być chronione przez `bsp_display_lock()`/`bsp_display_unlock()`.

## 12. Plan implementacji

Poniższe etapy opisują docelowe zależności całego rozwiązania. Bieżąca kolejność wykonania jest prowadzona w sekcji **18. Główna lista TODO**. Pierwszy działający zakres celowo omija modyfikacje JanOS i zaczyna się od transferu już istniejących plików.

### Etap 0 — przygotowanie i testowe próbki

- zebrać małe referencyjne capture’y: beacon, ACK, data Zigbee, MAC command i Thread;
- zachować surowy bufor ESP-IDF wraz z `frame_info`;
- przygotować automatyczne sprawdzenie PCAP przez `tshark -r`;
- potwierdzić zachowanie RSSI/LQI dla dokładnie używanego ESP-IDF 6.0.1.

Kryterium odbioru: próbka utworzona z bufora C5 otwiera się w Wiresharku bez błędów długości i jest rozpoznawana jako IEEE 802.15.4.

### Etap 1 — korekta Mesh Recon

- rozdzielić długość bufora sterownika od długości MAC bez FCS;
- poprawić `parse_mac()`;
- dodać testy ramek krótkich, ACK i ramek z różnymi trybami adresowania;
- upewnić się, że błędny parser nie blokuje dalszego odbioru.

Kryterium odbioru: RSSI i LQI nie są interpretowane jako payload, a istniejący ekran Mesh Recon nadal działa.

### Etap 2 — uniwersalny `pcap_writer`

- wydzielić writer z `main/main.c`;
- zachować kompatybilność `start_pcap radio|net`;
- dodać konfigurowalny linktype i snaplen;
- dodać pulę ramek/ring, kontrolę błędów, `.part`, CRC32 i bezpieczne stop;
- dodać statystyki oraz rotację.

Kryterium odbioru: Wi-Fi PCAP działa jak wcześniej, a odłączenie lub zapełnienie SD daje błąd i nie tworzy pozornie poprawnego finalnego pliku.

### Etap 3 — Mesh PCAP na C5

- dodać `start_zig_pcap`, `zig_pcap_status` i maszynowe odpowiedzi `[ZPCAP]`;
- podłączyć writer w tasku Mesh Recon przed parserem;
- dodać tryb `nofcs`/230;
- dodać tryby `all/survey` oraz `locked channel`;
- zsynchronizować Unix time z Tab5;
- dodać nazewnictwo i metadane capture.

Kryterium odbioru: godzinny capture na jednym kanale kończy się poprawnym plikiem, a licznik dropów i błędów jest dostępny po UART.

### Etap 4 — IEEE 802.15.4 TAP

- zaimplementować poprawny nagłówek TAP;
- zapisywać kanał, RSSI, LQI i informację o braku FCS;
- sprawdzić kilka kanałów i typów ramek w Wiresharku.

Kryterium odbioru: Wireshark pokazuje ramkę oraz metadane radiowe bez niestandardowego dissektora.

### Etap 5 — centralny transfer do Tab5

- dodać `esp_http_client` do zależności aplikacji Tab5;
- dodać task transferu oraz pasek postępu;
- automatycznie uruchamiać i zatrzymywać `JanOS-Admin`;
- łączyć wewnętrzny C6 z AP Monstera;
- zapisywać do `.part` na SD Tab5;
- porównywać rozmiar i CRC32;
- wykonywać bezpieczny rename;
- obsłużyć `Copy` i zweryfikowane `Move`;
- dodać retry i sprzątanie stanów po błędzie;
- w kolejnym kroku dodać HTTP Range/resume.

Kryterium odbioru: przerwanie transferu nie usuwa pliku z Monstera i nie tworzy finalnego `.pcap` na Tab5. Poprawny transfer daje identyczny rozmiar oraz CRC32.

### Etap 6 — centralna biblioteka

- utworzyć strukturę katalogów według urządzenia i daty;
- zapisywać sidecar JSON;
- skanować i odbudowywać indeks po starcie;
- pokazywać capture’y ze wszystkich Monsterów w jednym miejscu;
- dodać filtrowanie po źródle, typie i dacie.

Kryterium odbioru: pliki pozostają dostępne po restarcie Tab5, nawet jeśli indeks aplikacji zostanie usunięty.

### Etap 7 — viewer MVP

- parser classic PCAP;
- obsługa endian i mikro/nanosekund;
- indeksowanie w PSRAM;
- wirtualizowana lista LVGL;
- linktype 1, 105 i 230;
- ekran szczegółów oraz HEX;
- proste filtry;
- komunikaty dla PCAPNG i uszkodzonych plików.

Kryterium odbioru: Tab5 otwiera własne capture’y Wi-Fi, Ethernet i Mesh bez ładowania całego pliku do RAM.

### Etap 8 — viewer TAP i rozszerzenia

- linktype 283;
- prezentacja kanału, RSSI i LQI;
- podstawowe dekodowanie Zigbee/Thread;
- zakładki/statystyki: liczba protokołów, top talkers, kanały, PAN-y;
- później opcjonalnie PCAPNG i klucze Zigbee/Thread.

## 13. Testy wymagane przed wydaniem

### Capture

- pojedynczy kanał przez co najmniej godzinę;
- survey przez wszystkie kanały;
- dużo krótkich ramek/ACK;
- uszkodzone i nierozpoznane ramki;
- wolna karta SD;
- brak miejsca na SD;
- wyjęcie SD podczas capture;
- wielokrotne start/stop;
- restart w trakcie zapisu `.part`.

### Transfer

- pliki małe i większe niż 100 MB;
- zerwane Wi-Fi;
- restart Tab5;
- restart Monstera;
- brak miejsca na SD Tab5;
- błędne CRC;
- plik z taką samą nazwą;
- `Copy` oraz `Move`;
- kilka Monsterów obsługiwanych kolejno.

### Viewer

- wszystkie cztery obsługiwane linktype;
- oba endiany;
- timestamp mikro- i nanosekundowy;
- ucięty ostatni rekord;
- nieprawidłowe długości;
- PCAPNG;
- bardzo duża liczba pakietów;
- szybkie przewijanie podczas indeksowania;
- usunięcie pliku podczas otwarcia.

Każdy PCAP generowany przez firmware powinien być dodatkowo sprawdzany automatycznie przez `capinfos` i `tshark`.

## 14. Decyzje architektoniczne

1. **Tab5 jest centralnym magazynem i interfejsem.** Monster nie jest docelowym miejscem przeglądania capture’ów.
2. **Capture powstaje na C5.** UART przesyła sterowanie i metadane, nie pełny strumień ramek.
3. **HTTP jest głównym kanałem transferu plików.** UART binary może być wariantem awaryjnym.
4. **Viewer otwiera lokalną kopię z SD Tab5.** Nie wykonuje losowych odczytów bezpośrednio ze zdalnego endpointu.
5. **Pełny capture wymaga blokady jednego kanału.** Hopping jest oznaczony jako survey/incomplete.
6. **Nie filtrujemy zapisu po PAN ID.** Filtry są stosowane podczas analizy.
7. **PCAP classic jest formatem pierwszego wydania.** PCAPNG zostaje na później.
8. **Linktype 230 jest bezpiecznym MVP; linktype 283 TAP jest formatem docelowym dla Mesh.**
9. **Pliki są finalizowane atomowo przez `.part` + rename.**
10. **Usunięcie kopii z Monstera następuje dopiero po sprawdzeniu rozmiaru i CRC32 na Tab5.**

## 15. Oczekiwany rezultat końcowy

Użytkownik uruchamia Mesh Recon na wybranym Monsterze, wybiera wykryty PAN i rozpoczyna capture na jego kanale. Monster zapisuje poprawny PCAP bez blokowania odbiornika. Po naciśnięciu `Stop and transfer` Tab5 zamyka capture, pobiera go z SD Monstera, weryfikuje i umieszcza w centralnej bibliotece.

Plik można natychmiast otworzyć na Tab5, przefiltrować i obejrzeć w podstawowym viewerze albo później wyjąć kartę SD Tab5 i przeanalizować ten sam plik w Wiresharku.

## 16. Mapa kluczowych miejsc w obecnym kodzie

Numery linii odnoszą się do wersji repozytoriów analizowanych podczas tworzenia dokumentu i mogą się zmieniać po kolejnych commitach.

### JanOS / ESP32-C5

| Obszar | Plik i okolica | Znaczenie |
|---|---|---|
| Parser MAC 802.15.4 | `components/zig_recon/zig_recon.c:157` | wymaga korekty długości bez RSSI/LQI |
| Task recon i hopping | `components/zig_recon/zig_recon.c:490` | właściwe miejsce przekazania ramki do writera przed parsowaniem |
| Start radia promiscuous | `components/zig_recon/zig_recon.c:518` | konfiguracja kanałów i taska Mesh Recon |
| Callback odbioru | `components/zig_recon/zig_recon.c:705` | kopia ramki i metadanych do kolejki ISR |
| Stan trybów PCAP | `main/main.c:390` | obecnie wyłącznie `NONE`, `RADIO`, `NET` |
| Stop i finalizacja PCAP | `main/main.c:11555` | wymaga bezpiecznego oczekiwania na writer |
| Konflikty operacji radiowych | `main/main.c:12070` | obecnie Mesh Recon i PCAP wzajemnie się blokują |
| Polecenie `start_pcap` | `main/main.c:15048` | punkt do zachowania kompatybilności po refaktoryzacji |
| Kolejka PCAP | `main/main.c:15125` | obecnie 256 wskaźników |
| HTTP download | `main/main.c:19490` | gotowy transfer pliku z `/sdcard/lab` |
| Enqueue Wi-Fi/Ethernet | `main/main.c:23401` | obecnie `malloc()` dla każdej ramki |
| Task writera | `main/main.c:23447` | baza nowego komponentu `pcap_writer` |
| Serializer handshake | `components/pcap_serializer/pcap_serializer.c:59` | nie używać do długiego capture |

### Tab5 / ESP32-P4

| Obszar | Plik i okolica | Znaczenie |
|---|---|---|
| Kolekcja odpowiedzi Mesh Recon | `main/main.c:27358` | UART, bufor tekstowy i marker `[ZIG] END` |
| Task monitorujący recon | `main/main.c:27410` | cykliczne status/list/nodes |
| Start recon z UI | `main/main.c:27553` | obecnie wysyła `start_zig_recon all 250` |
| Lista zdalnych PCAP | `main/main.c:28001` | tylko nazwy przez `list_dir`, bez lokalnego dostępu do treści |
| Lokalna karta SD | `main/main.c:43053` | montowanie SD Tab5, docelowy centralny magazyn |
| Zależności komponentu | `main/CMakeLists.txt:1` | trzeba dodać klienta HTTP do transferu |

Przed implementacją każdego etapu należy ponownie wyszukać symbole po nazwie, zamiast opierać poprawki wyłącznie na zapisanych numerach linii.

## 17. Zakres możliwy bez modyfikowania JanOS

Przed dodaniem Mesh PCAP możemy zbudować większość centralnego systemu po stronie Tab5, wykorzystując istniejące komendy UART oraz gotowe API `JanOS-Admin`.

### 17.1. Transfer i zdalny menedżer SD

Bez zmian JanOS możemy wykorzystać istniejące endpointy:

```text
GET  /api/list?path=<rel>
GET  /api/download?path=<rel>
GET  /api/read?path=<rel>
POST /api/write?path=<rel>
POST /api/upload?path=<rel>
POST /api/rename
POST /api/delete?path=<rel>
```

Pozwala to zbudować na Tab5:

- przeglądanie całego `/sdcard/lab` Monstera;
- `Copy to Tab5` dla pojedynczego pliku;
- kopiowanie katalogu lub wszystkich nowych plików;
- kolejkę transferów;
- postęp, anulowanie i retry;
- bezpieczny zapis lokalny `.part` + rename;
- upload, rename i delete z potwierdzeniem;
- obsługę osobnych źródeł Grove, USB i MBus.

Bez zmiany JanOS nie mamy zdalnego CRC32 ani HTTP Range. Pierwsza wersja może jednak:

1. odczytać zdalny rozmiar przez `/api/list`;
2. pobrać plik po TCP;
3. ponownie odczytać zdalny rozmiar;
4. porównać go z rozmiarem lokalnym;
5. policzyć lokalny CRC32 i zapisać go w sidecarze;
6. pozostawić oryginał na Monsterze.

Automatyczne `Move` zostaje wyłączone do czasu uzyskania zdalnego CRC32 albo innego stabilnego identyfikatora treści.

### 17.2. Centralna biblioteka Tab5

Tab5 może katalogować bez zmian JanOS:

- handshake PCAP;
- PCAP z obecnego `start_pcap radio`;
- PCAP z obecnego `start_pcap net`;
- wardrive CSV/log;
- dane portali;
- pliki tekstowe i pozostałe artefakty z `/sdcard/lab`;
- PCAP-y skopiowane ręcznie na kartę Tab5.

Metadane, których JanOS nie udostępnia, mogą być tworzone lokalnie: źródłowy tab, identyfikator Monstera, czas transferu, oryginalna ścieżka, rozmiar, lokalny CRC32 i typ pliku.

### 17.3. Viewer i analiza lokalna

Cały parser oraz UI PCAP mogą powstać bez zmian JanOS. Viewer będzie działał na lokalnej kopii z SD Tab5 i obsłuży obecne capture’y Wi-Fi/Ethernet oraz przyszłe Mesh PCAP.

Możliwe dodatki:

- statystyki protokołów i adresów;
- wykrywanie EAPOL/handshake;
- eksport wybranych pakietów do nowego PCAP;
- tagi, notatki i wyszukiwanie;
- porównanie capture’ów;
- późniejsze udostępnienie biblioteki przez portal HTTP Tab5.

### 17.4. Automatyczny obecny Wi-Fi capture

JanOS już obsługuje:

```text
start_pcap radio
start_pcap net
stop
```

Tab5 może więc realizować bez zmian JanOS:

```text
Start capture
  -> lokalny licznik czasu
  -> Stop
  -> wykrycie nowego pliku w /lab/pcaps
  -> JanOS-Admin
  -> transfer na SD Tab5
  -> otwarcie w viewerze
```

### 17.5. Rozbudowa Mesh Recon bez surowych ramek

Na podstawie istniejących rekordów `[ZIG] status`, `[ZIG] pan` i `[ZIG] node` możemy dodać:

- snapshoty JSON/CSV na SD Tab5;
- historię PAN-ów oraz węzłów;
- wykresy RSSI/LQI w czasie;
- alerty o nowym PAN-ie lub urządzeniu;
- oznaczanie znanych urządzeń;
- notatki użytkownika;
- porównanie dwóch sesji recon;
- uproszczoną topologię inferowaną po obserwacjach.

Bez zmian JanOS nie uzyskamy rzeczywistych krawędzi topologii ani surowych ramek 802.15.4.

### 17.6. Twarde ograniczenia bez zmian JanOS

Nie wykonamy w tym wariancie:

- zapisu Mesh Recon do poprawnego PCAP na C5;
- IEEE 802.15.4 TAP z RSSI/LQI/kanałem;
- zdalnego CRC32;
- wznawiania transferu przez HTTP Range;
- bezpiecznego pobierania aktywnie zapisywanego pliku;
- prawdziwych krawędzi mesh;
- live streamingu ramek 802.15.4 do Tab5.

## 18. Historyczna główna lista TODO

Ta checklista dokumentuje kolejność powstawania rozwiązania. Dla aktualnego
zakresu ESPShark Offline obowiązuje sekcja 0. Otwarte elementy w tej sekcji
oznaczają testy, prace JanOS/Mesh/Live albo opcjonalne rozszerzenia i nie
unieważniają statusu `COMPLETE` implementacji Offline v6.

Legenda:

- `[ ]` — niewykonane, odroczone albo oczekujące na osobny test;
- `[x]` — zaimplementowane w kodzie; weryfikację sprzętową opisuje osobna
  sekcja testowa;
- `[~]` — w toku; ten znacznik należy zapisać jako tekst w opisie, ponieważ standardowy Markdown nie ma trzeciego stanu checkboxa;
- `[!]` — zablokowane; powód musi znaleźć się w dzienniku zmian.

### A. Analiza i decyzje

- [x] Przeanalizować Tab5 oraz lokalny JanOS 1.7.0.
- [x] Potwierdzić możliwość lokalnego viewera PCAP na ESP32-P4.
- [x] Potwierdzić możliwość zapisu Mesh PCAP po stronie ESP32-C5.
- [x] Wybrać Tab5 jako centralny magazyn wszystkich capture’ów.
- [x] Wybrać HTTP `JanOS-Admin` jako główny kanał transferu plików.
- [x] Ustalić, że UART służy do sterowania i metadanych, a nie do głównego transferu plików.
- [x] Utworzyć analizę, plan implementacji i żywą listę TODO.

### B. Transfer MVP bez zmian JanOS — aktywny pierwszy milestone

- [x] Utworzyć niezależny komponent `janos_file_transfer` zamiast dopisywać backend do `main/main.c`.
- [x] Dodać `esp_http_client` do zależności komponentu aplikacji Tab5.
- [x] Zdefiniować API transferu bez zależności od LVGL i `tab_context_t`.
- [x] Dodać konfigurację: URL, zdalna i lokalna ścieżka, mount point, rozmiar bufora, timeout, anulowanie i callback postępu; oczekiwany rozmiar jest pobierany automatycznie z `/api/list`.
- [x] Dodać test połączenia z `http://172.0.0.1/api/list`.
- [x] Pobrać ręcznie wskazany istniejący handshake PCAP.
- [x] Zapisywać transfer jako `.pcap.part`.
- [x] Sprawdzać wolne miejsce na SD Tab5 przed rozpoczęciem.
- [x] Liczyć bajty i lokalny CRC32 podczas zapisu.
- [x] Wykonać `fflush()`, `fsync()` i `fclose()` przed finalizacją.
- [x] Porównać lokalny rozmiar z `/api/list` przed i po pobraniu.
- [x] Zmienić nazwę `.part` na `.pcap` dopiero po pozytywnej walidacji.
- [x] Pozostawić zdalny oryginał bez zmian.
- [x] Dodać anulowanie bez blokowania LVGL.
- [ ] Dodać czytelne kody błędów: Wi-Fi, HTTP, SD, brak miejsca, rozmiar, anulowanie. **W toku:** backend zwraca `esp_err_t`, status HTTP i komunikaty etapów, a popup pokazuje przyczynę; pozostało rozdzielenie ich na stabilny enum błędów dla UI.

Kryterium milestone B: wskazany handshake PCAP zostaje skopiowany z SD Monstera na SD Tab5 i po przerwaniu transferu nie pojawia się fałszywy finalny `.pcap`.

### C. Automatyzacja połączenia z `JanOS-Admin`

- [x] Wygenerować losowe hasło sesyjne.
- [x] Wysłać `stop` i zaczekać na zakończenie aktywnej operacji.
- [x] Wysłać `start_admin_portal <password>` przez właściwy transport UART/USB.
- [x] Poczekać na potwierdzenie uruchomienia portalu.
- [x] Połączyć wewnętrzny C6 Tab5 jako STA z `JanOS-Admin`; test sprzętowy potwierdził DHCP `172.0.0.2` i transfer HTTP zakończony `ESP_OK`.
- [x] Nie pomylić tej operacji z komendą `wifi_connect`, która steruje radiem Monstera.
- [x] Zapisać poprzedni stan Wi-Fi Tab5 i odtworzyć go po transferze.
- [x] Zatrzymać portal JanOS po zakończeniu kolejki.
- [ ] Obsłużyć timeout, błędne hasło oraz utratę AP. **W toku:** są timeouty i maksymalnie cztery próby ponownego połączenia; zachowanie przy utracie AP wymaga testu sprzętowego.

Kryterium milestone C: użytkownik wybiera plik i nie musi ręcznie łączyć Tab5 z siecią Monstera.

### D. UI `Copy to Tab5`

- [x] Dodać `Copy to Tab5` na ekranie `Captured Handshakes`.
- [ ] Pokazywać nazwę źródła: Grove, USB albo MBus.
- [ ] Dodać ekran/popup postępu z liczbą bajtów, procentem, prędkością i ETA. **W toku:** popup pokazuje etap, liczbę bajtów i procent; pozostały prędkość oraz ETA.
- [x] Dodać `Cancel`.
- [x] Aktualizować LVGL wyłącznie z tasku LVGL albo pod blokadą display/LVGL.
- [ ] Po sukcesie pokazać lokalną ścieżkę i przycisk `Open`.
- [x] Po błędzie pozostawić zdalny oryginał i czytelnie opisać przyczynę.
- [x] Rozszerzyć wybór plików z `/lab/handshakes` na `/lab/pcaps`; test MBus wykrył dwa pliki w `/sdcard/lab/pcaps`.
- [x] Dodać osobny kafel `PCAP Captures`, który listuje `.pcap` z `/sdcard/lab/pcaps`; test sprzętowy MBus zakończony powodzeniem.
- [x] Udostępnić `COPY TO TAB5` oraz `Copy latest` także dla `sniff_N.pcap` z MITM i `start_pcap radio`; `sniff_2.pcap` skopiowano bez rebootu.
- [ ] Powtórzyć listing i transfer `PCAP Captures` przez Grove oraz USB, aby zamknąć pokrycie wszystkich transportów.

Kryterium milestone D: pełny scenariusz kopiowania jest dostępny z ekranu dotykowego.

### E. Centralna biblioteka

- [ ] Ustalić stabilny `monster_id` dla każdego źródła.
- [ ] Utworzyć lokalną strukturę katalogów według Monstera i daty.
- [ ] Zapisywać sidecar JSON dla każdego importu.
- [ ] Skanować bibliotekę po starcie Tab5.
- [ ] Odbudowywać indeks z plików i sidecarów.
- [ ] Obsłużyć konflikty nazw bez nadpisywania.
- [ ] Filtrować bibliotekę po źródle, typie, dacie i statusie.
- [ ] Dodać lokalne rename/delete z potwierdzeniem.
- [ ] Wyszukiwać osierocone `.part` i proponować retry albo usunięcie.
- [ ] Dodać kolejkę `Copy all new files` dla jednego Monstera. **W toku:** `Sync new` obsługuje wszystkie pliki bieżącej kategorii Handshakes albo PCAP Captures w jednej sesji `JanOS-Admin`; po teście sprzętowym pozostaje wspólna kolejka obu kategorii.

### F. PCAP Viewer MVP

- [x] Utworzyć niezależny od UI komponent `pcap_reader` z publicznym API.
- [x] Obsłużyć strumieniowo classic PCAP 2.4.
- [x] Obsłużyć endian little/big.
- [x] Obsłużyć timestampy mikro- i nanosekundowe.
- [x] Walidować nagłówek oraz granice każdego rekordu.
- [x] Indeksować rekordy w PSRAM bez ładowania payloadów całego pliku; indeks
  szczegółowy obejmuje maksymalnie 4096 rekordów, a skan zlicza cały plik.
- [x] Obsłużyć linktype 1 Ethernet: Ethernet/VLAN, ARP, IPv4, IPv6, TCP/UDP.
- [x] Obsłużyć linktype 105 IEEE 802.11: management/control/data, LLC/SNAP i EAPOL.
- [x] Obsłużyć linktype 230 IEEE 802.15.4 bez FCS; pełna zgodność wariantów
  adresowania 802.15.4-2015 pozostaje przypadkiem testowym.
- [x] Obsłużyć linktype 283 IEEE 802.15.4 TAP.
- [x] Dodać skalowalną listę pakietów LVGL jako paginację po 20 rekordów.
- [x] Dodać szczegóły nagłówków oraz podgląd pierwszych 256 bajtów HEX/ASCII.
- [x] Dodać filtry `ALL`, `DNS`, `TCP`, `UDP`, `HTTP`, `TLS`, `ARP`, `EAPOL`,
  `802.11 MGMT` i `MALFORMED`.
- [x] Rozpoznać PCAPNG i pokazać kontrolowany komunikat o nieobsługiwanym formacie.
- [x] Zachować poprawne rekordy poprzedzające ucięty/uszkodzony ogon i pokazać
  `TRUNCATED TAIL`.
- [ ] Opcjonalny research: generator `PCAP → Zeek logs/events → English summary`.
- [x] Zamknąć bramkę wykonalności: nie portować pełnego runtime Zeeka; użyć
  ograniczonych, niezależnych komponentów C dostosowanych do ESP32-P4.
- [x] Zdefiniować niezależny kontrakt `pcap_summary`, oddzielony od LVGL.
- [x] Zaimplementować odpowiedniki `Traffic`, `Protocols`, `Anomalies` i `IOC`
  przez Overview, DNS, Flows, Apps, Health oraz Investigation.
- [ ] Opcjonalna walidacja referencyjna: porównać wyniki Tab5 z Zeekiem na corpus.
- [ ] Bramka wydania: zmierzyć pamięć, czas i odporność na pustych, uciętych oraz
  uszkodzonych capture'ach.

### G. Automatyczny obecny Wi-Fi capture i import

- [ ] W MITM automatycznie używać znanych haseł JanOS. **W toku:** Tab5 używa `wifi_connect "<SSID>" --saved`, więc JanOS sprawdza `eviltwin.txt`, `portals.txt` i `home.txt`; pozostał test sprzętowy na Grove, USB i MBus.
- [ ] Dodać kontrolę `start_pcap radio` z Tab5.
- [ ] Dodać kontrolę `start_pcap net` z Tab5.
- [ ] Pokazywać lokalny czas trwania capture.
- [ ] Po `stop` sparsować ścieżkę, frames i drops z odpowiedzi JanOS.
- [ ] Wykryć nowy plik w `/lab/pcaps`.
- [ ] Automatycznie uruchomić transfer.
- [ ] Po transferze zaproponować `Open in PCAP Viewer`.

### H. Mesh Recon po stronie Tab5 bez zmian JanOS

- [ ] Zapisywać snapshot sesji jako JSON.
- [ ] Dodać eksport CSV PAN-ów i węzłów.
- [ ] Zapisywać historię RSSI/LQI.
- [ ] Dodać wykres aktywności kanałów.
- [ ] Dodać alerty o nowych PAN-ach i węzłach.
- [ ] Dodać tagi `known`, `unknown`, `interesting` i notatki.
- [ ] Porównywać dwie zapisane sesje.
- [ ] Wyraźnie oznaczyć topologię jako inferowaną, dopóki JanOS nie wysyła `[ZIG] edge`.

### I. Synchronizacja i eksport

- [ ] Dodać porównanie zdalnej i lokalnej listy plików. **W toku:** Tab5 porównuje dokładną zdalną ścieżkę i rozmiar z trwałym indeksem oraz sprawdza, czy lokalny plik nadal istnieje; JanOS nie udostępnia jeszcze zdalnego czasu modyfikacji ani CRC32.
- [ ] Kopiować tylko brakujące pliki. **W toku:** `Sync new` pomija pozycje zgodne z indeksem, indeksuje również istniejącą kopię o tej samej nazwie i rozmiarze, a brakujące pobiera kolejno bez ponownego łączenia Wi-Fi; wymagany test sprzętowy.
- [ ] Dodać kolejkę obejmującą kilka Monsterów obsługiwanych kolejno.
- [ ] Dodać własny portal HTTP na Tab5 dla telefonu/komputera.
- [ ] Udostępnić download i upload lokalnej biblioteki.
- [ ] Dodać raport wykonanych oraz nieudanych synchronizacji. **W toku:** popup końcowy pokazuje liczniki `Copied`, `Already synced` i `Failed`; brakuje jeszcze osobnej, przeglądalnej historii sesji.

### J. Późniejsze zmiany JanOS

- [ ] Zmienić nazewnictwo capture MITM z ogólnego `sniff_N.pcap` na nazwę zawierającą czas, SSID i rosnący numer.
- [ ] Przy poprawnej dacie i godzinie zapisywać MITM jako `YYYY-MM-DD_HH-MM-SS_<SSID>_pcap<N>.pcap`, np. `2026-08-08_14-32-10_MyWiFi_pcap3.pcap`; używać wyłącznie znaków bezpiecznych dla FAT.
- [ ] Gdy JanOS nie ma wiarygodnej daty/godziny, używać wariantu `<SSID>_pcap<N>.pcap`, np. `MyWiFi_pcap3.pcap`.
- [ ] Sanityzować SSID przed użyciem w nazwie: zamieniać separatory, znaki sterujące i znaki niedozwolone przez FAT na `_`, ograniczyć długość oraz używać `hidden` dla pustego SSID.
- [ ] Wyznaczać `N` osobno dla zsanityzowanego SSID: przeskanować `/sdcard/lab/pcaps`, znaleźć największy pasujący sufiks `_pcap<N>.pcap` zarówno w plikach z datą, jak i bez daty, a następnie użyć `N + 1`.
- [ ] Nie uzupełniać luk po usuniętych capture’ach. Jeżeli istnieją `pcap1` i `pcap3`, następny plik musi otrzymać `pcap4`.
- [ ] Rozszerzyć `find_next_pcap_file_number()` albo zastąpić je helperem przyjmującym SSID i tryb nazwy, bez zmiany katalogu `/sdcard/lab/pcaps` ani formatu zawartości PCAP.
- [ ] Dodać testy JanOS dla: pierwszego capture, kolejnego capture tego samego SSID, dwóch różnych SSID, nazwy z datą, fallbacku bez daty, ukrytego SSID, znaków specjalnych, istniejących luk oraz osiągnięcia limitu długości ścieżki.
- [ ] Poprawić długość ramek 802.15.4 bez RSSI/LQI w payloadzie.
- [ ] Wydzielić uniwersalny `pcap_writer`.
- [ ] Dodać `start_zig_pcap` i `zig_pcap_status`.
- [ ] Dodać locked-channel i survey PCAP.
- [ ] Dodać linktype 230.
- [ ] Dodać linktype 283 TAP.
- [ ] Dodać zdalny CRC32/identyfikator treści.
- [ ] Dodać HTTP Range i możliwość wznowienia.
- [ ] Włączyć `Move to Tab5` dopiero po weryfikacji zdalnego i lokalnego CRC32.

## 19. Zasady przenośności na CoreS3

Nowe elementy należy pisać tak, aby logika mogła zostać przeniesiona do projektu CoreS3 bez kopiowania kodu związanego z rozdzielczością Tab5, jego `tab_context_t` albo konkretnymi pinami.

### 19.1. Podział komponentów

Rekomendowana struktura:

```text
components/
  janos_file_transfer/
    include/janos_file_transfer.h
    janos_file_transfer.c
    CMakeLists.txt
  pcap_reader/
    include/pcap_reader.h
    pcap_reader.c
    CMakeLists.txt
  capture_library/
    include/capture_library.h
    capture_library.c
    CMakeLists.txt

main/
  platform_tab5_*.c
  ui_tab5_*.c
```

Po stronie CoreS3 potrzebne będą własne adaptery `platform_cores3_*` i `ui_cores3_*`, ale komponenty transferu, biblioteki oraz parsera PCAP powinny pozostać bez zmian.

### 19.2. Zakazane zależności w komponentach wspólnych

Komponenty wspólne nie powinny bezpośrednio używać:

- `lv_obj_t` ani innych obiektów LVGL;
- `tab_context_t`;
- `bsp_display_lock()`;
- numerów UART Grove/M5Bus;
- rozdzielczości 1280×720;
- globalnych wskaźników ekranów;
- stałej ścieżki montowania, jeśli można ją przekazać w konfiguracji;
- założeń, że dostępne są trzy jednoczesne zakładki/Monstery.

### 19.3. Wspólny model zdarzeń

Backend powinien raportować stan przez callback albo kolejkę:

```c
typedef enum {
    JANOS_TRANSFER_STARTING,
    JANOS_TRANSFER_CONNECTING,
    JANOS_TRANSFER_DOWNLOADING,
    JANOS_TRANSFER_VERIFYING,
    JANOS_TRANSFER_DONE,
    JANOS_TRANSFER_ERROR,
    JANOS_TRANSFER_CANCELLED,
} janos_transfer_event_type_t;
```

Zdarzenie powinno zawierać co najmniej:

- typ;
- odebrane i całkowite bajty;
- kod błędu;
- ścieżkę lokalną;
- komunikat techniczny.

UI Tab5 i CoreS3 tłumaczą te same zdarzenia na własne ekrany.

### 19.4. Pamięć i rozmiar ekranu

- duże indeksy i bufory umieszczać w PSRAM, jeśli jest dostępna;
- zapewnić mniejszy tryb pamięci dla CoreS3;
- rozmiar bufora HTTP i indeksu PCAP ustalać konfiguracją;
- nie uzależniać backendu od fontów ani wymiarów kontrolek;
- na CoreS3 używać krótszych wierszy, mniejszych fontów i osobnego ekranu szczegółów zamiast szerokiej tabeli;
- operacje plikowe i sieciowe zawsze wykonywać poza taskiem LVGL.

### 19.5. Checklist CoreS3 dla każdej funkcji

Przy każdej implementacji należy odpowiedzieć w dzienniku zmian:

- Czy backend jest niezależny od LVGL?
- Czy mount point SD jest konfigurowalny?
- Czy UART/USB jest przekazywany przez adapter?
- Czy użyto standardowego API ESP-IDF zamiast funkcji specyficznych dla Tab5?
- Czy rozmiary buforów można zmniejszyć?
- Czy UI da się przedstawić na 320×240?
- Jakie pliki komponentu można skopiować do repo CoreS3 bez modyfikacji?

## 20. Dziennik implementacji

Ta sekcja ma być aktualizowana przy każdej zmianie kodu związanej z transferem, biblioteką, viewerem albo Mesh PCAP. Samo oznaczenie checkboxa jako zakończone nie wystarcza.

Każdy wpis musi zawierać:

- datę;
- repozytorium i gałąź;
- zmienione pliki;
- opis zachowania przed i po zmianie;
- wykonane testy oraz ich wynik;
- znane ograniczenia;
- wpływ na CoreS3;
- następny krok.

### 20.1. Rejestr zmian

| Data | Zakres | Zmiany | Weryfikacja | CoreS3 / uwagi |
|---|---|---|---|---|
| 2026-08-11 | ESPShark Offline v6 — korekta `SHOW FQDN` | Parser zachowuje do czterech unikalnych A/AAAA z jednej odpowiedzi zamiast wyłącznie pierwszego, wykorzystuje częściowe odpowiedzi DNS/TCP mieszczące się w oknie dekodera oraz nazwę owner dla odpowiedzi bez question. Tabela preferuje dokładny SNI/HTTP Host bieżącego flow, potem mapę DNS/device. Przycisk i pager pokazują liczbę dostępnych name hints. Cache schema podniesiono do v7, aby wcześniejszy pusty lub niepełny cache przebudował się automatycznie. | Kontrola statyczna i `git diff --check`; zgodnie z decyzją właściciela nie uruchomiono kompilacji. Test sprzętowy opisuje sekcja 32.3. | Bez zapytań sieciowych i bez zmian JanOS. Nazwa z SNI/Host jest przypisana do konkretnego flow; globalna relacja IP/FQDN nadal jest wskazówką, szczególnie dla współdzielonych adresów CDN. |
| 2026-08-11 | ESPShark Offline v6 — interaktywny DNS | Zastąpiono statyczne Top Domains interaktywną listą. Kliknięcie domeny pokazuje klientów zapytań, rozwiązane adresy A/AAAA i największe flow skorelowane przez adres DNS albo dokładny TLS SNI/HTTP Host; każde flow ma `FILTER`, `FOLLOW` ASCII i `HEX` oraz powrót do listy DNS. | Pełny build ESP-IDF 5.4.1 zakończony sukcesem; firmware ma 71% wolnego miejsca w najmniejszej partycji app. Drill-down ma jawne limity 16 klientów, 16 adresów i 24 największych flow. Pozostał test sprzętowy z sekcji 31.3. | Funkcja analizuje tylko indeksowaną próbkę PCAP i istniejącą tabelę flow, nie wykonuje zapytań DNS do Internetu i nie wymaga zmiany JanOS ani schema cache. |
| 2026-08-11 | ESPShark Offline v6 — FQDN | Dodano bounded mapę DNS `IP → FQDN`, obsługę CNAME przed A/AAAA, przełącznik `SHOW FQDN` w tabeli Source/Destination, cache/report schema v6 oraz eksport mapowań w JSON. | Pełny build ESP-IDF 5.4.1 zakończony sukcesem; komponenty przeszły `-Werror`, firmware ma 71% wolnego miejsca w najmniejszej partycji app. Pozostał test sprzętowy z sekcji 30.4. | Funkcja jest lokalna dla analizy PCAP na Tab5, nie wymaga zmian JanOS ani zapytań DNS do Internetu. |
| 2026-08-11 | Finalny audyt ESPShark Offline v5 | Dodano kanoniczną sekcję 0, uzgodniono historyczne TODO z kodem, oddzielono zakończenie implementacji od bramki sprzętowej i sklasyfikowano pozostałe pomysły jako opcjonalny backlog po v5. Audyt wykonano względem `development` HEAD `1f2700f` i komponentów reader/summary/flow/investigation/store schema v5. | Inspekcja kodu, historii Git, kontraktów komponentów i akcji UI; `git diff --check`. Pełny build i macierz sprzętowa pozostają w sekcji 29.8. | ESPShark Offline pozostaje lokalny dla Tab5; komponenty backendowe zachowują stałe limity i brak zależności od LVGL tam, gdzie przewidziano przenośność. |
| 2026-08-08 | Analiza | Przeanalizowano oba repozytoria, obecny Mesh Recon, PCAP, SD i `JanOS-Admin`. | Inspekcja kodu lokalnego JanOS 1.7.0 i Tab5 `development`. | Ustalono rozdzielenie backendu od UI. |
| 2026-08-08 | Dokumentacja | Utworzono ten dokument z architekturą PCAP, transferu i planem etapów. | `git diff --check` bez błędów. | Brak zmian firmware. |
| 2026-08-08 | Backlog | Dodano zakres bez zmian JanOS, główną checklistę TODO, kolejność transfer-first i zasady portowania do CoreS3. | Kontrola struktury Markdown i statusu repo. | Nowe backendy mają powstawać jako komponenty przenośne. |
| 2026-08-08 | Transfer MVP Tab5 | Dodano backend HTTP, automatyczne sterowanie `JanOS-Admin`, zapis `.part`, walidację oraz UI kopiowania handshake. JanOS pozostał bez zmian. | ESP-IDF 5.4.1: build zakończony powodzeniem; test sprzętowy oczekuje na wykonanie przez użytkownika. | Backend nie zależy od LVGL i może zostać przeniesiony do CoreS3. |
| 2026-08-08 | Czytelność `Copy to Tab5` | Każdy wiersz handshake ma jawnie wymierzony poziomy obszar akcji 392×62 px. Przycisk transferu ma 250×62 px, kontrastowe obramowanie, tekst `COPY TO TAB5` i opis kierunku transferu; usunięto zależność czytelności od ikony fontu. Po załadowaniu listy nagłówek pokazuje dodatkowo `Copy latest` obok `Send to wpa-sec` i `Clean`. | Kontrola statyczna i `git diff --check`; bez kompilacji zgodnie z decyzją użytkownika. | CoreS3 otrzyma osobny, zwężony wariant UI; backend bez zmian. |
| 2026-08-08 | MITM — znane hasła | MITM próbuje `wifi_connect --saved` dla każdej zabezpieczonej sieci, dzięki czemu JanOS szuka hasła w Evil Twin, Portal i Home. Naprawiono transport odpowiedzi dla USB, zwiększono czas odczytu i dodano przejście do ręcznego hasła po nieudanej próbie. | Kontrola statyczna i `git diff --check`; bez kompilacji. Test sprzętowy pozostaje otwarty. | Zachowanie opiera się na przenośnej komendzie JanOS; CoreS3 potrzebuje tylko własnego UI i adaptera transportu. |
| 2026-08-08 | OTA — poprawny moment `Done` | Usunięto fałszywy sukces po 12 s ciszy oraz ogólne markery `restart`/`reboot`. `download complete` i cisza oznaczają finalizację z zablokowanym przyciskiem; `Done - Close` pojawia się dopiero po `OTA: update applied, restarting` albo po terminalnym `no update`. | Kontrola statyczna i `git diff --check`; bez kompilacji. Do sprawdzenia na fizycznym OTA Grove/USB/MBus. | Parser pozostaje niezależny od konkretnego portu dzięki istniejącemu adapterowi transportu. |
| 2026-08-08 | Tab5 C6 — przywrócenie SDIO | Naprawiono brak startu wewnętrznego Wi-Fi: host ESP32-P4 ponownie używa fabrycznego interfejsu SDIO do ESP32-C6 zamiast błędnego SPI. Przywrócono slot 1, magistralę 4-bit 40 MHz, piny CMD/CLK/D0–D3 `13/12/11/10/9/8` i reset GPIO15. | Inspekcja aktywnego `sdkconfig`, historii Git i konfiguracji ESP-Hosted; `git diff --check`. Bez kompilacji zgodnie z decyzją użytkownika. Test sprzętowy Copy pozostaje otwarty. | MBus `37/38` i Grove `53/54` nie zostały zmienione; JanOS pozostaje bez zmian. |
| 2026-08-08 | Tab5 C6 — odporność SDIO na brak bufora | Po uruchomieniu SDIO usunięto reboot powodowany przez `assert(pkt_rxbuff)` w ESP-Hosted 2.8.5. Zastosowano lokalny backport zachowania wprowadzonego przez Espressif w 2.12.0: chwilowy brak bufora RX powoduje kontrolowane odrzucenie pakietu i log, a brak większego bufora stream nie niszczy poprzedniej alokacji. | Analiza pełnego panic dumpu i stosu `sdio_push_data_to_queue`; kontrola statyczna oraz `git diff --check`. Bez kompilacji. | Zmiana dotyczy wyłącznie sterownika SDIO P4–C6; UART-y Monsterów są poza zakresem. |
| 2026-08-08 | Tab5 C6 — odporność TX i DHCP | Drugi test doszedł do połączenia wewnętrznego C6 z `JanOS-Admin`, po czym pierwszy DHCP Discover wywołał `assert(copy_buff)`. Zastąpiono assert kontrolowanym błędem `ESP_ERR_ESP_NETIF_*` i dodano log odzyskania puli. Pierwsza wersja rezerwowała dwa bufory STA DMA podczas tworzenia kanału; kolejny test wykazał, że ten moment rezerwacji jest zbyt wczesny. | Analiza stosu `transport_drv_sta_tx -> low_level_output -> dhcp_discover`; porównanie z aktualną implementacją Espressif oraz `git diff --check`. Bez kompilacji. Ponowny test sprzętowy pozostaje otwarty. | Zmiana jest lokalna dla hosta ESP-Hosted na Tab5; nie zmienia JanOS, MBus ani Grove. Dla CoreS3 ma znaczenie tylko przy użyciu ESP-Hosted pod presją internal/DMA RAM. |
| 2026-08-08 | Tab5 C6 — priorytet INIT RX | Trzeci test nie wszedł już w reset loop, ale wczesna rezerwacja TX odebrała DMA krytycznej ramce INIT. Usunięto rezerwację z tworzenia kanału; jeden bufor STA jest odkładany dopiero po udanym `rpc_wifi_init`. Kopie odebranych ramek SDIO używają general-heap fallback, gdy pula DMA jest pusta, więc INIT/RPC mogą trafić do PSRAM zamiast zostać utracone. | Log potwierdził kontrolowany brak rebootu, a następnie `SDIO RX mempool exhausted`, brak INIT i nieudany ponowny `sdmmc_card_init`. Przeprowadzono inspekcję kolejności `esp_hosted_init -> SDIO INIT -> rpc_wifi_init`; kontrola statyczna bez kompilacji. | Fallback dotyczy wyłącznie CPU-side kopii RX po wykonaniu transferu DMA. Nie przenosi samego bufora magistrali do PSRAM i nie zmienia interfejsów Monsterów. |
| 2026-08-08 | Tab5 C6 — bufor DHCP | Czwarty test potwierdził działanie fallbacku: INIT, RPC, start Wi-Fi i asocjacja z `JanOS-Admin` zakończyły się bez rebootu. Rezerwacja wykonana dopiero po RPC była jednak za późna i DHCP nie dostał bufora. Docelowo odkładany jest jeden, nie dwa, bufor STA podczas tworzenia kanału; fallback RX chroni INIT, a wywołanie po RPC pozostaje bezpieczną ponowną próbą. | Logi `using general heap fallback`, `Received INIT`, `WiFi initialized successfully`, `Station mode: Connected`, a następnie `Could not reserve...` i kontrolowany timeout. Kontrola statyczna bez kompilacji. | Jeden blok 1664 B internal/DMA jest dedykowany ścieżce STA; CPU-side kopie RX mogą korzystać z PSRAM. JanOS oraz UART MBus/Grove pozostają bez zmian. |
| 2026-08-08 | Tab5 C6 — wspólny TX DMA | Piąty test pokazał, że bufor zarezerwowany wyłącznie dla STA blokował `sdio_write_task` podczas odpowiedzi INIT i powodował `assert(sendbuf)`. Usunięto osobną rezerwację STA. Jeden blok DMA jest teraz rezerwowany w puli write-tasku SDIO już podczas tworzenia kanału STA, z ponowną próbą na początku `bus_init`; pakiety STA/AP oraz kopie streaming RX oczekują w general heap/PSRAM. DMA jest zajmowane dopiero na czas faktycznego, szeregowego zapisu magistralą. | Analiza panicu `sdio_write_task:534`, stosu `mempool_alloc` i całego lifecycle bufora. Usunięto także assert w ścieżce write oraz przeniesiono staging `send_slave_config` do general heap. Kontrola statyczna bez kompilacji. | Architektura odpowiada ograniczeniu jednego ciągłego bloku internal/DMA i wykorzystuje mocną stronę P4 — dużą PSRAM. JanOS, C6 firmware i UART Monsterów pozostają bez zmian. |
| 2026-08-08 | JanOS-Admin i RX 1024 B | Szósty test doszedł do `Station mode: Connected`, co potwierdza działający AP Monstera, lecz DHCP/RPC utknęły po `RX stream buffer allocation failed (len=1024)`. Zweryfikowano w JanOS, że `start_admin_portal` tworzy jawny WPA2 SSID `JanOS-Admin` na kanale 1 (`ssid_hidden=0`, maks. 4 klientów). Tab5 loguje teraz wysłanie pełnej komendy i osobno potwierdzenie Monstera, bez ujawniania hasła. Sterownik rezerwuje wcześnie jeden bufor TX oraz dwa pełne bufory streaming RX DMA. | Inspekcja JanOS `cmd_start_admin_portal`, logu asocjacji i ścieżki `sdio_rx_get_buffer`; `git diff --check` bez kompilacji. | Zmiany funkcjonalne pozostają wyłącznie w Tab5. JanOS został tylko odczytany; jego AP nie jest ukryty ani modyfikowany. |
| 2026-08-08 | Transfer zakończony, cleanup TLS | Siódmy test przeszedł cały przepływ: start AP, SDIO, DHCP `172.0.0.2`, HTTP, zapis 996 B, CRC32 i `ESP_OK`. Reset następował dopiero po sukcesie, gdy idle task usuwał worker i odrzucał callback pthread/LwIP pod adresem XIP PSRAM `0x4800705e`. Worker jawnie uruchamia teraz wewnętrzny cleanup pthread przed `vTaskDelete`, zwalniając semafor LwIP i zerując slot callbacku. | Log potwierdził plik `/sdcard/lab/pcaps/imported/mbus/TP-Link_0E90_700E90_234390.pcap`, 996 B, CRC32 `0b3ee871` i późniejszy panic `vPortTLSPointersDelCb`. Mapa ELF potwierdziła umieszczenie callbacku w `0x4800705e`; kontrola statyczna bez kompilacji. | Backend transferu jest sprawdzony na sprzęcie. Obejście dotyczy cyklu życia taska Tab5 przy ESP-IDF 5.4.1 i `CONFIG_SPIRAM_XIP_FROM_PSRAM`; JanOS oraz CoreS3 nie są modyfikowane. |
| 2026-08-08 | Plan testu MITM PCAP | Potwierdzono, że `start_pcap net` używany przez ekran MITM zapisuje kolejne pliki jako `/sdcard/lab/pcaps/sniff_N.pcap` z linktype 1 (Ethernet). Obecny ekran handshake listuje wyłącznie `/sdcard/lab/handshakes`, więc nie pokaże capture MITM. Do TODO dodano kafel `PCAP Captures`, listing `/sdcard/lab/pcaps` oraz ponowne użycie istniejącego backendu `COPY TO TAB5`. | Inspekcja `mitm_connect_and_start_cb`, JanOS `cmd_start_pcap`, `find_next_pcap_file_number` i strony `Compromised Data`; bez zmian JanOS i bez kompilacji. | Jest to następny test po poprawce cleanup: wygenerować świeży `sniff_N.pcap`, zatrzymać capture, a po dodaniu listy skopiować go na SD Tab5. Backend HTTP nie wymaga zmian. |
| 2026-08-08 | `PCAP Captures` w Compromised Data | Dodano piąty kafel `PCAP Captures`, osobną stronę i asynchroniczny listing `list_dir /sdcard/lab/pcaps` z fallbackami starszych ścieżek. Pliki `.pcap` mają `Copy latest`, per-file `COPY TO TAB5`, `DELETE FILE` i zbiorcze `Clean`. Kopiowanie używa istniejącego bezpiecznego workera HTTP, `.part`, CRC32 i unikalnych nazw lokalnych. | Kontrola wszystkich przełączników `compromised_file_kind_t`, lifecycle strony, odświeżania po delete/clean oraz ograniczenia callbacku copy; `git diff --check` bez kompilacji. Test sprzętowy pozostaje otwarty. | JanOS nie został zmieniony. Ten sam rodzaj strony i adapter transportu można przenieść do CoreS3, zmniejszając jedynie layout przycisków. |
| 2026-08-08 | Test `PCAP Captures` przez MBus | Kafel odnalazł dwa pliki w `/sdcard/lab/pcaps`, uruchomił tymczasowy `JanOS-Admin`, zestawił P4–C6 SDIO oraz DHCP `172.0.0.2` i skopiował `sniff_2.pcap` na SD Tab5. Jawny cleanup pthread/LwIP usunął poprzedni reboot po `ESP_OK`. | Zapisano `/sdcard/lab/pcaps/imported/mbus/sniff_2.pcap`, 938 B, CRC32 `a79930bf`. Log działał dalej co najmniej do 182340 ms, obejmując dwukrotne wygaszenie i wybudzenie ekranu, bez panicu ani rebootu. | Milestone MBus dla ogólnych PCAP jest zaliczony. Pozostały testy Grove/USB, nowy handshake oraz porównanie pliku w Wiresharku. |
| 2026-08-08 | `Sync new` Monster SD → Tab5 SD | Na stronach Handshakes i PCAP Captures dodano zbiorczą synchronizację. Worker tworzy jedną sesję `JanOS-Admin`, sprawdza każdy plik przez `/api/list`, pomija wpisy już obecne i pobiera wyłącznie brakujące. Trwały indeks `.monster_sync.tsv` jest osobny dla Grove, USB i MBus; wpis powstaje dopiero po zweryfikowanym `.part → final`, `fflush` i `fsync`. Pojedyncze `COPY TO TAB5` także aktualizuje indeks. | Kontrola statyczna przepływu, granic tablicy 256 pozycji, alokacji snapshotu w PSRAM, walidacji lokalnego pliku i ścieżek cleanup; bez kompilacji zgodnie z decyzją użytkownika. Test sprzętowy MBus `Sync new` pozostaje otwarty. | JanOS pozostaje bez zmian. Logika indeksu jest obecnie w adapterze Tab5 i przy porcie CoreS3 powinna zostać wydzielona do niezależnego komponentu. |
| 2026-08-08 | TODO JanOS — nazwy MITM PCAP | Dodano wymaganie nazwy `YYYY-MM-DD_HH-MM-SS_<SSID>_pcap<N>.pcap` oraz fallbacku `<SSID>_pcap<N>.pcap`, gdy czas nie jest wiarygodny. Numer ma rosnąć osobno dla SSID według największego istniejącego sufiksu, bez ponownego używania luk. Uwzględniono sanityzację FAT, ukryte SSID i plan testów. | Zmiana wyłącznie dokumentacyjna; kod JanOS nie został zmodyfikowany ani skompilowany. | Listing i `Sync new` Tab5 filtrują po rozszerzeniu `.pcap`, więc nowe nazwy nie wymagają zmiany protokołu ani UI CoreS3. |
| 2026-08-08 | Plan `Zeek-derived PCAP Summary` | Dodano osobny tor badawczo-implementacyjny: referencyjne przetwarzanie offline Zeek, mapowanie conn/DNS/HTTP/TLS/weird/files, stabilne API metryk, reduktory, angielski renderer oraz testy deterministyczności, odporności i wydajności. Przed portem na P4 wymagana jest bramka wykonalności, aby nie przenosić pełnego runtime Zeek. | Plan z załącznika użytkownika został włączony do TODO i opisany w sekcji 22; bez zmian kodu i bez kompilacji. | Wynik ma być niezależnym komponentem summary. CoreS3 może korzystać z tego samego kontraktu, lecz prawdopodobnie z mniejszym zestawem dekoderów i limitów top-N. |
| 2026-08-08 | Lokalny `PCAP Viewer` MVP | Dodano niezależny komponent `pcap_reader`, strumieniowy indeks classic PCAP w PSRAM, dekodery Ethernet/802.11/802.15.4 oraz szósty kafel `PCAP Viewer`. Lokalna biblioteka skanuje `/sdcard/lab/pcaps`, a ekran capture pokazuje podsumowanie, strony po 20 pakietów i popup HEX/ASCII. Analiza lokalnego Zeeka ustaliła granicę: Zeek pozostaje oracle, bez portowania event/script/log runtime do firmware. | Kontrola statyczna API, obsługi endian i timestampów, granic rekordów, uciętego ogona, limitów indeksu oraz lifecycle LVGL/task; `git diff --check`. Bez kompilacji i bez testu sprzętowego zgodnie z decyzją użytkownika. | JanOS i repo Zeeka pozostały niezmienione. `pcap_reader` nie zależy od LVGL, więc może być współdzielony z CoreS3 przy innym adapterze UI i niższym limicie indeksu. |
| 2026-08-08 | PCAP Viewer — poprawka kompilacji | GCC z `-Werror=format-truncation` wykrył, że `dirent.d_name` może mieć 255 znaków, a skrócona nazwa prezentacyjna ma 96 bajtów. Zastąpiono nieograniczone `snprintf("%s")` jawnym `strnlen + memcpy + NUL`, zachowując pełną ścieżkę osobno i gwarantując zakończenie tekstu. | Poprawka wynika z logu kompilatora użytkownika na ESP-IDF 5.4.1; wykonano kontrolę statyczną pozostałych nowych kopii nazw i `git diff --check`. Codex nie uruchamiał kompilacji. | Zmiana jest przenośna i nie dotyczy JanOS; ten sam bezpieczny wzorzec można zachować w CoreS3. |
| 2026-08-08 | Filtry oraz Zeek-inspired Summary | Rozszerzono `pcap_reader` o bitowe klasyfikacje i strukturalny DNS: ID, QR, QTYPE, RCODE, kompresowane nazwy oraz pierwszą odpowiedź A/AAAA/CNAME/NS/PTR/MX. Dodano niezależny `pcap_summary` z ograniczonymi tabelami Top-N, licznikami ruchu, protokołów, endpointów, portów, ramek Wi-Fi i DNS. Viewer buduje klasyfikacje w tle, ma dziesięć filtrów oraz popupy `Overview` i `DNS Summary`. | Kontrola statyczna granic nazw DNS i pointer loop, pojemności tablic, deterministycznego sortowania, sygnałów `approximate`, lifecycle alokacji PSRAM, filtrowania bez ponownego skanowania SD i `git diff --check`. Bez kompilacji zgodnie z decyzją użytkownika; test sprzętowy pozostaje otwarty. | JanOS i Zeek pozostały niezmienione. Backend nie zależy od LVGL; CoreS3 może obniżyć limity Top-N i liczbę indeksowanych pakietów bez zmiany parsera. |
| 2026-08-08 | Kompaktowa tabela pakietów | Zastąpiono duże, wielowierszowe kafle stałymi wierszami 40 px w układzie przypominającym Wiresharka. Dodano osobny nagłówek kolumn `No./Relative time`, `Protocol`, `Source`, `Destination`, `Info`, `Len`, naprzemienne tło, pojedynczy separator i kropkowanie długich wartości. Cały wiersz pozostaje dotykalny i otwiera ten sam popup HEX/detail. | Kontrola statyczna szerokości kolumn dla panelu 1280 px, wysokości dotyku, zachowania flex-grow dla `Info`, paginacji i filtrów; `git diff --check`. Bez kompilacji zgodnie z decyzją użytkownika. | Zmiana dotyczy tylko adaptera LVGL Tab5. CoreS3 wymaga osobnego układu z mniejszą liczbą kolumn, bez zmiany readera ani summary. |
| 2026-08-09 | Korekta tabeli i filtrów PCAP | Powiększono pasek filtrów z 50 do 62 px, tworząc osobną przestrzeń pod poziomy scrollbar i nadając mu cienki cyjanowy styl. Każdy filtr pokazuje teraz nazwę oraz liczbę pasujących indeksowanych pakietów. Pełne adresy IPv6 w kolumnach Source/Destination są deterministycznie łamane po czwartym heks­tecie i ograniczone do dwóch linii, aby nie wychodziły poza ramkę. | Kontrola statyczna: liczniki korzystają z gotowych flag i nie dekodują ponownie pakietów; ALL odpowiada liczbie indeksowanych rekordów; IPv4/MAC pozostają jednoliniowe; sprawdzono rozmiary buforów oraz style LVGL. Bez kompilacji zgodnie z decyzją użytkownika. | Zmiana dotyczy tylko UI Tab5. Ogólny helper liczników może zostać użyty w CoreS3, lecz szerokości, wysokość paska i sposób prezentacji IPv6 wymagają osobnego layoutu 320×240. |
| 2026-08-09 | Analiza licznika live MITM | JanOS już zlicza `pcap_capture_frame_count` po zapisaniu rekordu PCAP i `pcap_capture_drop_count` przy przepełnieniu kolejki. Task ARP co pięć rund, czyli zwykle około 10 s, wysyła przez UART `ARP spoof: ... captured N, dropped M`; `stop` wysyła końcowe `PCAP saved: ...`. Tab5 po odczytaniu nazwy pliku nie ma obecnie taska monitorującego te linie. | Inspekcja przepływu `start_pcap net`, hooków RX/TX, writera, taska ARP, `cmd_stop` oraz popupu MITM Tab5. Bez zmian kodu i bez kompilacji. | MVP licznika można wykonać wyłącznie po stronie Tab5 dla sesji z aktywnym ARP spoof. Gwarantowana, częstsza i pełna telemetria wymaga małej zmiany JanOS. |
| 2026-08-09 | Test sprzętowy UI PCAP i koncepcja `ESPShark` | Zdjęcia z Tab5 potwierdziły indeksowanie pliku 6169 rekordów z limitem 4096, liczniki filtrów, dwuwierszowe IPv6, tabelę pakietów, `Overview` i `DNS Summary`. Ustalono nazwę roboczą kafla `ESPShark` dla wspólnego wejścia do plików, sesji live i diagnostyki. Przeanalizowano trzy poziomy Live: UART stats, near-live z fragmentów oraz surowy stream pakietów. | Test wizualny na urządzeniu oraz inspekcja JanOS `start_admin_portal`, `wifi_connect`, hooków PCAP i HTTP download. Obecny `wifi_connect` deinitializuje Wi-Fi i niszczy AP netif, więc istniejący portal nie może bez zmian pozostać aktywny podczas MITM. Bez zmian firmware i bez kompilacji. | Offline `ESPShark` pozostaje w całości po stronie Tab5. Pełny Live wymaga rozszerzenia JanOS i nowego odbiornika strumienia; UART pozostaje sterowaniem, nie transportem surowego ruchu. |
| 2026-08-09 | Hub `ESPShark` i synchronizacja źródeł | Dwa osobne kafle `PCAP Captures` i `PCAP Viewer` zastąpiono jednym wejściem `ESPShark`. Nowy ekran pozwala wybrać `READ FROM MONSTER` albo `OPEN FROM TAB5`. Lista Monstera pokazuje dla każdego PCAP `MONSTER: AVAILABLE` oraz `TAB5: SYNCED/NOT COPIED`; przycisk `Sync all` zachowuje dotychczasową deduplikację i kopiuje tylko brakujące lub zmienione pliki. Nawigacja obu bibliotek wraca do huba. | Status TAB5 jest wyliczany w tasku listingu z trwałego `.monster_sync.tsv` i potwierdzany rozmiarem istniejącego pliku lokalnego. `Sync all` wykonuje potem mocniejszą kontrolę aktualnej ścieżki i rozmiaru przez API Monstera. Kontrola statyczna i `git diff --check`; bez kompilowania. | Bez zmian JanOS. Widok posiada też nieaktywną zapowiedź `Network Health / Threat Analysis`; implementacja detekcji pozostaje następnym etapem. |
| 2026-08-09 | Cache i eksport ESPShark | Dodano komponent `pcap_analysis_store`. Pierwsza analiza zapisuje wersjonowany cache indeksu, flag i Summary w ukrytym katalogu `.espshark/cache`; kolejne otwarcie może ominąć skan i dekodowanie całej próbki. Biblioteka oznacza pliki `ANALYZED`. Popup `CACHE / EXPORT` obsługuje JSON report, filtered PCAP, zapis/odczyt ostatniego profilu filtra, usunięcie cache i wymuszone `REANALYZE`. | Cache weryfikuje schema/rozmiary struktur, ścieżkę, size, mtime, szybki CRC początku/końca, CRC nagłówka i payloadu oraz granice offsetów. Zapisy cache/report/profile są atomowe przez `.tmp`, `fflush`, `fsync` i rename. Eksport PCAP kopiuje oryginalny global header i kompletne rekordy pasujące do aktywnego filtra. Kontrola statyczna bez kompilacji. | Bez zmian JanOS. JSON jest przenośnym formatem, cache pozostaje wewnętrzny i zależny od wersji analizatora. Import pełnego raportu jako read-only pozostaje kolejnym etapem. |

### 20.2. Szczegóły implementacji transferu MVP — 2026-08-08

Repozytorium i gałąź: `M5MonsterC5-Tab5`, `development`.

Zmodyfikowane lub dodane pliki:

- `components/janos_file_transfer/include/janos_file_transfer.h` — publiczne API backendu;
- `components/janos_file_transfer/janos_file_transfer.c` — zapytanie `/api/list`, download HTTP, zapis i walidacja;
- `components/janos_file_transfer/CMakeLists.txt` — zależności `esp_http_client`, `fatfs` i `json`;
- `main/CMakeLists.txt` — dołączenie komponentu do aplikacji;
- `main/main.c` — adapter transportu Monster, automatyzacja Wi-Fi oraz popup LVGL;
- `docs/PCAP_Analysis_and_Implementation_Plan.md` — stan TODO i ten dziennik.

Zaimplementowany przepływ:

1. Na liście `Captured Handshakes` plik otrzymał przycisk `Copy`.
2. Tab5 blokuje start, gdy jego SD nie jest zamontowana, działa lokalny captive portal albo trwa ręczna sesja SD Admin.
3. Worker przejmuje właściwy transport Grove, USB albo MBus, wysyła `stop` i czeka na odpowiedź Monstera.
4. Tab5 generuje tymczasowe hasło, wysyła `start_admin_portal <password>` i czeka na potwierdzenie `JanOS-Admin`.
5. Wewnętrzny ESP32-C6 zapisuje poprzedni tryb oraz konfigurację STA i łączy się z `JanOS-Admin`.
6. Backend odczytuje rozmiar pliku przez `/api/list`, sprawdza wolne miejsce i pobiera `/api/download` porcjami po 8192 bajtów.
7. Dane trafiają do `/sdcard/lab/pcaps/imported/<grove|usb|mbus>/<nazwa>.pcap.part`; konflikt nazwy tworzy kolejny sufiks zamiast nadpisania.
8. Podczas zapisu liczony jest lokalny CRC32. Następnie wykonywane są `fflush()`, `fsync()` i `fclose()`.
9. Backend ponownie odpytuje `/api/list` i wymaga zgodności rozmiaru przed transferem, po transferze oraz liczby zapisanych bajtów.
10. Dopiero po walidacji plik `.part` jest atomowo zmieniany na nazwę końcową. Oryginał na Monsterze nigdy nie jest usuwany.
11. Po sukcesie, błędzie lub anulowaniu Tab5 zatrzymuje tymczasowy portal i odtwarza poprzednią konfigurację Wi-Fi.

Weryfikacja:

- kompilacja projektu ESP-IDF 5.4.1 zakończyła się powodzeniem;
- wynikowy obraz miał rozmiar `0x2c5500`, a najmniejsza partycja aplikacji zachowała 72% wolnego miejsca;
- po decyzji użytkownika kolejne kompilacje i flashowanie wykonuje wyłącznie użytkownik;
- test fizyczny skopiował handshake PCAP z Monstera do Tab5: 996 bajtów, lokalny CRC32 `0b3ee871`, wynik backendu `ESP_OK`;
- do potwierdzenia po tej zmianie pozostaje brak rebootu przy końcowym usuwaniu taska oraz otwarcie obu kopii w Wiresharku.

Znane ograniczenia:

- obecne API JanOS udostępnia download tylko z drzewa `/sdcard/lab`; starsze pliki spoza tego drzewa nie zostaną pobrane;
- CRC32 jest obecnie liczone tylko lokalnie, ponieważ JanOS nie zwraca zdalnego CRC; ochronę przed zmianą pliku zapewnia porównanie rozmiaru przed i po transferze;
- przerwany transfer celowo pozostawia `.part`, finalny `.pcap` nie powstaje;
- popup nie pokazuje jeszcze prędkości, ETA, źródła ani przycisku `Open`;
- obsługa utraty AP i anulowania na każdym etapie wymaga testów sprzętowych;
- rozpoczęcie transferu wysyła uniwersalne `stop`, więc kończy aktualną operację na wybranym Monsterze.

Wpływ na CoreS3:

- katalog `components/janos_file_transfer` można przenieść bez zmian, jeżeli CoreS3 korzysta z ESP-IDF z `esp_http_client`, FATFS i cJSON;
- mount point, rozmiar bufora, timeout, ścieżki i callback są konfigurowalne;
- komponent nie zna LVGL, `tab_context_t`, rozdzielczości ekranu ani numerów UART;
- do napisania dla CoreS3 pozostają adapter uruchamiania portalu/Wi-Fi oraz osobny, mniejszy ekran postępu.

Następny krok: test fizyczny pojedynczego handshake zgodnie z poniższą procedurą, a potem uzupełnienie kodów błędów i danych prędkość/ETA.

Procedura testu sprzętowego:

1. Włożyć karty SD do Tab5 i wybranego Monstera.
2. Utworzyć lub wskazać handshake widoczny na ekranie `Captured Handshakes`.
3. Nacisnąć `Copy`, obserwować start `JanOS-Admin`, połączenie C6 i postęp pobierania.
4. Sprawdzić plik końcowy w `/sdcard/lab/pcaps/imported/<źródło>/` oraz zachowanie oryginału na Monsterze.
5. Otworzyć oba pliki w Wiresharku na komputerze i porównać rozmiar oraz lokalny hash po skopiowaniu kart.
6. Powtórzyć test z `Cancel`, odłączeniem AP i brakiem miejsca; w żadnym przypadku nie może powstać fałszywy finalny `.pcap`.

### 20.3. Naprawa łącza Tab5 P4–C6 — 2026-08-08

Objaw sprzętowy: po wybraniu `Copy` lista handshake była odczytywana prawidłowo przez UART, lecz transfer kończył się przed połączeniem z `JanOS-Admin`. ESP-Hosted wielokrotnie resetował slave na GPIO12 i zwracał `ESP-Hosted link not yet up` oraz `ESP_FAIL`.

Przyczyna: commit `c1881dcd` zmienił host ESP32-P4 z SDIO na SPI, mimo że wewnętrzny ESP32-C6 Tab5 oraz obraz `wifi_c6_fw/ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin` używają SDIO.

Zmiana w `sdkconfig`:

- przywrócono `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y`;
- wyłączono host SPI;
- wybrano SDIO slot 1, magistralę 4-bit i zegar 40 MHz;
- przywrócono piny Tab5: CMD 13, CLK 12, D0 11, D1 10, D2 9, D3 8;
- przywrócono reset ESP32-C6 na GPIO15 i opóźnienie resetu 1500 ms;
- przywrócono progi buforowania danych właściwe dla SDIO;
- zsynchronizowano także wygenerowane aliasy opcji w sekcji deprecated ESP-Hosted.

Poza zakresem zmiany:

- kod JanOS i firmware Monstera;
- UART MBus i Grove;
- backend HTTP transferu oraz zapis na SD;
- firmware ESP32-C6 — jego ponowne flashowanie nie powinno być potrzebne, jeżeli zachował fabryczny obraz SDIO.

Weryfikacja statyczna: konfiguracja odpowiada wcześniejszej konfiguracji Tab5 i dołączonemu obrazowi C6. Nie wykonywano kompilacji. Test sprzętowy powinien potwierdzić pojawienie się `Transport active`, połączenie z `JanOS-Admin`, pobieranie HTTP i finalny plik bez rozszerzenia `.part`.

#### Backport zabezpieczenia przed panic SDIO

Pierwszy test sprzętowy potwierdził poprawne zestawienie SDIO i identyfikację ESP32-C6, ale ESP-Hosted 2.8.5 wykonał `assert(pkt_rxbuff)` w `sdio_push_data_to_queue`, gdy alokacja bufora z internal/DMA RAM chwilowo zwróciła `NULL`. Oficjalny changelog ESP-Hosted wskazuje, że twardy assert przy braku bufora został zastąpiony łagodną obsługą dopiero w wersji 2.12.0.

Do śledzonego lokalnie komponentu 2.8.5 przeniesiono minimalną część tej poprawki bez aktualizacji całego protokołu i bez zmiany firmware C6:

- brak bufora pojedynczego pakietu RX nie uruchamia już panic/rebootu;
- sterownik pomija tylko pakiet, którego nie może bezpiecznie umieścić w kolejce;
- logi `SDIO RX mempool exhausted` i `SDIO RX mempool recovered` pokazują początek i koniec presji pamięci;
- powiększenie bufora stream wykonuje alokację przed zwolnieniem starego bufora;
- nieudane powiększenie zachowuje poprzedni bufor i wraca do pętli zamiast wykonywać assert.

#### Backport zabezpieczenia TX i potwierdzenie sieci Monstera

Drugi test sprzętowy potwierdził kolejno `Transport active`, `WiFi initialized successfully via ESP-Hosted`, `esp_wifi_remote_connect` oraz `Station mode: Connected`. Oznacza to, że wewnętrzny ESP32-C6 Tab5 prawidłowo dołączył warstwą Wi-Fi do tymczasowego AP `JanOS-Admin`, który chwilę wcześniej został uruchomiony na Monsterze komendą UART. Nie był to captive portal Tab5 ani przypadkowa zapamiętana sieć, ponieważ worker przed `esp_wifi_connect()` wpisuje do konfiguracji STA dokładnie SSID `JanOS-Admin` oraz losowe hasło tej samej sesji przekazane Monsterowi.

Reboot nastąpił dopiero przy pierwszym DHCP Discover. Stos prowadził przez `dhcp_discover`, `low_level_output` i `transport_drv_sta_tx`, gdzie alokacja bufora 0x680 bajtów z internal/DMA RAM zwróciła `NULL`, a ESP-Hosted 2.8.5 wykonał `assert(copy_buff)`. Jest to analogiczna luka do wcześniej naprawionej ścieżki RX.

Lokalny backport TX:

- zarówno STA, jak i AP zwracają do `esp_netif` kontrolowany błąd braku bufora zamiast restartować cały ESP32-P4;
- `errno = -ENOBUFS` pozwala stosowi sieciowemu odroczyć lub ponowić pakiet;
- log jest ograniczony do początku presji pamięci oraz komunikatu o odzyskaniu puli;
- jeden współdzielony bufor DMA jest rezerwowany przez sterownik SDIO już podczas tworzenia kanału STA, przed `setup_transport`; początek `bus_init` ponawia próbę, jeśli pierwsza alokacja się nie udała;
- pakiety STA/AP są kolejkowane jako dokładne kopie w general heap/PSRAM, a szeregowy `sdio_write_task` kopiuje je do wspólnego DMA dopiero w chwili wysyłania;
- aplikacja loguje `Connecting internal C6 to Monster AP JanOS-Admin`, a po DHCP `JanOS-Admin connected, IP=...`, bez ujawniania hasła sesyjnego.

Trzeci test ujawnił, że rezerwacja TX podczas samego tworzenia kanału była zbyt wczesna: pierwsza ramka INIT nie dostała bufora RX, a kolejna próba inicjalizacji SDMMC nie miała już pamięci DMA. Dlatego kopia odebranej ramki — po zakończeniu właściwego odczytu DMA do bufora stream — może teraz awaryjnie korzystać z general heap. Na Tab5 normalny allocator kieruje takie większe, niedma-owe kopie przede wszystkim do PSRAM. Bufory używane bezpośrednio przez SDIO nadal pozostają w internal/DMA RAM.

Czwarty test potwierdził skuteczność tego fallbacku: INIT i RPC przeszły, C6 uruchomił STA i połączył się z `JanOS-Admin`, ale po RPC nie istniał już ciągły blok 1664 B dla pierwszego DHCP Discover. Ostateczna kolejność łączy oba zabezpieczenia: jeden bufor TX jest odkładany wcześnie, a ramki INIT/RPC w razie presji używają general heap. Poprzednie dwa bufory były zbyt kosztowne; jeden jest wystarczający dla DHCP, a późniejsze chwilowe spiętrzenie pakietów nadal obsługuje kontrolowany `ESP_ERR_ESP_NETIF_*`.

Piąty test wykazał, że sam jeden blok nie wystarczy, jeżeli należy do osobnej puli STA. Po odebraniu INIT host musi najpierw wysłać kontrolną odpowiedź przez `sdio_write_task`; bufor uwięziony w puli STA był dla tego tasku niewidoczny i wywołał `assert(sendbuf)`. Docelowa implementacja ma więc jednego właściciela DMA — szeregowy writer SDIO. Odpowiedź INIT, RPC, DHCP i HTTP używają tego samego bloku kolejno, nigdy równolegle. Dane oczekujące oraz CPU-side kopie RX żyją w general heap, który na Tab5 może korzystać z ponad 8 MB PSRAM. `send_slave_config` także nie rezerwuje już osobnego bufora DMA.

Szósty test przeszedł cały INIT i asocjację, lecz ujawnił drugi rodzaj długowiecznego DMA: surowe bufory, do których SDIO fizycznie odbiera stream. Próba późniejszego powiększenia jednego z nich do 1024 B nie znalazła już ciągłego bloku i zgubiła odpowiedzi RPC/DHCP. Oba bufory double-buffer są teraz od razu alokowane w maksymalnym rozmiarze 1536 B razem ze wspólnym TX, zanim powstaną kolejki i taski. Przy `bus_deinit` są jawnie zwalniane; poprzedni kod ich nie zwalniał.

Weryfikacja AP po stronie JanOS: `cmd_start_admin_portal` ustawia `ssid="JanOS-Admin"`, kanał 1, `WIFI_AUTH_WPA2_PSK`, czterech klientów i nie ustawia `ssid_hidden`, więc wartość pozostaje 0. Worker Tab5 wysyła tę komendę przez aktualny transport Monstera i nie przechodzi do C6, dopóki nie odbierze `Admin portal started...` albo `Portal already running`. Nowe logi rozróżniają `command sent` oraz `Monster confirmed JanOS-Admin AP is running`; hasło sesyjne pozostaje ukryte.

Siódmy test spełnił warunek transportu end-to-end: pojawiły się potwierdzenie portalu, `Station mode: Connected`, DHCP `172.0.0.2`, zapis pliku 996 B z CRC32 `0b3ee871` i końcowe `ESP_OK`. Reset nie dotyczył już SDIO, Wi-Fi, HTTP ani FATFS. Nastąpił 28 ms później w idle tasku podczas usuwania zakończonego workera: `vPortTLSPointersDelCb` uznał prawidłowy callback `pthread_cleanup_thread_specific_data_callback` pod `0x4800705e` za niewykonywalny. Mapa linkera pokazuje, że ten adres leży w kodzie przeniesionym do PSRAM przez `CONFIG_SPIRAM_XIP_FROM_PSRAM`; test `esp_ptr_executable()` w ESP-IDF 5.4.1 nie obejmuje tego zakresu w konfiguracji dual-core P4.

Naprawa nie wyłącza XIP i nie omija destruktorów. Worker, po zakończeniu wszystkich wywołań socket/HTTP i przywróceniu Wi-Fi, jawnie wywołuje ten sam cleanup TLS, którego używają taski utworzone przez pthread. Cleanup zwalnia per-task semafor LwIP i zeruje wskaźnik oraz callback TLS, zanim `vTaskDelete()` przekaże TCB do idle taska. Zależność `pthread` została dopisana jawnie do komponentu `main`, a prywatny symbol IDF jest użyty wyłącznie pod warunkiem obu opcji konfiguracyjnych powodujących ten przypadek.

Warunek zaliczenia następnego testu: po `Saved ...`, rozłączeniu C6 i `Monster file transfer finished: ESP_OK` urządzenie ma pozostać uruchomione, popup ma pokazać `Transfer complete`, a kolejny transfer w tej samej sesji także ma się zakończyć bez `TLSP deletion callback` i rebootu.

#### Gdzie trafia capture MITM i jak go przetestować

Ekran MITM łączy radio Monstera z wybraną siecią, a następnie wysyła `start_pcap net`. JanOS tworzy `/sdcard/lab/pcaps/sniff_N.pcap`, gdzie `N` jest pierwszym wolnym numerem. Jest to classic PCAP 2.4 z linktype 1 (Ethernet); `start_pcap radio` używa tego samego katalogu i nazw, ale linktype 105 (IEEE 802.11). Przycisk `STOP` wysyła ogólne `stop`, po którym writer opróżnia kolejkę, wykonuje `fflush`, zamyka plik, synchronizuje SD i loguje `PCAP saved: ...`.

Tab5 ma teraz w `Compromised Data` osobny kafel `PCAP Captures`. Strona wywołuje `list_dir /sdcard/lab/pcaps`, filtruje pliki `.pcap` i przekazuje wybraną pełną ścieżkę do istniejącego workera transferu. W nagłówku dostępne jest `Copy latest`, a każdy wiersz ma jawny `COPY TO TAB5`. JanOS i protokół HTTP nie wymagają modyfikacji.

Test MBus zaliczony: strona znalazła dwa capture'y, a `sniff_2.pcap` został zapisany jako `/sdcard/lab/pcaps/imported/mbus/sniff_2.pcap` (938 B, CRC32 `a79930bf`). Po `Monster file transfer finished: ESP_OK` Tab5 pracował dalej bez `TLSP deletion callback`, panicu i rebootu. Kolejne testy: nowy handshake oraz ten sam przepływ przez Grove i USB. Istniejący mechanizm unikalnej nazwy zapobiegnie nadpisaniu wcześniejszej lokalnej kopii.

Drugi panic widoczny już w bootloaderze wystąpił po automatycznym reboocie. Core dump do flash jest wyłączony, więc kod nie zapisywał zrzutu do partycji aplikacji. Jeżeli urządzenie nie wystartuje po pełnym odłączeniu i ponownym podaniu zasilania, należy ponownie wgrać obraz Tab5; nie wymaga to flashowania JanOS ani ESP32-C6.

### 20.4. Synchronizacja tylko nowych capture’ów — 2026-08-08

Zakres pierwszej wersji obejmuje dane przechwycone, które już są widoczne w `Compromised Data`: `/sdcard/lab/handshakes/*.pcap` oraz `/sdcard/lab/pcaps/*.pcap`. Nie synchronizuje całego drzewa konfiguracyjnego, HTML ani wardrive, ponieważ te dane mają inne reguły stanu i retencji. JanOS nie został zmodyfikowany.

Przepływ `Sync new`:

1. Po załadowaniu listy Handshakes albo PCAP Captures nagłówek pokazuje zielony przycisk `Sync new`.
2. Callback kopiuje bieżącą listę maksymalnie 256 plików do PSRAM, więc późniejsze przeładowanie UI nie może zmienić kolejki pracującego taska.
3. Worker wysyła `stop`, uruchamia jeden tymczasowy `JanOS-Admin` i tylko raz łączy wewnętrzny C6 Tab5 z AP Monstera.
4. Dla każdego pliku pobiera aktualny rozmiar przez `/api/list`.
5. Plik jest uznawany za zsynchronizowany tylko wtedy, gdy indeks zawiera zgodne `remote_path + remote_size`, a wskazany lokalny plik nadal istnieje i ma identyczny rozmiar.
6. Jeżeli indeks jeszcze nie istnieje, ale wcześniejszy plik z pojedynczego transferu ma kanoniczną nazwę i zgodny rozmiar, Tab5 dopisuje go do indeksu bez ponownego pobierania.
7. Brakujący plik jest pobierany istniejącym backendem: unikalna nazwa, zapis `.part`, lokalny CRC32, kontrola wolnego miejsca, porównanie rozmiaru przed i po pobraniu oraz atomowe `rename`.
8. Dopiero po sukcesie transferu Tab5 dopisuje rekord indeksu i wykonuje `fflush` oraz `fsync` indeksu.
9. Błąd jednego pliku nie zatrzymuje pozostałej kolejki. Anulowanie zatrzymuje kolejkę, zamyka portal i przywraca wcześniejsze Wi-Fi.
10. Popup pokazuje bieżący plik, numer `N/total`, postęp oraz podsumowanie `Copied | Already synced | Failed`.

Indeks dla każdego fizycznego transportu jest zapisany osobno:

```text
/sdcard/lab/pcaps/imported/grove/.monster_sync.tsv
/sdcard/lab/pcaps/imported/usb/.monster_sync.tsv
/sdcard/lab/pcaps/imported/mbus/.monster_sync.tsv
```

Format rekordu:

```text
remote_size<TAB>local_crc32<TAB>remote_path<TAB>local_path
```

Znane ograniczenia bez zmian JanOS: endpoint `/api/list` zwraca nazwę, typ i rozmiar, ale nie zwraca czasu modyfikacji ani CRC32. Plik zmieniony w miejscu bez zmiany długości może więc zostać uznany za ten sam. Indeks jest przypisany do złącza Grove, USB albo MBus, dlatego po fizycznej zamianie dwóch Monsterów używających tego samego złącza należy traktować pierwszy sync jako potencjalnie niepełny. Docelowe rozwiązanie to zdalny CRC32 oraz stabilny `monster_id` po stronie JanOS; obecna wersja jest zgodna z wymaganiem kopiowania nowych capture’ów i nie wymaga zmian firmware Monstera.

Plan testu sprzętowego MBus:

1. Zostawić na Monsterze co najmniej jeden PCAP już skopiowany na Tab5 i utworzyć drugi, nowy plik.
2. Otworzyć odpowiednią stronę w `Compromised Data` i nacisnąć `Sync new`.
3. Oczekiwany pierwszy wynik: `Copied: 1 | Already synced: 1 | Failed: 0` oraz jeden start i jedno zamknięcie `JanOS-Admin`.
4. Uruchomić `Sync new` ponownie bez tworzenia pliku. Oczekiwany wynik: `Copied: 0 | Already synced: 2 | Failed: 0`; na konsoli nie powinien pojawić się nowy log `Saved ...`.
5. Utworzyć kolejny handshake lub `sniff_N.pcap` i potwierdzić, że trzecie uruchomienie pobiera tylko ten jeden plik.
6. Sprawdzić brak rebootu po cleanupie taska oraz obecność `.monster_sync.tsv` w katalogu źródła na SD Tab5.

### 20.5. Lokalny PCAP Viewer MVP — 2026-08-08

Pierwsza działająca warstwa przeglądarki znajduje się całkowicie po stronie Tab5. JanOS nie został zmieniony. Po skopiowaniu pliku przez `COPY TO TAB5` albo `Sync new` użytkownik otwiera `Compromised Data → PCAP Viewer`, wybiera lokalny capture i przegląda pakiety bez ponownego łączenia z Monsterem.

#### Decyzja po analizie Zeeka

Zeek otwiera plik offline w `src/iosource/pcap/Source.cc` przez `pcap_fopen_offline()`, pobiera następne rekordy przez `pcap_next_ex()`, tworzy `Packet`, a `src/RunState.cc` przekazuje go do `packet_mgr->ProcessPacket()` i opróżnia kolejkę eventów. Dalej uruchamiane są analizatory pakietów i protokołów, event engine, runtime skryptów oraz manager logów.

Pełny runtime nie jest dobrym kandydatem do bezpośredniego portu na ESP32-P4. Jego build wymaga między innymi libpcap, OpenSSL 3, Zlib, Flex, Bison, Pythona na etapie budowania oraz opcjonalnego runtime Spicy/HILTI. Najbardziej kosztowna jest jednak nie sama biblioteka PCAP, lecz graf analizatorów, eventów, typów, skryptów i logowania. Przyjęta granica jest więc następująca:

| Część Zeeka | Decyzja dla Tab5 |
|---|---|
| lifecycle wejścia offline i przetwarzanie rekord po rekordzie | `reference`: zachować model strumieniowy |
| libpcap | `exclude` w firmware MVP: mały parser classic PCAP nie wymaga zewnętrznego runtime |
| `Packet` i packet analyzer manager | `adapt concept`: lekki rekord indeksu i jawne dekodery linktype |
| analizatory Ethernet/IP/TCP/UDP/DNS/HTTP/TLS | `adapt semantics`: implementować tylko potrzebne, ograniczone dekodery |
| event engine, skrypty Zeek, logging manager, Broker i Spicy | `exclude` z firmware MVP |
| pełny Zeek uruchomiony na komputerze | `reference/oracle` dla przyszłych testów summary |

Repo Zeeka pozostaje niezmodyfikowane na commicie `32fc38c1e1`. Licencja główna jest 3-klauzulową licencją BSD z obowiązkiem zachowania informacji copyright i warunków przy redystrybucji. Obecny `pcap_reader` jest własną implementacją i nie kopiuje kodu Zeeka.

#### Implementacja w Tab5

Nowy komponent `components/pcap_reader` nie zależy od LVGL. Udostępnia otwarcie pliku, skan rekordów, odczyt wybranego payloadu oraz lekki opis pakietu. Plik jest czytany strumieniowo z SD; payloady całego capture nie są kopiowane do RAM.

Obsługiwane obecnie:

- classic PCAP 2.4, little-endian i big-endian;
- timestampy mikrosekundowe i nanosekundowe;
- walidacja snaplen, caplen, długości rekordu, zakresu pliku i uciętego końca;
- linktype 1: Ethernet, do dwóch tagów VLAN, ARP, IPv4, IPv6, TCP, UDP i podstawowe rozpoznanie aplikacji;
- linktype 105: IEEE 802.11 management/control/data, LLC/SNAP i EAPOL;
- linktype 230: podstawowe IEEE 802.15.4 bez FCS;
- linktype 283: IEEE 802.15.4 TAP z przejściem do dekodera MAC;
- podpowiedzi DNS, DHCP, HTTP, TLS, mDNS, SSDP i NTP;
- kontrolowane rozpoznanie PCAPNG jako formatu jeszcze nieobsługiwanego.

Stan widoku i lista maksymalnie 128 plików są alokowane preferencyjnie w PSRAM. Skanowanie odbywa się w osobnym tasku i tworzy w PSRAM indeks maksymalnie 4096 pierwszych pakietów. Nawet po osiągnięciu tego limitu skan jest kontynuowany, aby policzyć wszystkie rekordy i bajty. UI pokazuje 20 wpisów na stronę, więc liczba obiektów LVGL nie rośnie razem z rozmiarem pliku. Pakiety są prezentowane jako kompaktowa tabela z wierszami 40 px, a nie jako osobne wysokie kafle.

Ekran capture pokazuje:

- format, endian, rozdzielczość czasu, linktype, snaplen i rozmiar pliku;
- liczbę wszystkich oraz zindeksowanych pakietów i czas trwania;
- ostrzeżenia `INDEX LIMITED` oraz `TRUNCATED TAIL`;
- stałe kolumny `No./Relative time`, `Protocol`, `Source`, `Destination`, `Info` i `Len` z jednoliniowym kropkowaniem;
- popup po dotknięciu wiersza z timestampem, nagłówkami i HEX/ASCII pierwszych 256 bajtów.

#### Filtry i Zeek-inspired Summary

Drugi etap dodaje osobny komponent `components/pcap_summary`. Komponent przyjmuje reader, indeks pakietów i opcjonalną tablicę klasyfikacji. Nie zna LVGL, nie używa runtime Zeeka i nie alokuje pamięci dynamicznie wewnątrz analizy. Wszystkie tabele mają jawne limity, a po ich przekroczeniu używają ograniczonego algorytmu Top-N oraz ustawiają flagę `approximate`.

Podczas otwierania capture task wykonuje dwie fazy:

1. skanuje cały plik i buduje indeks maksymalnie 4096 rekordów;
2. dekoduje zindeksowaną próbkę, buduje summary oraz zapisuje 32-bitową klasyfikację każdego pakietu.

Dzięki cache klasyfikacji przełączanie filtrów nie wykonuje ponownych odczytów całego pliku. Dostępne są `ALL`, `DNS`, `TCP`, `UDP`, `HTTP`, `TLS`, `ARP`, `EAPOL`, `802.11 MGMT` i `MALFORMED`. Paginacja odnosi się do pakietów pasujących do filtra, ale zachowuje oryginalne numery rekordów.

`Overview` pokazuje:

- capture packets/bytes, czas, packets/s i bitrate;
- liczbę faktycznie przeanalizowanych rekordów oraz informację o próbce;
- TCP, UDP, ARP, DNS, HTTP, TLS i EAPOL;
- malformed, snapshot truncation oraz pakiety większe od 512-bajtowego okna dekodera;
- beacon, probe request/response i deauthentication;
- Top-N protokołów, endpointów i portów.

`DNS Summary` jest wzorowany na polach `DNS::Info` Zeeka, ale pozostaje własnym, ograniczonym dekoderem. Obsługuje skompresowane nazwy z limitem skoków pointera, QTYPE, RCODE oraz pierwszą odpowiedź A, AAAA, CNAME, NS, PTR albo MX. Pokazuje queries/responses, NOERROR/NXDOMAIN/SERVFAIL, zaobserwowane unikalne domeny, typy zapytań, top domeny, odpowiedzi, klientów i serwery DNS. Długość nazwy co najmniej 50 znaków i co najmniej pięć etykiet są raportowane wyłącznie jako wskaźniki, nigdy jako automatyczny werdykt bezpieczeństwa.

Limity pierwszej wersji summary:

| Zbiór | Pojemność kandydatów |
|---|---:|
| protokoły | 16 |
| endpointy | 32 |
| porty | 24 |
| domeny DNS | 64 |
| odpowiedzi DNS | 24 |
| klienci/serwery DNS | po 16 |
| typy DNS | 16 |

Po osiągnięciu pojemności popup oznacza tabelę jako `approximate`. Summary analizuje pierwsze 4096 pakietów, natomiast podstawowe liczniki nagłówków PCAP nadal obejmują cały plik.

#### Ograniczenia MVP

- PCAPNG jest wykrywany, ale nie jest jeszcze parsowany.
- Przeglądać można pierwsze 4096 pakietów; statystyka całego pliku pozostaje poprawna.
- Warstwa aplikacyjna jest rozpoznawana heurystycznie i nie zastępuje pełnych analizatorów Wiresharka ani Zeeka.
- IPv6 extension headers, fragmentacja IP, retransmisje TCP oraz pełny TLS ClientHello/SNI nie są jeszcze analizowane.
- Mapowanie adresów dla wszystkich wariantów 802.11 oraz reguły PAN compression z IEEE 802.15.4-2015 wymagają corpus testowego.
- Operacje plikowe używają `fseek`/`ftell`; przed obsługą capture większych niż 2 GB trzeba potwierdzić szerokość `long` w toolchainie ESP-IDF.
- Nie ma jeszcze connection tracking zgodnego z `conn.log`, TCP stream reassembly, IOC ani eksportu wybranych pakietów.

#### Plan pierwszego testu sprzętowego

1. Wgrać firmware z nowym komponentem zgodnie ze zwykłą procedurą użytkownika; Codex nie wykonywał kompilacji.
2. Potwierdzić, że skopiowany `sniff_2.pcap` jest widoczny w `Compromised Data → PCAP Viewer`.
3. Otworzyć plik i porównać liczbę pakietów, linktype i długości z Wiresharkiem.
4. Otworzyć kilka wierszy, sprawdzić popup i płynność przewijania/zmiany stron.
5. Przełączyć wszystkie filtry i potwierdzić oryginalne numery pakietów oraz prawidłowy licznik `indexed match`.
6. Otworzyć `Overview` i porównać protokoły, endpointy oraz porty z Wiresharkiem.
7. Na capture MITM zawierającym DNS otworzyć `DNS Summary` i porównać domeny, QTYPE oraz RCODE z Wiresharkiem i później Zeekiem.
8. Powtórzyć test dla handshake PCAP z linktype 105; oczekiwany jest EAPOL bez DNS.
9. Dodać kontrolowane pliki little/big endian, mikro/nanosekundowe, pusty, z zerowym `caplen`, ucięty oraz PCAPNG.
10. Zmierzyć czas obu faz analizy i minimum wolnego internal RAM/PSRAM dla małego oraz dużego pliku.
11. Dopiero po teście zmienić odpowiednie checkboxy sekcji F na `[x]`.

Gotowe małe wektory z lokalnego repo Zeeka, których nie kopiujemy do firmware ani repo Tab5:

| Plik pod `testing/btest/Traces` | Rozmiar | Oczekiwany przypadek |
|---|---:|---|
| `web.pcap` | 11525 B | classic PCAP little-endian, linktype 1 Ethernet |
| `wlanmon.pcap` | 731 B | classic PCAP little-endian, linktype 105 IEEE 802.11 |
| `http/get-to-ssh-server.pcap` | 1398 B | classic PCAP little-endian z timestampami nanosekundowymi |
| `unknown-ip-short-payload.pcap` | 248 B | krótki/nieznany payload IP i kontrola odporności dekodera |
| `websocket/oversized-close-frame.pcapng` | 1300 B | kontrolowany komunikat o nieobsługiwanym PCAPNG |

Repo Zeeka nie zawiera w tym checkoutcie gotowego przykładu big-endian; taki plik oraz wariant ucięty utworzymy jako własne minimalne fixture'y testowe w osobnym etapie.

### 20.6. Szablon kolejnego wpisu

```text
Data:
Repo/branch:
Zakres TODO:
Zmodyfikowane pliki:
Zmiana zachowania:
Testy:
Wynik:
Znane ograniczenia:
Wpływ na CoreS3:
Następny krok:
```

### 20.7. Reguła zamykania TODO

Checkbox można zmienić na `[x]` dopiero, gdy:

1. kod został zaimplementowany;
2. wykonano bezpieczny test odpowiadający funkcji;
3. test zakończył się powodzeniem;
4. wpisano wynik w dzienniku implementacji;
5. opisano wpływ albo brak wpływu na CoreS3.

Jeżeli zmiana jest częściowa, zadanie pozostaje `[ ]`, a postęp zapisujemy w dzienniku jako `W toku` wraz z konkretnym pozostałym zakresem.

## 21. Proponowany commit message

Poniższy komunikat obejmuje aktualny pakiet zmian. Przed commitem należy zdecydować, czy dołączyć wygenerowane binaria z `binaries-esp32p4/`; katalogu `build/` nie należy commitować.

```text
feat(tab5): centralize Monster captures and add a local PCAP viewer

- add a reusable janos_file_transfer component using esp_http_client
- query JanOS file metadata through /api/list before and after downloads
- stream Monster SD files to Tab5 SD using atomic .part finalization
- validate free space, byte counts and local CRC32 before committing files
- preserve remote originals and support cancellation without blocking LVGL
- automate temporary JanOS-Admin startup, C6 Wi-Fi connection and cleanup
- restore the previous Tab5 Wi-Fi configuration after each transfer
- restore the Tab5 P4-to-C6 transport to the factory SDIO interface and pinout
- backport graceful SDIO RX allocation-failure handling to prevent P4 reboots
- serialize control, STA and AP writes through one reserved SDIO DMA buffer
- preallocate both full-size SDIO RX stream buffers before heap fragmentation
- queue Wi-Fi TX and streaming RX copies in general heap to leverage PSRAM
- replace ESP-Hosted RX/TX allocation asserts with controlled failures
- log the Monster admin-portal command and positive AP confirmation without exposing credentials
- log the Monster JanOS-Admin association and DHCP address during file transfer
- clean up task-local LwIP/pthread resources before deleting the transfer worker
- add a PCAP Captures tile backed by the JanOS /sdcard/lab/pcaps listing
- copy MITM and radio sniff PCAPs to Tab5 through per-file and latest actions
- add Sync new actions for handshake and general PCAP capture lists
- compare remote path and size against a persistent per-transport sync index
- reuse one JanOS-Admin session to copy only missing capture files in a batch
- persist sync entries only after verified atomic file finalization
- report copied, already-synced and failed counts in the LVGL progress popup
- document MITM capture storage and the PCAP Captures hardware test
- document the planned timestamped, SSID-aware JanOS MITM capture filenames
- require monotonic per-SSID PCAP numbering with a no-clock fallback
- add per-handshake COPY TO TAB5 actions and a Copy latest header shortcut
- improve copy-button sizing, contrast and visibility in the handshake list
- store imported captures under source-specific Grove, USB and MBus folders
- reuse JanOS saved passwords automatically for secured MITM connections
- fall back to manual MITM password entry when saved credentials fail
- fix MITM response handling across Grove, USB and MBus transports
- keep OTA completion locked during download finalization and UART silence
- enable OTA Done only after the explicit update-applied restart marker
- add a staged Zeek-derived PCAP summary extraction and validation plan
- add a reusable streaming classic-PCAP reader without a libpcap runtime
- detect little/big endian and micro/nanosecond PCAP variants safely
- retain valid packet records when the capture has a truncated tail
- index the first 4096 packets in PSRAM while scanning full-file totals
- decode Ethernet, IEEE 802.11 and IEEE 802.15.4 link-layer captures
- identify basic IP, TCP, UDP, DNS, DHCP, HTTP, TLS and EAPOL traffic
- add a local PCAP Viewer tile with paginated packet rows and file summary
- show per-packet metadata and a bounded HEX/ASCII detail popup
- replace oversized packet cards with a compact Wireshark-style table
- keep packet rows touchable while separating source, destination, info and length columns
- classify indexed packets once for instant protocol and anomaly filters
- add ALL, DNS, TCP, UDP, HTTP, TLS, ARP, EAPOL, Wi-Fi management and malformed filters
- decode compressed DNS names, query types, response codes and first answers safely
- add a bounded pcap_summary component with deterministic Top-N output
- show Zeek-inspired Overview and DNS Summary popups backed by PSRAM
- show indexed packet counts directly below every PCAP filter name
- reserve a separate visual lane for the horizontal filter scrollbar
- wrap full IPv6 endpoints at a deterministic hextet boundary
- document the existing JanOS MITM captured/dropped telemetry and Tab5 monitor plan
- report capture sampling and approximate table limits explicitly
- treat Zeek as the desktop reference instead of porting its full runtime
- document the transfer architecture, hardware tests, CoreS3 port and OTA rules

JanOS firmware remains unchanged.
```

## 22. Plan: Zeek-derived PCAP Summary dla Tab5

### 22.1. Cel i status

Status: **fundament `pcap_reader`, ograniczony `pcap_summary`, filtry oraz pierwsze ekrany `Overview`/`DNS Summary` są zaimplementowane; generator referencyjny i porównanie z pełnym Zeekiem pozostają etapem badawczym**.

Lokalne źródła referencyjne Zeek:

```text
ZEEK_ROOT=C:\Users\mati\.codex\workspace\zeek
baseline commit=32fc38c1e1
```

Celem jest uzyskanie deterministycznego, tekstowego podsumowania PCAP w języku angielskim, o średnim poziomie szczegółowości, podzielonego na:

- `Traffic`;
- `Protocols`;
- `Anomalies`;
- `IOC`.

Zeek ma być źródłem semantyki, algorytmów i wyniku referencyjnego. Analiza kodu wykazała, że pełny runtime nie powinien być uruchamiany na ESP32-P4: oprócz wejścia libpcap uruchamia packet analyzers, event engine, skrypty i logowanie oraz ma szeroki zestaw zależności systemowych. Tab5 otrzymuje własny mały parser i dekodery, a pełny Zeek na komputerze posłuży do tworzenia baseline'ów oraz porównywania wyników.

```text
PCAP testowy
   |
   +--> pełny Zeek na PC --> logi/eventy --> reference summary EN
   |                                             |
   |                                             | baseline
   v                                             v
pcap_reader / dekodery Tab5 --> metryki --> portable summary core --> renderer EN
```

### 22.2. Decyzje zakresowe

W zakresie:

- ekstrakcja logiki jako biblioteka C++ z możliwie małą powierzchnią runtime;
- wejście offline PCAP oraz opcjonalne wejście z rekordów przypominających logi Zeek;
- deterministyczny output angielski;
- średni poziom szczegółowości;
- rozdzielenie zbierania metryk od budowania narracji;
- testy PCAP → summary oraz porównanie z wynikiem referencyjnym Zeek.

Poza zakresem pierwszej wersji:

- pełny redesign frameworka logowania Zeek;
- uruchamianie pełnego Zeek/Bro scripting runtime na Tab5 bez wcześniejszego benchmarku;
- generowanie opisu przez LLM;
- aktywne wzbogacanie IOC z Internetu;
- pełna zgodność ze wszystkimi analizatorami protokołów Zeek.

### 22.3. Etap 1 — granica ekstrakcji runtime Zeek

Analiza statyczna tego etapu jest zakończona. Checkboxy pozostają otwarte do czasu utworzenia corpus PCAP, pomiarów i testu referencyjnego Zeeka.

- [ ] Potwierdzić lifecycle wejścia offline w `src/iosource/pcap/Source.cc`.
- [ ] Potwierdzić kontrakt źródła i statystyk pakietów w `src/iosource/PktSrc.h`.
- [ ] Prześledzić wejście procesu w `src/main.cc`.
- [ ] Prześledzić packet/event run loop w `src/RunState.cc`.
- [ ] Zbudować diagram zależności od libpcap, event engine, analyzers, script runtime i logging managera.
- [ ] Wyznaczyć `cut line`: pozostawić niezbędne dekodery i eventy, a pełny system skryptów zastąpić minimalnym kontraktem metryk tam, gdzie jest to możliwe.
- [ ] Zweryfikować licencję Zeek oraz wymagane attribution przed kopiowaniem albo adaptacją kodu.

Kryterium wyjścia: lista komponentów `reuse`, `adapt`, `reference only` i `exclude`, wraz z oszacowaniem flash, RAM/PSRAM oraz wymagań systemowych.

Wynik wstępnej bramki:

- `Source.cc::OpenOffline()` używa `pcap_fopen_offline()`, a `ExtractNextPacket()` pobiera kolejne rekordy przez `pcap_next_ex()`;
- `RunState.cc::dispatch_packet()` aktualizuje czas sieciowy, wywołuje `packet_mgr->ProcessPacket(pkt)` i `event_mgr.Drain()`;
- manager analizy przekazuje pakiet do korzenia grafu analizatorów i generuje eventy, które następnie obsługują skrypty i loggery;
- pełny build wymaga PCAP, OpenSSL 3, Zlib, Flex, Bison i Pythona, a konfiguracje z nowymi analizatorami również Spicy/HILTI;
- główna licencja Zeeka jest 3-klauzulową BSD; przy kopiowaniu kodu trzeba zachować wymagane noty, lecz obecna implementacja Tab5 jest napisana niezależnie;
- `reuse`: brak kodu Zeeka w firmware MVP;
- `adapt`: model strumieniowy, semantyka wybranych protokołów i przyszły kontrakt metryk;
- `reference only`: analizatory protokołów, conn state, weird/notice, SumStats i logi;
- `exclude`: event engine, scripting runtime, logging manager, Broker, Spicy/HILTI i pełny type system.

Pomiar flash/RAM pełnego Zeeka nie jest potrzebny do decyzji MVP, bo wymienione warstwy są architektonicznie niezgodne z małym firmware. Do zmierzenia pozostaje koszt naszego `pcap_reader`, indeksu PSRAM oraz kolejnych dekoderów na realnych plikach.

### 22.4. Etap 2 — mapa źródeł semantycznych

Etap zależy od 22.3.

| Obszar summary | Źródło referencyjne w Zeek | Minimalne dane |
|---|---|---|
| Connections | `scripts/base/protocols/conn/main.zeek` | flow, peers, bytes, duration, service, conn state |
| DNS | `scripts/base/protocols/dns/main.zeek` | domains, qtypes, rcodes, answers, NXDOMAIN |
| HTTP | `scripts/base/protocols/http/main.zeek` | methods, hosts, URIs, status classes, user agents |
| TLS | `scripts/base/protocols/ssl/main.zeek` | versions, ciphers, SNI, alerts, cert metadata |
| Notices | `scripts/base/frameworks/notice/main.zeek` | typ, severity/priority, exemplar |
| Weird | `scripts/base/frameworks/notice/weird.zeek` | parser/protocol anomaly, count, exemplar |
| Files/IOC | `scripts/base/frameworks/files/main.zeek` | MIME/type, size, source, hashes gdy dostępne |
| Aggregation | `scripts/base/frameworks/sumstats/main.zeek` | sum, unique, top-k, ratio, threshold, window |

Zadania:

- [ ] Opisać dokładne pola wejściowe każdej sekcji.
- [ ] Rozdzielić dane możliwe do uzyskania z linktype 1, 105, 230 i 283.
- [ ] Oznaczyć pola wymagające reassembly TCP, deszyfracji albo zewnętrznych danych.
- [ ] Zdefiniować zachowanie `unknown/not available`, aby brak dekodera nie tworzył fałszywych wniosków.

### 22.5. Etap 3 — API biblioteki summary

Etap zależy od 22.4.

Stan implementacji: powstał pierwszy kontrakt C w `components/pcap_summary`. Rozdziela reader od agregacji i UI, używa jawnych statusów `pcap_reader_status_t`, stałych pojemności oraz flag `approximate`. Jest to kontrakt MVP do zweryfikowania na corpus, a nie zamknięte API zgodności z Zeekiem.

API ma rozdzielać trzy warstwy:

```text
packet/log ingest -> normalized metrics -> deterministic text renderer
```

Wymagania kontraktu:

- wejście z pliku PCAP i/lub ze znormalizowanych rekordów log-like;
- struktury sekcji `Traffic`, `Protocols`, `Anomalies`, `IOC`;
- jawne histogramy, liczniki, top-N, ratio, threshold i confidence flags;
- stabilna kolejność wyników niezależna od kolejności elementów w mapie/hash table;
- konfigurowalne limity pamięci, top-N i długości tekstu;
- brak zależności warstwy metryk od LVGL;
- renderer tekstu EN wymienialny bez modyfikowania parserów;
- możliwość strumieniowego przetwarzania bez ładowania całego PCAP do RAM.

Do ustalenia w API:

- sposób raportowania częściowego wyniku dla uciętego PCAP;
- sposób oznaczania pominiętych pakietów i limitów pamięci;
- polityka błędów C++ zgodna z ESP-IDF, preferująca jawne statusy zamiast wyjątków;
- zakres użycia STL, RTTI i dynamicznych alokacji na P4.

Pierwsza implementacja nie używa C++, STL, RTTI ani wyjątków. Stan summary jest jedną strukturą alokowaną przez aplikację w PSRAM; reduktor nie wykonuje własnych dynamicznych alokacji.

### 22.6. Etap 4 — reduktory i agregacja

Etap zależy od 22.5.

- [ ] Użyć Zeek SumStats jako referencji zachowania, nie kopiować automatycznie całego frameworka.
- [ ] Zaimplementować reduktory: `sum`, `unique`, `top-k`, `ratio`, `time-window`. **W toku:** działają sumy, ograniczone Top-N i licznik observed-unique DNS; ratio i time-window pozostają otwarte.
- [ ] Dodać normalizację kluczy: IP/host pair, service, domain, SNI, filename/hash.
- [ ] Zapewnić deterministyczne sortowanie, w tym regułę rozstrzygania remisów. **W toku:** wynik jest sortowany malejąco po count, a remisy leksykograficznie albo według transport/port; wymaga baseline testu.
- [ ] Dodać limity cardinality oraz sygnał `truncated/approximate` po ich osiągnięciu. **W toku:** wszystkie tabele mają stałą pojemność i osobną flagę `approximate`; pozostaje walidacja na capture o dużej kardynalności.
- [ ] Ocenić, czy unique wymaga dokładnego zbioru w PSRAM, czy kontrolowanego algorytmu przybliżonego.

### 22.7. Etap 5 — algorytm sekcji medium-detail EN

Etap zależy od 22.6.

#### Traffic

- unique peers i host pairs;
- total flows i packets;
- bytes in/out;
- capture duration;
- dominant talkers i one-flow dominance;
- błędy lub niekompletność wpływające na statystyki.

#### Protocols

- service/protocol mix;
- DNS top domains, qtypes, rcodes i NXDOMAIN ratio;
- HTTP methods, hosts oraz status classes;
- TLS versions, ciphers, SNI i alerty;
- informacja o protokołach wykrytych, ale nieposiadających pełnego dekodera.

#### Anomalies

- liczba `weird` i `notice` według kategorii;
- high-signal exemplars z limitem liczby przykładów;
- bursty NXDOMAIN, TLS alerts, malformed HTTP i niespójne stany połączeń;
- confidence zależne od kompletności capture oraz dostępnych warstw dekodera.

#### IOC

- podejrzane domains, IP, URLs, filenames i hashes, gdy są dostępne;
- jasne rozróżnienie `observed indicator` od `known malicious`;
- w pierwszej wersji reguły lokalne i deterministyczne, bez zewnętrznego threat feedu;
- limit top-N oraz powód umieszczenia elementu w sekcji.

Dla każdej sekcji należy zdefiniować priorytety zdań, limity długości, reguły pomijania pustych sekcji i stabilny format baseline testów.

### 22.8. Etap 6 — build i integracja

Etap zależy od 22.7.

Ścieżka referencyjna Zeek na komputerze:

- `CMakeLists.txt`;
- `src/logging/Manager.cc` tylko tam, gdzie potrzebny jest adapter logów;
- `doc/advanced/devel/hacking.rst` jako workflow developerski;
- `doc/advanced/devel/btest.rst` jako standard baseline/regresji.

Ścieżka Tab5:

- osobny komponent, roboczo `components/pcap_summary/`;
- brak bezpośredniej zależności od LVGL i `tab_context_t`;
- integracja z planowanym `pcap_reader`;
- duże indeksy i agregaty w PSRAM;
- worker FreeRTOS analizujący plik offline;
- anulowanie i raport postępu do UI przez callback;
- wynik tekstowy oraz strukturalny możliwy do zapisania jako sidecar.

Nie należy dodawać kodu Zeek do firmware Tab5 przed zakończeniem bramki wykonalności i pomiarem minimalnego prototypu.

### 22.9. Etap 7 — testy i kryteria akceptacji

Etap zależy od 22.8.

- [ ] Testy jednostkowe reduktorów: empty, single element, ties, limits, overflow i deterministic order.
- [ ] Testy integracyjne `PCAP → summary` z tekstowym baseline.
- [ ] Porównanie pomocniczych pól z `conn.log`, `dns.log`, `http.log`, `ssl.log`, `weird.log` i `notice.log`.
- [ ] Test pustego PCAP.
- [ ] Test one-flow dominance.
- [ ] Test uciętego nagłówka globalnego i uciętego ostatniego rekordu.
- [ ] Test malformed protocol data i invalid caplen/origlen.
- [ ] Test DNS NXDOMAIN burst, TLS alerts i malformed HTTP.
- [ ] Test deterministyczności: identyczny output bajt w bajt dla tego samego wejścia i konfiguracji.
- [ ] Benchmark czasu, peak internal RAM, peak PSRAM i liczby pominiętych rekordów.
- [ ] Regresja Zeek `btest` dla zmienionego adaptera/pluginu, jeśli repo Zeek będzie modyfikowane.
- [ ] Przegląd `doc/security-considerations.rst` przed uznaniem parser-path za gotowy.

Minimalne kryterium akceptacji MVP:

1. summary nie crashuje na pustym, uciętym ani błędnym pliku;
2. wynik jest deterministyczny;
3. `Traffic` zgadza się z referencyjnymi licznikami dla kontrolowanych PCAP;
4. brak danych jest jawnie oznaczony, a nie interpretowany jako brak zagrożenia;
5. analiza nie ładuje całego PCAP do RAM;
6. użytkownik może anulować analizę bez blokowania LVGL;
7. wynik mieści się w ustalonym budżecie czasu i pamięci P4.

### 22.10. Etap 8 — migracja i rollback

Etap może być prowadzony równolegle z testami po ustabilizowaniu API.

#### Faza 1 — adapter log-based na komputerze

- pełny Zeek generuje istniejące logi/eventy;
- nowa biblioteka ingestuje rekordy i generuje summary;
- cel: szybka walidacja jakości sekcji i reguł narracyjnych.

#### Faza 2 — portable C++ summary core

- reduktory i renderer nie zależą od procesu Zeek;
- wejście dostarcza minimalny adapter rekordów;
- cel: uruchomienie tych samych test vectors poza Zeek.

#### Faza 3 — integracja Tab5

- `pcap_reader` i dekodery Tab5 zasilają ten sam kontrakt metryk;
- feature flag ukrywa funkcję do czasu przejścia benchmarków;
- wynik można porównać z reference summary na komputerze.

Rollback:

- każda faza ma osobny target i benchmark;
- brak ingerencji w istniejący transfer oraz podstawowy viewer PCAP;
- feature flag pozwala wyłączyć summary bez wyłączania otwierania plików;
- regresja czasu lub pamięci blokuje przejście do następnej fazy.

### 22.11. Pliki referencyjne Zeek

Wszystkie ścieżki poniżej są względne wobec `C:\Users\mati\.codex\workspace\zeek`:

- `src/iosource/pcap/Source.cc` — offline PCAP open/read/close i koniec źródła;
- `src/iosource/PktSrc.h` — kontrakt źródła pakietów oraz statystyki;
- `src/main.cc` — uruchomienie procesu;
- `src/RunState.cc` — orkiestracja packet/event processing;
- `src/logging/Manager.cc` — strumienie logowania i writery;
- `scripts/base/protocols/conn/main.zeek` — flow i conn state;
- `scripts/base/protocols/dns/main.zeek` — DNS request/response;
- `scripts/base/protocols/http/main.zeek` — HTTP request/response;
- `scripts/base/protocols/ssl/main.zeek` — TLS/SSL handshake i stany;
- `scripts/base/frameworks/notice/main.zeek` — klasyfikacja notice;
- `scripts/base/frameworks/notice/weird.zeek` — anomalie parser/protocol;
- `scripts/base/frameworks/files/main.zeek` — metadata plików i IOC;
- `scripts/base/frameworks/sumstats/main.zeek` — agregacja i thresholding;
- `doc/advanced/devel/hacking.rst` — build/dev workflow;
- `doc/advanced/devel/btest.rst` — baseline i testy regresji;
- `doc/security-considerations.rst` — założenia bezpieczeństwa parserów.

### 22.12. Pierwszy konkretny krok

Po dodaniu fundamentu viewera następnym krokiem jest corpus i ścieżka referencyjna:

1. wybrać 3–5 małych PCAP reprezentujących Ethernet, Wi-Fi, DNS/HTTP/TLS oraz dane uszkodzone;
2. najpierw porównać na Tab5 indeks, linktype, timestampy i długości pakietów z Wiresharkiem;
3. uruchomić pełny Zeek offline na tym samym corpus i zachować minimalny zestaw logów referencyjnych;
4. zdefiniować pierwszy neutralny format metryk ponad istniejącym `pcap_reader`;
5. stworzyć renderer tylko dla `Traffic`, a następnie porównać wynik z `conn.log`;
6. dopiero po stabilnym baseline dołączać `Protocols`, `Anomalies` i `IOC`.

Najważniejsza zasada: **Zeek jest oracle/reference implementation, a nie automatycznie gotową biblioteką embedded**. Kod trafia na ESP32-P4 dopiero wtedy, gdy jego zależności i koszt zostały zmierzone.

### 22.13. Jak używać lokalnego Zeeka do baseline'u

Aktualny workspace zawiera czysty kod źródłowy Zeeka, ale nie zawiera gotowego `zeek.exe` i polecenie `zeek` nie jest obecnie dostępne w `PATH`. Zgodnie z `doc/quickstart.rst` podstawowe przetwarzanie offline po zbudowaniu lub zainstalowaniu Zeeka wygląda tak:

```text
zeek -r capture.pcap
```

Polecenie zapisuje dostępne logi, na przykład `conn.log`, `dns.log`, `http.log`, `ssl.log`, `files.log`, `notice.log` i `weird.log`, do bieżącego katalogu. Dla łatwiejszego parsera referencyjnego użyjemy JSON:

```text
zeek -r capture.pcap LogAscii::use_json=T
```

Opcję `-C`, która wyłącza walidację checksum, należy dodawać tylko dla capture'ów z celowo niepoprawnymi checksumami, na przykład wynikającymi z offloadu karty sieciowej:

```text
zeek -C -r capture.pcap LogAscii::use_json=T
```

Każdy plik testowy powinien dostać osobny pusty katalog wyjściowy. Dzięki temu stare logi nie zostaną pomylone z wynikiem bieżącego capture. Dla danych z Monstera najpierw porównujemy classic-PCAP i liczniki z Wiresharkiem, a dopiero potem logi Zeeka z przyszłym kontraktem `pcap_summary`.

### 22.14. Kolejne ekrany diagnostyczne inspirowane Zeekiem

Największą wartość da osobny ekran `Diagnostics`, który agreguje dane podczas jednego przebiegu po indeksie. Nie należy przeciążać podstawowej tabeli pakietów. Proponowana kolejność:

1. **Connections (`conn.log`-like)** — grupowanie po 5-tuple, czas początku, czas trwania, protokół, liczba pakietów i bajtów w obu kierunkach oraz uproszczony stan TCP. To powinien być następny moduł, bo zasila większość kolejnych analiz.
2. **Top Talkers / Services** — najbardziej aktywne hosty, pary hostów, porty i usługi, z rozdzieleniem RX/TX. Większość liczników już istnieje w `pcap_summary`; brakuje kierunku i rozmów.
3. **Device Discovery** — inwentarz urządzeń na podstawie ARP, DHCP, mDNS i SSDP: IP, MAC, hostname, oferowane usługi i pierwsze/ostatnie wystąpienie.
4. **DNS Diagnostics** — udział NXDOMAIN/SERVFAIL, najczęstsze klienty i serwery, rzadkie QTYPE, długie lub wieloetykietowe domeny, gwałtowne serie zapytań oraz kandydaci na DNS tunneling. Część wymaganych liczników już jest zaimplementowana.
5. **TLS ClientHello** — SNI, ALPN, wersja TLS, oferowane szyfry i liczba połączeń do nazw. Odcisk JA3 można dodać później, po ustaleniu kosztu MD5 i zasad prezentacji.
6. **HTTP metadata** — metoda, Host, URI, User-Agent, status i Content-Type dla nieszyfrowanego HTTP; bez składania pełnych plików w pierwszej wersji.
7. **ARP Diagnostics** — zmiana MAC dla tego samego IP, wiele IP na jednym MAC, gratuitous ARP i możliwe konflikty adresów.
8. **Timeline / Bursts** — pakiety i bajty na sekundę, największe przerwy oraz krótkie skoki ruchu. Na Tab5 można pokazać prosty wykres słupkowy bez przechowywania wszystkich timestampów.
9. **Notices (`notice.log`/`weird.log`-like)** — jedna lista zdarzeń wysokiego poziomu: skan portów, nagły wzrost NXDOMAIN, podejrzanie długie DNS, deauth storm, malformed/truncated burst, konflikt ARP i nietypowy port usługi.

Najbliższy rekomendowany etap to `Connections + Top Talkers`. Wymaga ograniczonej tablicy przepływów w PSRAM i daje bazę pod port-scan detection, timeline, TLS/HTTP oraz listę urządzeń. Każda tabela musi mieć stały limit, flagę `approximate` i deterministyczne sortowanie tak jak obecny `pcap_summary`.

### 22.15. Live stats podczas MITM

Stan JanOS pozwala dodać podstawowy licznik bez modyfikowania jego firmware:

- `pcap_capture_frame_count` rośnie po zapisaniu nagłówka i danych rekordu przez `pcap_writer_task`;
- `pcap_capture_drop_count` rośnie, gdy pełna kolejka nie przyjmie nowej ramki;
- aktywny task ARP wypisuje mniej więcej co 10 sekund linię `ARP spoof: round R, H hosts, captured N, dropped M`;
- po `stop` JanOS wypisuje końcowe `PCAP saved: PATH (N frames, M drops)`.

TODO Tab5 — wariant bez zmian JanOS:

- [ ] Dodać do kontekstu MITM flagę monitorowania, uchwyt taska, tab źródłowy i liczniki `captured`/`dropped`.
- [ ] Po zakończeniu synchronicznego startu uruchomić jeden reader UART przypisany do transportu, z którego wystartował MITM.
- [ ] Parsować okresową linię ARP i pokazywać w popupie `Saved packets`, `Queue drops`, czas sesji oraz wyliczane na Tab5 `packets/s` z różnicy kolejnych próbek.
- [ ] Po naciśnięciu STOP pozostawić popup w stanie `Stopping`, poczekać na końcowe `PCAP saved`, pokazać finalne liczniki i dopiero udostępnić `Done`.
- [ ] Zatrzymywać task przed usunięciem obiektów LVGL i nie dopuszczać drugiego czytnika tego samego UART podczas sesji.

Ograniczenia obecnej telemetrii JanOS:

- linia okresowa istnieje tylko, gdy wykryto gateway i co najmniej jeden host, więc działa task ARP;
- pierwsza aktualizacja pojawia się dopiero po około 10 sekundach;
- `dropped` nie obejmuje nieudanej alokacji ramki ani ramek większych niż 1600 B;
- nie ma licznika bajtów, głębokości kolejki, błędów `fwrite` ani osobnych RX/TX;
- niesprawdzane wyniki `fwrite` oznaczają, że `captured` jest liczbą prób zapisania rekordów, a nie twardym potwierdzeniem zapisu na nośniku.

Docelowa mała modyfikacja JanOS powinna wysyłać co 1–2 s stabilną, łatwą do parsowania linię, niezależną od taska ARP, na przykład:

```text
[PCAP_STATS] frames=1234 drops=2 bytes=845120 queue=4 rx=700 tx=534
```

Taki komunikat powinien powstawać w tasku writera lub osobnym lekkim timerze i mieć końcowy odpowiednik po zamknięciu pliku. Nie jest konieczny dla MVP, ale jest wymagany, jeśli licznik ma działać zawsze, aktualizować się szybko i pokazywać wiarygodne błędy zapisu.

## 23. `ESPShark` — centralny analizator PCAP i Live

### 23.1. Rola kafla

`ESPShark` jest roboczą nazwą wspólnego wejścia do funkcji sieciowych Tab5. Nie powinien być tylko przemianowanym `PCAP Viewerem`. Docelowe cztery obszary:

1. `Files` — obecny lokalny viewer plików z SD Tab5.
2. `Live` — sterowanie capture na wybranym Monsterze i podgląd bieżącego strumienia.
3. `Sessions` — lokalna biblioteka, synchronizacja i ostatnie capture’y.
4. `Diagnostics` — Overview, DNS, Connections, Top Talkers, Devices i Alerts.

Proponowany podtytuł kafla: `Capture • Inspect • Analyze`.

### 23.2. Poziomy funkcji Live

| Poziom | Widok | Transport | Zmiany JanOS |
|---|---|---|---|
| `Live Stats` | packets, drops, rate, hosts, czas, plik | tekst UART | brak dla ograniczonego MITM MVP; mała zmiana dla stabilnej telemetrii |
| `Near Live` | nowe pakiety partiami z opóźnieniem kilku sekund | HTTP offset/range albo zamknięte segmenty PCAP | średnie: APSTA, rotacja lub Range, synchronizacja odczytu z writerem |
| `True Live` | tabela pakietów, filtry i analiza przyrostowa z małym opóźnieniem | dedykowany binarny TCP przez `JanOS-Live` | większe, ale lokalne: drugi bounded queue, stream task i lifecycle APSTA |

UART 115200 ma około 11,5 kB/s użytecznego maksimum i jest wspólnym kanałem sterowania/logów. Nie nadaje się jako niezawodny transport pełnych ramek Ethernet/Wi-Fi. Może obsługiwać statystyki i ewentualnie ograniczony podgląd metadata-only.

### 23.3. Rekomendowana architektura True Live

```text
JanOS capture hook
  +--> bounded SD queue ----> PCAP writer ----> Monster SD (źródło prawdy)
  |
  +--> bounded live queue --> non-blocking TCP stream
                                  |
Tab5 internal C6 <--- Wi-Fi APSTA-+--> P4 live decoder
                                       +--> PSRAM ring buffer
                                       +--> incremental summary
                                       +--> LVGL ESPShark Live
```

Zasady bezpieczeństwa działania:

- zapis pełnego PCAP na SD Monstera ma zawsze wyższy priorytet niż Live;
- brak klienta lub wolny Tab5 nie może blokować hooka ani writera;
- przepełnienie `live queue` zwiększa osobny `stream_drops`, ale nie zatrzymuje capture SD;
- UART służy do `start/stop/status` oraz negocjacji sesji;
- stream ma nagłówek sesji z wersją protokołu, linktype i rozdzielczością czasu;
- każda ramka ma timestamp, caplen, origlen i payload;
- po reconnect Tab5 zaczyna od nowych ramek, a kompletny plik pozostaje na SD Monstera.

### 23.4. Wymagane zmiany JanOS dla True Live

- [ ] Zmienić `wifi_connect`, aby opcjonalnie zachowywał APSTA i nie niszczył AP netif aktywnej sesji Live.
- [ ] Uruchamiać chroniony hasłem SSID `JanOS-Live` przed połączeniem STA z siecią docelową; AP musi podążać za kanałem STA.
- [ ] Dodać osobną ograniczoną kolejkę live, która nie współdzieli ownership buforów z kolejką SD.
- [ ] Dodać nieblokujący TCP stream task i licznik `stream_drops`.
- [ ] Zdefiniować `ESPShark Live Protocol v1` z framingiem odpornym na częściowe odczyty i reconnect.
- [ ] Dodać jednoznaczne komunikaty `LIVE_READY`, `LIVE_CLIENT`, `LIVE_STATS`, `LIVE_STOPPED`.
- [ ] Przetestować, że APSTA, ARP spoof, IP forwarding, writer SD i stream działają równocześnie bez utraty kontroli UART.

### 23.5. Wymagane zmiany Tab5

- [ ] Wydzielić dekoder pakietu z file-only `pcap_reader`, aby przyjmował także bufor w RAM.
- [ ] Dodać receiver TCP przez wewnętrzny C6 i parser ramek odporny na fragmentację strumienia.
- [ ] Trzymać w PSRAM ograniczony ring ostatnich 512–1024 pakietów; SD Monstera pozostaje pełnym archiwum.
- [ ] Aktualizować agregaty przyrostowo zamiast przebudowywać summary przy każdym pakiecie.
- [ ] Odświeżać LVGL partiami, maksymalnie kilka razy na sekundę, zamiast tworzyć obiekt dla każdej odebranej ramki.
- [ ] Rozdzielić `Freeze View` od `Stop Capture`: zamrożenie UI nie zatrzymuje zapisu ani odbioru.
- [ ] Pokazywać osobno `SD frames`, `stream frames`, `stream drops`, bitrate, packets/s i stan połączenia.
- [ ] Po STOP zsynchronizować finalny plik i zaoferować `Open saved capture`.

### 23.6. Proponowany ekran Live

Górna część: źródło `MBus/Grove/USB`, typ `MITM/Radio/Mesh`, sieć lub kanał oraz `Start Live`.

Po starcie:

- pasek `LIVE`, czas, packets/s, bitrate, SD frames i drops;
- zakładki `Packets`, `Overview`, `DNS`, `Connections`, `Alerts`;
- przyciski `Freeze View`, `Stop & Save` oraz po zakończeniu `Sync + Open`;
- filtry działające na ograniczonym buforze live, z jawnym opisem `last N packets`.

### 23.7. Decyzje przed implementacją

Rekomendowane odpowiedzi startowe:

1. Pierwszy True Live tylko dla `MITM/net`; `radio` i `mesh` później, ponieważ współdzielą radio z transportem Wi-Fi.
2. Monster zawsze zapisuje pełny PCAP na własną SD; Live jest podglądem best-effort.
3. TCP zamiast UDP/WebSocket: prostsza kontrola sesji i kolejności, ale nadawca pozostaje nieblokujący.
4. Ring Tab5: 512 pełnych ramek na początek, potem pomiar PSRAM i płynności.
5. Pierwsza analiza live: protokoły, endpoints, ports i DNS; Connections dopiero po wydzieleniu klucza przepływu.
6. Docelowe odświeżanie UI 4–5 Hz, niezależnie od prędkości odbioru.

Otwarte pytania produktowe:

- czy pierwszy milestone ma być bezpiecznym `Live Stats`, czy od razu `True Live MITM`;
- czy dopuszczamy opóźnienie 1–3 s dla prostszego Near Live, czy celem jest tabela reagująca poniżej 500 ms;
- czy ring ma przechowywać pełne ramki dla HEX, czy tylko pierwsze 256 B, podczas gdy pełny payload zostaje wyłącznie na SD Monstera;
- czy `ESPShark` zastępuje obecny kafel `PCAP Viewer`, czy początkowo działa równolegle do czasu migracji.

### 23.8. Zaimplementowany flow źródeł — 2026-08-09

Decyzja produktowa została zamknięta: `ESPShark` zastępuje dwa wcześniejsze kafle PCAP i jest jednym wejściem do całego modułu. Aktualny przepływ:

```text
Compromised Data
  └─ ESPShark
      ├─ READ FROM MONSTER
      │   ├─ lista PCAP z SD aktywnego MBus/Grove/USB
      │   ├─ MONSTER: AVAILABLE
      │   ├─ TAB5: SYNCED / NOT COPIED
      │   ├─ COPY TO TAB5
      │   └─ Sync all
      └─ OPEN FROM TAB5
          └─ lokalna biblioteka → indeks → pakiety/summary/DNS
```

`Sync all` oznacza „sprawdź całą listę”, a nie „pobierz wszystko ponownie”. Worker otwiera jedną sesję `JanOS-Admin`, pobiera aktualne metadane każdego pliku, pomija zgodne kopie i pobiera tylko brakujące albo zmienione. Widoczny znacznik `TAB5: SYNCED` jest szybką informacją z lokalnego dziennika: wymaga zgodnej ścieżki zdalnej oraz istnienia lokalnego pliku o zapisanym rozmiarze. Ostateczna decyzja o pominięciu podczas synchronizacji korzysta z aktualnego rozmiaru zwróconego przez Monster.

Po zamknięciu popupu transferu lista Monstera jest automatycznie odczytywana ponownie, dzięki czemu stan świeżo skopiowanych plików zmienia się na `TAB5: SYNCED` bez ręcznego wychodzenia z modułu.

Jeśli aktywna jest zakładka `INTERNAL` albo Monster nie zgłasza SD, `READ FROM MONSTER` jest wyłączone z czytelnym komunikatem. `OPEN FROM TAB5` pozostaje dostępne, bo analizuje wspólną lokalną kartę Tab5.

Test sprzętowy:

- [ ] Otworzyć `Compromised Data → ESPShark` na MBus.
- [ ] Sprawdzić, że oba wybory są w pełni widoczne i dotykalne.
- [ ] Otworzyć listę Monstera i potwierdzić status wcześniej skopiowanego `sniff_2.pcap` jako `TAB5: SYNCED`.
- [ ] Utworzyć nowy capture i potwierdzić `TAB5: NOT COPIED`.
- [ ] Nacisnąć `Sync all`; oczekiwane: stary plik pominięty, nowy skopiowany.
- [ ] Zamknąć popup, ponownie wejść na listę i potwierdzić `TAB5: SYNCED` dla obu plików.
- [ ] Wejść w `OPEN FROM TAB5` i otworzyć nowo zsynchronizowany plik.
- [ ] Powtórzyć `Sync all`; oczekiwane `Copied: 0`, bez duplikatu na SD Tab5.

### 23.9. Kierunek: Pentest oraz Network Health

`ESPShark` powinien rozdzielać surowe fakty od wniosków. Nie należy wyświetlać arbitralnego „sieć zdrowa/chora” bez dowodów. Proponowany wynik jednej sesji:

- `HEALTHY` — brak silnych sygnałów w obserwowanej próbce;
- `WATCH` — odchylenia wymagające sprawdzenia;
- `SUSPICIOUS` — kilka skorelowanych sygnałów albo jeden sygnał wysokiej jakości;
- `CRITICAL` — aktywny konflikt, szybka propagacja, atak warstwy drugiej lub mocno potwierdzona anomalia;
- `INSUFFICIENT DATA` — próbka jest zbyt krótka, limitowana albo nie zawiera obu kierunków ruchu.

Każdy wynik musi pokazać `confidence`, zakres analizy i listę dowodów, na przykład: `192.168.0.44 contacted 37 internal hosts on TCP/445 in 12 s`. Punkty ryzyka są użyteczne do sortowania alertów, ale ekran powinien prezentować przyczyny, a nie sam numer.

Widok pentestowy dla sieci, do której użytkownik ma uprawnienia, powinien zawierać:

1. `Asset Inventory` — IP, MAC, hostname, vendor, pierwsze/ostatnie wystąpienie i odkryte usługi.
2. `Exposure` — otwarte lub obserwowane usługi, ruch jawnym tekstem, stare wersje TLS, niespodziewane protokoły administracyjne.
3. `Segmentation` — kto komunikuje się między podsieciami/VLAN-ami i czy urządzenia IoT kontaktują się z nietypowymi segmentami.
4. `Name & Discovery` — DNS, mDNS, LLMNR, NBNS, SSDP i DHCP; nadmierny broadcast oraz wycieki nazw.
5. `Attack Surface` — skany pionowe/poziome, próby do wielu hostów, nietypowe porty i gwałtowne serie SYN.
6. `Evidence` — dotknięcie alertu otwiera pasujące connections/pakiety, żeby można było zweryfikować werdykt.

### 23.10. Detekcja „czy lata robak”

Samo wystąpienie wielu pakietów nie wystarcza do wykrycia robaka. Najbardziej wiarygodna detekcja na Tab5 powinna korelować zdarzenia w czasie:

1. Jeden host kontaktuje wiele wewnętrznych adresów na tym samym porcie w krótkim oknie (`horizontal scan`).
2. Liczba nowych celów i nieudanych połączeń rośnie szybciej niż udanych sesji.
3. Jeden z niedawno dotkniętych hostów zaczyna powtarzać ten sam wzorzec wobec kolejnych hostów (`second-hop spread`).
4. Wzorzec dotyczy portów często używanych do ruchu bocznego lub usługi nietypowej dla profilu tej sieci.
5. Opcjonalnie pojawiają się skorelowane anomalie DNS, ARP albo gwałtowny wzrost broadcastu.

Pierwsza wersja może działać offline na PCAP i nie wymaga zmian JanOS. Potrzebuje jednak warstwy `Connections` oraz kubełków czasowych. Zalecany ograniczony stan w PSRAM:

- tabela przepływów 5-tuple z pierwszym/ostatnim timestampem, pakietami, bajtami i flagami TCP;
- tabela `source → unique internal destinations` w oknach 10 s / 60 s;
- tabela `source + destination port → unique destinations`;
- mały graf „kto zaczął skanować po kontakcie z kim”;
- limity pojemności, wskaźnik `approximate` i brak alokacji per-packet.

Przykładowe alerty pierwszej wersji:

- `PORT SCAN` — jeden host, wiele portów jednego celu;
- `HOST SWEEP` — jeden host, ten sam port, wiele celów;
- `WORM-LIKE SPREAD` — co najmniej dwa kolejne hosty powtarzają podobny sweep w krótkim czasie;
- `ARP CONFLICT` — ten sam IP przechodzi między różnymi MAC;
- `DNS ANOMALY` — wysoki NXDOMAIN/SERVFAIL, wiele unikalnych długich nazw lub nietypowe QTYPE;
- `BEACONING` — regularne małe połączenia do tego samego celu;
- `EXFIL CANDIDATE` — długotrwała asymetria wysyłanych bajtów do rzadkiego zewnętrznego endpointu.

Stan realizacji pierwotnej kolejności:

- [x] P0: `Connections` i rozróżnienie sieci wewnętrznej/zewnętrznej.
- [x] P0: `Devices/Services` i drill-down z alertu do pakietów.
- [x] P1: `ARP Conflict`, `Port Scan`, `Host Sweep`, DNS anomaly oraz timeline.
- [x] P1: ekran `Network Health` z poziomem, confidence i dowodami.
- [x] P2: korelacja `WORM-LIKE SPREAD`, beaconing i pojedynczy baseline `home`.
- [ ] Opcjonalne po v6: wiele profili baseline per sieć.
- [ ] Poza zakresem Offline v6: te same detektory w trybie Live po dodaniu
  protokołu strumieniowego JanOS.

### 23.11. Proponowany commit message

```text
feat(tab5): introduce the ESPShark capture hub

- replace separate PCAP capture and viewer tiles with ESPShark
- add Monster SD and Tab5 SD source flows
- show per-capture Monster and Tab5 synchronization state
- rename PCAP synchronization to Sync all while copying only missing files
- route capture-library navigation through the ESPShark hub
- document the network health and threat-analysis roadmap
```

### 23.12. Cache, eksport i profil filtra — implementacja 2026-08-09

Dodany komponent: `components/pcap_analysis_store`.

Układ plików na SD Tab5:

```text
/sdcard/lab/pcaps/
├─ sniff_5.pcap
└─ .espshark/cache/
   └─ <path-hash>_sniff_5.espcache

/sdcard/lab/espshark/
├─ exports/
│  ├─ sniff_5_<timestamp>.espreport.json
│  └─ sniff_5_<timestamp>_<FILTER>.pcap
└─ profiles/
   └─ last.espfilter.json
```

Katalog cache zaczyna się od kropki, dlatego obecny rekursywny skaner biblioteki PCAP pomija go automatycznie.

#### Cache v1

Cache przechowuje:

- `pcap_capture_info_t`;
- `pcap_scan_summary_t`;
- indeks maksymalnie 4096 pakietów;
- bitowe flagi klasyfikacji każdego indeksowanego pakietu;
- aktualne `pcap_summary_t` wraz z DNS Top-N.

Cache nie przechowuje payloadów. Popup HEX oraz 20 widocznych wierszy strony nadal czytają dane z oryginalnego PCAP na żądanie. Dzięki temu cache jest mały w porównaniu z capture i nie duplikuje wrażliwego payloadu.

Warunki cache hit:

- zgodny magic i schema version;
- zgodne rozmiary struktur bieżącego firmware;
- ten sam limit indeksu analizatora;
- zgodny hash ścieżki, rozmiar i mtime źródła;
- zgodny szybki CRC pierwszych i ostatnich 4096 B oraz rozmiaru;
- poprawny CRC nagłówka i całego payloadu cache;
- poprawne liczności tabel oraz każdy offset/caplen mieszczący się w źródłowym pliku.

Nieprawidłowy albo stary cache nie blokuje pliku: viewer wykonuje normalną analizę i zastępuje cache nowym. `REANALYZE` celowo pomija nawet poprawny cache.

#### Raport JSON v1

`EXPORT JSON REPORT` zapisuje:

- źródło, size, mtime i quick CRC;
- wersję/linktype/snaplen PCAP;
- całkowitą i indeksowaną liczbę pakietów;
- informację `index_limited`, `truncated_tail` i `loaded_from_cache`;
- aktywny filtr i liczbę dopasowań;
- protokoły, endpointy, porty oraz pełne obecne DNS Summary;
- puste, wersjonowane sekcje `connections`, `devices`, `alerts`, gotowe do zasilenia przez kolejne moduły.

JSON jest przeznaczony do Pythona, pandas, `jq` oraz własnych importerów. Nie należy importować binarnego `.espcache` jako raportu przenośnego.

#### Filtered PCAP

`EXPORT FILTERED PCAP` kopiuje 24-bajtowy global header classic-PCAP i pełne rekordy pasujące do aktywnego filtra. Wynik jest zwykłym PCAP dla Wiresharka, Zeeka i tsharka. Jeśli źródło ma `INDEX LIMITED`, eksport obejmuje wyłącznie analizowaną próbkę i popup pokazuje jawne ostrzeżenie.

#### Profil filtra MVP

`SAVE FILTER PROFILE` zapisuje aktualny filtr protokołu do `last.espfilter.json`. `LOAD LAST FILTER` waliduje enum i stosuje go do dowolnego otwartego PCAP bez ponownego indeksowania. Jest to MVP jednego profilu; nazwy wielu profili, CIDR, porty, zakres czasu i progi detektorów zostaną dodane wraz z query engine.

#### Test sprzętowy

- [ ] Otworzyć plik bez cache; oczekiwane pełne `Indexing` i `Building Summary`, potem `ANALYZED - cache v1 saved`.
- [ ] Wrócić do biblioteki; oczekiwany znacznik `ANALYZED | cache v1 | N indexed`.
- [ ] Otworzyć plik ponownie; oczekiwany szybki `CACHE HIT` bez długiego indeksowania.
- [ ] Wybrać DNS i zapisać profil; zmienić filtr, załadować profil i potwierdzić powrót do DNS.
- [ ] Wyeksportować JSON i sprawdzić poprawność w `jq` albo Pythonie.
- [ ] Wyeksportować filtered PCAP i porównać liczbę rekordów z filtrem w Wiresharku.
- [ ] Usunąć cache; bieżący widok pozostaje otwarty, a następne otwarcie wykonuje analizę od nowa.
- [ ] Uruchomić `REANALYZE` i potwierdzić zastąpienie cache bez plików `.tmp`.

#### Następne TODO

- [ ] Ekran `Saved Reports` z importem `.espreport.json` w trybie read-only.
- [ ] Powiązanie importowanego raportu z PCAP po fingerprint i aktywacja `SHOW EVIDENCE` tylko przy zgodności.
- [ ] Wiele nazwanych profili zamiast jednego `last.espfilter.json`.
- [ ] Limit rozmiaru/LRU cache i ekran `Manage ESPShark Storage`.
- [x] Rozszerzenie cache/report o Connections, Devices, Alerts i Network Health;
  etap zakończył się schema v5, a bieżący format v6 dodaje mapowania FQDN.

Proponowany commit message po testach:

```text
feat(espshark): cache PCAP analysis and export portable artifacts

- persist validated packet indexes, flags and summaries on Tab5 SD
- mark analyzed captures and load compatible cache entries on reopen
- export versioned JSON reports and filtered classic-PCAP files
- save and restore the last packet filter profile
- add cache deletion and forced reanalysis controls
```

## 24. Connections, Top Protocols and Follow Stream — implementacja 2026-08-09

Dodano pierwszą interaktywną warstwę analizy sieciowej ESPSharka. Kod jest
rozdzielony na komponent `components/pcap_flow` oraz widoki LVGL Tab5. Działa
offline na plikach z SD Tab5 i nie wymaga zmian JanOS.

### 24.1. Zrealizowany zakres 1–7

- [x] Wspólny model quick filter dla hosta/IP, kierunku IP, MAC, portu, aplikacji,
      flow i bezwzględnego okna czasu.
- [x] Dwukierunkowy identyfikator 5-tuple dla TCP i UDP.
- [x] Widok `CONNECTIONS`: originator/responder, czas, pakiety i bajty obu kierunków,
      confidence aplikacji i uproszczony stan TCP.
- [x] Klasyfikator aplikacji z `CONFIRMED`, `LIKELY` i `TRANSPORT`.
- [x] Widok `TOP PROTOCOLS`: pakiety, bajty, flow i liczniki confidence.
- [x] `Follow Stream Lite` otwierany z pakietu albo bezpośrednio z Connections.
- [x] Ograniczony TCP reassembly według SEQ, z usuwaniem retransmisji/overlap,
      raportowaniem gapów i jawnym oznaczeniem niepełnego wyniku.

### 24.2. Akcje pakietu i filtrowanie

Popup szczegółów pakietu ma przewijany pasek:

```text
HOST SRC | HOST DST | MAC SRC | MAC DST | PORT | TIME +/-5s | FOLLOW
```

`HOST SRC` i `HOST DST` wybierają host z pakietu, ale dopasowują go w obu
kierunkach. `PORT` preferuje port respondera/usługi z flow zamiast losowego portu
klienta. Quick filter łączy się z istniejącym filtrem protokołu. Aktywny filtr jest
widoczny nad kaflami protokołów i ma przycisk `CLEAR`.

Silnik zawiera również kierunkowe pola source/destination, aby późniejszy ekran
budowania filtrów mógł łączyć warunki bez zmiany packet matchera. Aktualny UI
udostępnia najczęstsze operacje jako akcje jednego dotknięcia.

`EXPORT FILTERED PCAP` respektuje filtr protokołu oraz aktywny quick filter.

### 24.3. Model flow i limity

Każdy flow przechowuje:

- IP, port i zaobserwowany MAC originatora oraz respondera;
- protokół L4, aplikację i confidence;
- pierwszy/ostatni pakiet i timestamp w mikrosekundach;
- pakiety, captured bytes i payload bytes w obu kierunkach;
- zaobserwowane flagi TCP w obu kierunkach.

Limity chroniące pamięć Tab5:

```text
mapa pakietów:       4096
flow:                 512
segmenty Follow:      256
tekst Follow:       24 KiB
odczyt segmentu:     4 KiB
```

Po przekroczeniu limitu wynik pokazuje `LIMITED`, `TRUNCATED` lub `INCOMPLETE`.
Connections obejmuje obecnie indeksowaną próbkę. Capture większy niż 4096 pakietów
nadal pokazuje `INDEX LIMITED`; pełna agregacja strumieniowa całego pliku jest
osobnym następnym etapem.

### 24.4. Klasyfikacja aplikacji v1

Sygnatury potwierdzane payloadem:

- linia żądania/odpowiedzi HTTP;
- nagłówek rekordu TLS;
- QUIC long header na UDP/443;
- banner SSH;
- magic SMB1/SMB2;
- nazwa protokołu MQTT CONNECT;
- handshake BitTorrent;
- bencode query/response BitTorrent DHT;
- strukturalnie zdekodowany DNS/mDNS.

Klasyfikacja `LIKELY` na podstawie portów:

- DNS, mDNS, DHCP, LLMNR, NBNS, SSDP/UPnP i NTP;
- HTTP, HTTPS/TLS i QUIC;
- SSH, FTP, Telnet, SMB i MQTT;
- SMTP, IMAP i POP3;
- typowy zakres BitTorrent 6881–6999.

Sam TCP/443 nie jest prezentowany jako potwierdzone HTTPS bez zaobserwowanego
rekordu TLS. Szyfrowany/obfuskowany BitTorrent może pozostać `TCP`, `UDP` albo
`LIKELY BitTorrent`.

### 24.5. Follow Stream Lite

Dla TCP payloady są grupowane kierunkami i porządkowane po TCP SEQ. Pełne
retransmisje są usuwane, overlap przycinany, a brakujące zakresy prezentowane jako
`[GAP: N byte(s)]`. UDP zachowuje kolejność capture. Bajty binarne są zastępowane
kropkami, a CR/LF/TAB pozostają czytelne.

```text
=== ORIGINATOR -> RESPONDER ===
[#14 +72 B] GET / HTTP/1.1

=== RESPONDER -> ORIGINATOR ===
[#18 +128 B] HTTP/1.1 200 OK
```

Nie jest to pełny odpowiednik reassembly Wiresharka. TCP sequence wrap-around,
zaawansowane IPv6 extension headers, payload poza oknem odczytu i ponad 256
segmentów są jawnie raportowane jako ograniczenia.

### 24.6. Cache i raport v3

Walidowany cache przechowuje bounded flow analysis oraz mapy packet→flow/app.
Aktualna schema v3 automatycznie unieważnia cache v1/v2; plik zostanie raz
przeanalizowany i zapisany w nowym formacie.

Raport JSON v3 dodaje:

- `applications` z packet/byte/flow confidence;
- `connections` ze statystyką obu kierunków i czasem;
- `flow_limits` z informacją o overflow.

Payload nie jest zapisywany w cache ani raporcie JSON.

### 24.7. Test sprzętowy

- [ ] Otworzyć capture bez cache i potwierdzić zapis cache v3.
- [ ] Otworzyć ponownie i sprawdzić `CACHE HIT` oraz tę samą liczbę Connections.
- [ ] Przetestować z pakietu filtry host, MAC, port usługi i `TIME +/-5s`.
- [ ] Połączyć quick filter z DNS/TCP/UDP i sprawdzić licznik dopasowań.
- [ ] Otworzyć Connections, wybrać flow i potwierdzić obecność obu kierunków.
- [ ] Uruchomić Follow z Connections oraz szczegółów pakietu.
- [ ] Dla HTTP sprawdzić czytelne request/response.
- [ ] Dla TLS porównać `CONFIRMED` z port-only `LIKELY`.
- [ ] Sprawdzić handshake/DHT BitTorrent i confidence.
- [ ] Wyeksportować quick-filtered PCAP i porównać z Wiresharkiem.
- [ ] Sprawdzić `applications`, `connections` i `flow_limits` w JSON v2.
- [ ] Capture z ponad 512 flow musi pokazać limit, bez crasha.

### 24.8. Następne TODO

- [ ] Pełna agregacja flow poza indeksem 4096 pakietów.
- [ ] Nazwane filtry z AND/OR, CIDR i kierunkiem.
- [x] Przełącznik ASCII/HEX w Follow Stream.
- [ ] Opcjonalne po v6: eksport tekstu Follow Stream jako osobnego artefaktu.
- [x] TLS ClientHello: SNI, ALPN, oferowana wersja, liczba cipherów/extensions
  i lokalny fingerprint `CH-FNV64`.
- [x] HTTP Host/method/URI oraz BitTorrent info-hash w metadanych flow.
- [x] Devices/Services i alerty z dowodami oparte na flow.
- [x] Diagnostyka retransmisji, SYN/SYN-ACK RTT i zero-window w Network Health.

Proponowany commit message po testach sprzętowych:

```text
feat(espshark): add flow analysis and Follow Stream Lite

- add bidirectional TCP and UDP connection tracking with bounded PSRAM limits
- classify common application protocols with confirmed and likely confidence
- add interactive Connections and Top Protocols views
- add host, MAC, service-port, time-window, flow and application filters
- reassemble bounded TCP payload streams with gap and retransmission indicators
- persist flow analysis in cache v3 and export connections in JSON reports
- apply quick filters when exporting evidence PCAP files
```

### 24.9. Poprawki kompilacji po wdrożeniu flow analysis

- [x] Usunięto `-Werror=format-truncation` w dekoderze ramek 802.11.
- [x] `source_mac` i `destination_mac` są formatowane bezpośrednio z sześciu
  bajtów adresu ramki, zamiast kopiowania z 64-bajtowych pól tekstowych
  `source`/`destination` do 18-bajtowych pól MAC.
- [x] Zachowano prawidłowe role adresów dla wariantów `ToDS`, `FromDS` oraz
  czteroadresowych ramek `ToDS + FromDS`.
- [ ] Ponowić kompilację użytkownika i dopisać kolejne błędy, jeżeli kompilator
  przejdzie dalej do następnego komponentu.

## 25. Offline Network Intelligence — implementacja 2026-08-09

Zakres działa wyłącznie na Tab5 na zapisanym classic-PCAP i nie wymaga nowych
komend ani modyfikacji JanOS. Detektory są heurystykami z dowodami, a nie
automatycznym werdyktem bezpieczeństwa.

### 25.1. Devices & Services

- [x] Inwentarz maksymalnie 128 unicastowych endpointów IPv4/IPv6.
- [x] IP, zaobserwowany MAC, hostname z odpowiedzi DNS, LAN/WAN, pierwszy i
  ostatni timestamp, pakiety oraz bajty TX/RX.
- [x] Maksymalnie 12 usług na urządzenie: transport, port, aplikacja, pakiety i
  bajty.
- [x] Sortowanie urządzeń według wolumenu ruchu.
- [x] Dotknięcie urządzenia ustawia quick filter na jego IP.
- [x] Jawne flagi `DEVICE TABLE LIMITED` oraz `services_limited`.

### 25.2. Metadane aplikacyjne

- [x] HTTP request/status line oraz nagłówek `Host`.
- [x] TLS ClientHello: legacy version, SNI i pierwszy ALPN.
- [x] BitTorrent handshake: info-hash w hex.
- [x] Metadane widoczne przy Connections i eksportowane w JSON.
- [x] Rozpoznanie nadal rozdziela `CONFIRMED`, `LIKELY` i `TRANSPORT`.

### 25.3. Network Health z dowodami

- [x] `PORT SCAN`: wiele portów jednego hosta w oknie 60 s.
- [x] `HOST SWEEP`: ten sam port wielu hostów w oknie 60 s.
- [x] `ARP CONFLICT`: jeden adres IPv4 obserwowany z różnymi MAC.
- [x] `DNS ANOMALY`: wysoki udział odpowiedzi z błędem.
- [x] `DNS ANOMALY`: także długie i wielolabelowe nazwy jako wskaźnik tunelowania.
- [x] `BEACONING`: co najmniej pięć regularnych połączeń do tego samego celu.
- [x] `EXFIL CANDIDATE`: silna asymetria payloadu LAN → WAN z minimalnym
  wolumenem danych.
- [x] `CLEARTEXT SERVICE`: HTTP, FTP, Telnet oraz nieszyfrowane protokoły pocztowe.
- [x] `EXCESSIVE BROADCAST`: wysoki udział multicast/broadcast w próbce.
- [x] `WEAK TLS`: ClientHello wskazujący TLS 1.0/1.1 lub starszy.
- [x] `TCP QUALITY`: wysoki udział wskaźników retransmisji albo zero-window.
- [x] `WORM-LIKE SPREAD`: host dotknięty przez sweep później powtarza podobny
  sweep na tym samym porcie.
- [x] Poziomy `INSUFFICIENT DATA`, `HEALTHY`, `WATCH`, `SUSPICIOUS`, `CRITICAL`.
- [x] Każdy alert przechowuje źródło, cel, port, czas, flow i do sześciu numerów
  pakietów jako dowody.
- [x] Dotknięcie alertu ustawia filtr flow albo host + okno czasu.
- [x] Jawna flaga `ALERT TABLE LIMITED`; maksymalnie 64 alerty.

### 25.4. Follow Stream i filtry

- [x] Follow Stream ASCII.
- [x] Follow Stream HEX z podziałem co 16 bajtów.
- [x] ASCII i HEX dostępne z pakietu oraz Connections.
- [x] Profil filtra v2 zapisuje protocol filter i pełny quick filter: IP, MAC,
  port, aplikację, flow oraz czas.
- [x] Nieprawidłowy file-specific flow jest wyłączany po wczytaniu profilu do
  innego PCAP.
- [x] Raport zapisuje opis quick filter i rzeczywistą liczbę wybranych pakietów.

### 25.5. Cache i raport v3

- [x] Cache v3 przechowuje flow metadata, urządzenia, usługi i alerty; cache v2
  jest automatycznie traktowany jako niezgodny i odbudowywany.
- [x] Walidacja liczników, zakresów enum, service count, evidence count i flow ID.
- [x] JSON v3 eksportuje `devices`, `services`, `network_health`, `alerts`,
  evidence packet numbers, SNI/HTTP/info-hash i rozszerzone limity.
- [x] Payload nadal nie jest zapisywany w cache ani raporcie JSON.

### 25.6. Testy sprzętowe przed zamknięciem etapu

- [ ] Ponowić build po poprawce `format-truncation`; Codex nie wykonywał buildu.
- [ ] Otworzyć stary plik z cache v2 i potwierdzić przebudowę do cache v3.
- [ ] Sprawdzić `DEVICES`, filtr urządzenia oraz listę usług.
- [ ] Sprawdzić HTTP Host i TLS SNI/ALPN na odpowiednim capture.
- [ ] Porównać Follow ASCII i HEX dla tego samego flow.
- [ ] Przetestować każdy typ alertu na przygotowanym PCAP i otworzyć jego dowody.
- [ ] Wyeksportować JSON v3 i zwalidować w `jq` albo Pythonie.
- [ ] Zapisać i wczytać profil łączący filtr protokołu z hostem/portem/czasem.
- [ ] Capture przekraczający limity ma pokazać `LIMITED`, bez resetu i bez
  uszkodzenia cache.

### 25.7. Opcjonalny backlog po zamknięciu Offline v6

Żadna z poniższych pozycji nie jest warunkiem kompletności v6. Są to możliwe
rozszerzenia następnej wersji analizatora.

- [ ] Pełna agregacja całego pliku poza szczegółowym indeksem 4096 pakietów.
- [ ] Query builder z grupami AND/OR, CIDR i relatywnym zakresem czasu.
- [ ] Wiele nazwanych profili i ekran zarządzania nimi.
- [ ] Ekran importu zapisanych raportów JSON oraz fingerprint PCAP.
- [ ] LRU/limit miejsca dla cache i eksportów.
- [ ] Eksport tekstu Follow Stream do osobnego pliku evidence.
- [x] SYN/SYN-ACK RTT, wskaźniki retransmisji i zero-window per flow.
- [ ] Duplicate ACK, RTT danych i analiza zmian TCP window.
- [ ] Dokładniejszy baseline per sieć do beaconingu i anomalii DNS.

### 25.8. Korekta paska analiz ESPSharka

- [x] Zastąpiono pojedynczy, poziomo przewijany pasek dwoma stałymi rzędami.
- [x] Rząd 1: `OVERVIEW`, `DNS`, `FLOWS`, `APPS`.
- [x] Rząd 2: `DEVICES`, `HEALTH`, `MAP`, `EXPORT`.
- [x] Przyciski korzystają z `flex_grow`, dlatego dzielą dostępną szerokość
  równo i nie wychodzą poza prawą krawędź ekranu.
- [x] Zachowano liczniki DNS, flow, urządzeń i alertów oraz kolor poziomu Health.
- [ ] Potwierdzić na Tab5 czy dwa rzędy nie zmniejszają nadmiernie wysokości
  tabeli pakietów; w razie potrzeby zmniejszyć wysokość przycisków z 42 do 38 px.

Proponowany commit message po kompilacji i testach:

```text
feat(espshark): add offline network intelligence

- inventory devices and observed services with capture evidence
- extract HTTP, TLS ClientHello and BitTorrent flow metadata
- detect scans, ARP conflicts, DNS anomalies, beaconing and suspicious transfer patterns
- correlate host sweeps into bounded worm-like spread findings
- add Network Health and Devices views with evidence filters
- add ASCII and HEX Follow Stream modes
- persist quick filters and offline insights in cache and report schema v3
```

## 26. ESPShark Communication Map — implementacja testowa 2026-08-09

Mapa jest grafem komunikacji zaobserwowanej w analizowanym PCAP, a nie fizyczną
topologią switchy, AP i routerów. Brak krawędzi oznacza wyłącznie brak takiej
komunikacji w widocznej próbce capture.

### 26.1. Model danych i limity

- [x] Graf jest budowany w PSRAM z istniejącego cache `pcap_flow_analysis_t`;
  JanOS i format cache nie wymagają zmian.
- [x] Maksymalnie 48 widocznych węzłów i 128 najważniejszych krawędzi.
- [x] Krawędzie wielu flow pomiędzy tą samą parą endpointów są agregowane.
- [x] Grubość krawędzi zależy od wolumenu, a kolor od dominującej aplikacji.
- [x] Węzły lokalne są scalane po MAC, dzięki czemu IPv4 i IPv6 tego samego
  urządzenia nie muszą zajmować dwóch pozycji.
- [x] Endpointy zewnętrzne pozostają rozdzielone po IP; MAC routera nie jest
  błędnie używany jako tożsamość internetowego celu.
- [x] Multicast i broadcast są grupowane do wspólnych węzłów.
- [x] Nadmiar lokalnych i zewnętrznych endpointów jest zwijany do `OTHER LAN`
  oraz `INTERNET +N`.
- [x] Alerty i rozpoznane usługi otrzymują priorytet przy wyborze krawędzi.
- [x] Widok pokazuje `SAMPLE`, `MAP LIMITED` i `OBSERVED IN CAPTURE`, gdy
  analiza lub prezentacja jest ograniczona.

### 26.2. Interfejs

- [x] Drugi rząd analiz: `DEVICES`, `HEALTH`, `MAP`, `EXPORT`.
- [x] Pełnoekranowy popup `ESPShark Communication Map`.
- [x] `TRAFFIC`: cały zaobserwowany graf, aplikacje rozróżnione kolorami.
- [x] `THREATS`: zwykły ruch jest przygaszony, a węzły i krawędzie związane z
  alertem są oznaczane żółtym albo czerwonym.
- [x] `SERVICES`: pozostają krawędzie z rozpoznanym protokołem aplikacyjnym.
- [x] Rozmiar węzła odzwierciedla wolumen ruchu.
- [x] Zielony oznacza LAN, fioletowy WAN, cyan grupy multicast, a amber
  broadcast lub finding wymagający uwagi.
- [x] Dotknięcie węzła pokazuje rolę, IP/hostname, MAC, flow, usługi, alerty,
  pakiety oraz bajty TX/RX.
- [x] `FILTER NODE` wraca do tabeli pakietów z filtrem MAC dla LAN albo IP dla
  endpointu zewnętrznego.
- [x] Grupowane węzły nie udają pojedynczego endpointu i nie pozwalają ustawić
  nieprecyzyjnego filtra.
- [x] Capture bez zdekodowanych endpointów IP pokazuje wyjaśnienie zamiast
  pustego ekranu.

### 26.3. Testy sprzętowe

- [ ] Zbudować firmware lokalnie i sprawdzić brak warningów `-Werror`.
- [ ] Otworzyć mały PCAP i potwierdzić, że `MAP` mieści się w drugim rzędzie.
- [ ] Porównać liczbę węzłów i relacje z `DEVICES` oraz `FLOWS`.
- [ ] Sprawdzić przełączanie `TRAFFIC`, `THREATS` i `SERVICES` bez wzrostu heap.
- [ ] Kliknąć lokalny węzeł posiadający IPv4 i IPv6; `FILTER NODE` powinien
  zastosować filtr MAC.
- [ ] Kliknąć zewnętrzny IP; filtr powinien obejmować tylko ten endpoint.
- [ ] Sprawdzić, że mDNS/SSDP nie tworzą dziesiątek osobnych węzłów multicast.
- [ ] Przetestować capture z port scan, host sweep, beaconing i worm-like
  spread; właściwe relacje powinny zostać wyróżnione w `THREATS`.
- [ ] Otworzyć handshake-only PCAP; mapa powinna pokazać komunikat o braku
  zdekodowanych endpointów IP.
- [ ] Otworzyć capture przekraczający 48 węzłów lub 128 relacji; sprawdzić
  `OTHER LAN`, `INTERNET +N` oraz `MAP LIMITED`.
- [ ] Wielokrotnie otworzyć i zamknąć mapę, obserwując PSRAM i brak resetu.
- [ ] Ocenić czy etykiety pierwszych 22 najważniejszych węzłów są czytelne;
  pozostałe punkty nadal muszą reagować na dotyk.

### 26.4. Następne możliwe rozszerzenia

- [ ] Klikalne krawędzie z agregatem flow i filtrem całej pary endpointów.
- [ ] Kierunek dominującego transferu oraz strzałki na krawędziach.
- [ ] Zoom/pan i przycisk `FIT` dla bardzo gęstych grafów.
- [ ] Suwak czasu i odtwarzanie zmian grafu w kolejnych oknach capture.
- [ ] Eksport grafu do JSON/GraphML jako część raportu ESPShark.
- [ ] Fingerprint gateway/DNS/DHCP i semantyczne role infrastruktury.

Proponowany commit message po kompilacji i testach:

```text
feat(espshark): add offline communication map

- aggregate cached flows into a bounded PSRAM communication graph
- merge local IPv4 and IPv6 identities by MAC
- collapse multicast, broadcast and low-priority endpoints
- add traffic, threat and service map modes
- highlight alert-related nodes and observed links
- show node evidence and apply packet filters directly from the map
- label sampled and limited views without implying physical topology
```

## 27. MITM — obsługa ukrytego SSID 2026-08-09

- [x] Wybór rekordu z pustym SSID przed otwarciem MITM uruchamia istniejący
  formularz `Hidden Network`.
- [x] Wpisana nazwa jest przypisywana do nadal wybranego rekordu skanowania;
  zachowane pozostają BSSID, kanał, security i pozostałe metadane AP.
- [x] Po zatwierdzeniu MITM otwiera standardowy popup, wyszukuje zapisane hasło
  po podanym SSID i korzysta ze wspólnego `wifi_connect`.
- [x] Pusty SSID nie jest już błędnie raportowany wyłącznie jako zbyt długa
  komenda; awaryjny komunikat brzmi `Enter hidden SSID first`.
- [x] JanOS nie wymaga modyfikacji — flow jest zgodny z połączeniem używanym
  przez upload Wigle i WDGWars.

Testy sprzętowe:

- [ ] Wybrać zabezpieczony ukryty AP, podać SSID i użyć zapisanego hasła.
- [ ] Powtórzyć test z hasłem wpisanym ręcznie.
- [ ] Sprawdzić ukrytą sieć otwartą.
- [ ] Anulować formularz SSID i potwierdzić, że MITM nie startuje.
- [ ] Zweryfikować w logu wpis `MITM: Hidden network resolved as` oraz komendę
  `wifi_connect` z właściwą nazwą.

Proponowany commit message po testach:

```text
fix(mitm): support connecting to hidden Wi-Fi networks

- prompt for the SSID before opening the MITM capture dialog
- preserve the selected AP metadata and BSSID
- reuse saved-password and manual-password connection flows
- report a missing hidden SSID with an accurate UI message
```

## 28. ESPShark — podział Local Devices / Remote Endpoints 2026-08-10

Dotychczasowy licznik `DEVICES` obejmował każdy unicastowy adres IP widoczny
w capture. Publiczne serwery, CDN-y i usługi chmurowe szybko zapełniały twardy
limit 128 wpisów, dlatego wartość `128` nie oznaczała 128 urządzeń w domu.

### 28.1. Model i priorytety

- [x] `Local Devices` obejmuje wyłącznie adresy prywatne/link-local IPv4 oraz
  lokalne/link-local IPv6 rozpoznawane przez analizator.
- [x] Lokalne IPv4 i IPv6 posiadające ten sam użyteczny MAC są prezentowane
  jako jedno urządzenie.
- [x] Pusty, multicastowy i zerowy `00:00:00:00:00:00` MAC nie jest używany
  jako tożsamość urządzenia.
- [x] `Remote Endpoints` zachowuje publiczne IP jako osobne endpointy; nie scala
  ich po MAC, ponieważ w routowanym capture jest to zazwyczaj MAC bramy.
- [x] Po zapełnieniu tabeli nowy lokalny endpoint zastępuje najmniej aktywny
  wpis WAN. Internet nie może już wyprzeć całego lokalnego inwentarza.
- [x] `DEVICE/ENDPOINT TABLE LIMITED` nadal jawnie informuje, że wynik jest
  ograniczoną próbką.

### 28.2. Interfejs i filtrowanie

- [x] Kafelek `DEVICES (N)` pokazuje liczbę unikalnych urządzeń lokalnych,
  zamiast łącznej liczby adresów LAN i WAN.
- [x] Popup inwentarza posiada przełączniki `LOCAL DEVICES` i `REMOTE` z
  osobnymi licznikami.
- [x] Lokalny rekord agreguje aliasy IP, TX/RX i listę zaobserwowanych usług.
- [x] Dotknięcie lokalnego urządzenia filtruje pakiety po MAC, dzięki czemu
  obejmuje jednocześnie jego IPv4 i IPv6.
- [x] Dotknięcie zewnętrznego endpointu nadal filtruje po dokładnym IP.
- [x] Overview i Network Health pokazują osobno liczbę urządzeń lokalnych oraz
  endpointów zewnętrznych.
- [x] WAN pozostaje dostępny w Remote, Flows, Follow Stream, Map i detektorach
  bezpieczeństwa.

### 28.3. Cache i eksport

- [x] Cache podniesiono do schema v4, aby stare indeksy zdominowane przez WAN
  zostały automatycznie uznane za niezgodne i przebudowane.
- [x] Raport JSON v4 otrzymał `inventory.local_devices`,
  `inventory.remote_endpoints` i `inventory.bounded_sample`.
- [x] Istniejąca tablica `devices` pozostaje w raporcie i nadal posiada pole
  `internal`, więc pełne dane endpointów są zachowane do dalszej analizy.
- [x] Bez zmian JanOS.

### 28.4. Testy sprzętowe

- [ ] Otworzyć capture domowej sieci i porównać `DEVICES (N)` z liczbą
  rzeczywistych urządzeń.
- [ ] Sprawdzić, że telefon/komputer z IPv4 i IPv6 jest jednym lokalnym wpisem.
- [ ] Przełączyć `LOCAL DEVICES` / `REMOTE` i sprawdzić osobne listy.
- [ ] Dotknąć lokalnego urządzenia; filtr powinien mieć opis `MAC ...`.
- [ ] Dotknąć WAN; filtr powinien mieć opis `HOST ...` dla jednego IP.
- [ ] Otworzyć PCAP osiągający limit 128 i potwierdzić, że LAN nadal znajduje
  się na liście, a licznik Remote ma znak `+`.
- [ ] Potwierdzić przebudowę starego cache v3 do v4 oraz poprawny ponowny
  `CACHE HIT`.
- [ ] Wyeksportować JSON i sprawdzić obiekt `inventory`.
- [ ] Zbudować firmware lokalnie; Codex nie wykonywał kompilacji.

Proponowany commit message po kompilacji i testach:

```text
feat(espshark): split local devices from remote endpoints

- count private and link-local identities as local devices
- merge local IPv4 and IPv6 aliases by usable MAC address
- keep Internet peers in a separate remote endpoint view
- prioritize local inventory when the endpoint table reaches its limit
- filter local identities by MAC and remote endpoints by IP
- expose separate inventory counts in overview, health and JSON reports
- invalidate legacy analysis caches with schema v4
```

## 29. ESPShark — kompletne Offline Investigation 2026-08-10

Ten etap zmienia ESPShark z samej przeglądarki pakietów w ograniczone zasobowo,
pasywne stanowisko dochodzeniowe. Całość działa na PCAP-ie zapisanym na SD
Tab5 i nie wymaga zmian JanOS. Wynik jest wskazówką opartą o zaobserwowany
ruch, a nie aktywnym skanem ani dowodem, że port jest dostępny z całej sieci.

### 29.1. Workspace Investigation

- [x] Przycisk `INVEST (N)` otwiera jeden workspace z kartami `FINDINGS`,
  `TIMELINE`, `BASELINE` oraz `INTEL`.
- [x] Findings łączą wcześniejsze detektory (port scan, host sweep, ARP,
  beaconing, asymetria transferu, worm-like spread, TCP quality) z nowym
  security posture.
- [x] Dotknięcie findingu ustawia możliwie najdokładniejszy filtr: flow, IP,
  port, DNS albo okno czasu, po czym wraca do tabeli pakietów.
- [x] Timeline pokazuje first-seen urządzeń lokalnych, maksymalnie 32 pierwsze
  istotne flow oraz wszystkie findingi, posortowane po czasie capture.
- [x] Bufory są ograniczone (`96` findingów, `128` zdarzeń) i przechowywane w
  PSRAM; UI jawnie sygnalizuje ograniczenie próbki.

### 29.2. Security posture i detekcje offline

- [x] Zaobserwowane usługi administracyjne: SSH, Telnet, HTTP(S), SMB, RDP,
  VNC i typowe porty paneli. Opis nie udaje aktywnego port scanu.
- [x] Kontakt zewnętrznego endpointu z lokalną usługą administracyjną.
- [x] Wielu nadawców DHCP, LLMNR/NBNS i powierzchnia name poisoning.
- [x] SMB1, nieszyfrowany MQTT, cleartext na urządzeniach IoT/camera i
  kandydaci encrypted DNS/DoT.
- [x] Heurystyczne długie/niestandardowe nazwy DNS oraz istniejące anomalie
  DNS, NXDOMAIN/SERVFAIL i top clients/servers.
- [x] Burst ramek 802.11 deauthentication, jeśli capture zawiera warstwę
  radiową i dekoder widzi takie ramki.
- [x] Wskaźnik jawnego uwierzytelniania (`Basic`, `USER/PASS`, `AUTH
  LOGIN/PLAIN`) bez zachowywania wartości loginu, hasła czy nagłówka.
- [x] TLS ClientHello: wersja, ALPN/SNI, liczba cipherów i extensions oraz
  lokalny stabilny `CH-FNV64`. To fingerprint do porównań na Tab5, nie JA3.
- [x] Dodatkowe klasy usług po sygnaturze/porcie: RDP, VNC, RTSP, CoAP,
  Redis i popularne bazy (MySQL/PostgreSQL/MongoDB), obok HTTP(S), QUIC,
  BitTorrent, MQTT, SMB i pozostałych istniejących protokołów.

### 29.3. Device Dossier

- [x] Każde lokalne urządzenie ma przycisk `PROFILE` i osobny dossier.
- [x] Dossier scala IPv4/IPv6 po MAC oraz pokazuje hostname, rolę heurystyczną,
  adresy, aktywność, TX/RX, flow, usługi, top peers, DNS/SNI i findingi.
- [x] Risk score 0–100 wynika wyłącznie z findingów przypisanych do danej
  tożsamości i zawsze należy go interpretować razem z dowodami.
- [x] Opcjonalny plik `/sdcard/lab/espshark/intel/oui.txt` dodaje producenta.
  Format wiersza: `AA:BB:CC Vendor name`.
- [x] `FILTER MAC` obejmuje wszystkie lokalne aliasy urządzenia.

### 29.4. Lokalne IOC i reguły

- [x] `/sdcard/lab/espshark/intel/ips.txt` — jeden adres IP na linię.
- [x] `/sdcard/lab/espshark/intel/domains.txt` — domena; dopasowanie obejmuje
  dokładną domenę i subdomeny zaobserwowane w DNS/SNI.
- [x] `/sdcard/lab/espshark/intel/forbidden_ports.txt` — jeden port na linię.
- [x] Puste linie i komentarze `#` są ignorowane; po zmianie list należy użyć
  `REANALYZE` albo ponownie otworzyć PCAP.
- [x] Match IOC jest findingiem `CRITICAL` z endpointem/flow umożliwiającym
  przejście bezpośrednio do pakietów.

### 29.5. Baseline i różnice

- [x] `EXPORT -> SET AS BASELINE` zapisuje znany dobry stan do
  `/sdcard/lab/espshark/baseline/home.espbaseline`.
- [x] Zapis jest odporny na typowe błędy przez plik tymczasowy, flush/fsync,
  backup poprzedniej wersji i podmianę z próbą przywrócenia po błędzie.
- [x] `COMPARE BASELINE` oraz karta `BASELINE` pokazują nowe i brakujące
  urządzenia LAN, nowe endpointy WAN, nowe domeny i zmiany zestawu usług.
- [x] Obecnie istnieje jeden profil `home`; ustawienie nowego baseline celowo
  zastępuje poprzedni.

### 29.6. Cache i evidence pack

- [x] Cache/report podniesiono do schema v5; starszy cache zostanie
  automatycznie przebudowany.
- [x] Investigation jest szybko wyliczane ponownie z cache v5, dzięki czemu
  nowe lokalne listy IOC działają bez przechowywania ich wyniku w cache.
- [x] JSON v5 eksportuje flow, fingerprint TLS, wskaźnik cleartext auth,
  findingi i timeline do dalszego przetwarzania na komputerze.
- [x] `EXPORT HTML REPORT` tworzy samodzielny raport w
  `/sdcard/lab/espshark/exports/<capture>_investigation_N.html` z findingami,
  timeline, dossier urządzeń i aktualnym baseline diff.
- [x] `EXPORT FILTERED PCAP` nadal tworzy dowód otwieralny w Wireshark/Zeek;
  najpierw można dotknąć findingu, a potem wyeksportować jego filtr.
- [x] Profil filtrów jest zapisywany oddzielnie przez `SAVE FILTER PROFILE`.

### 29.7. Świadome ograniczenia

- Analizowana jest ograniczona próbka indeksu (obecnie maksymalnie 4096
  pakietów), 512 flow i 128 rekordów endpointów; nagłówek pokazuje
  `INDEX LIMITED`, gdy wynik nie obejmuje całego capture.
- Klasyfikacja portowa ma confidence `LIKELY`; `CONFIRMED` wymaga rozpoznanej
  sygnatury/metadanych. Szyfrowanej zawartości ESPShark nie odszyfrowuje.
- Detektor poświadczeń przechowuje tylko boolowski marker, analizuje dostępny
  początek payloadu i może nie zobaczyć danych podzielonych między segmenty.
- Role, anomalie DNS, beaconing i worm-like spread są heurystykami. Nie są
  automatycznym werdyktem malware/pentest.
- Offline nie potwierdzi PMF/WPA policy bez odpowiednich ramek zarządzających,
  nie wykona aktywnego skanu i nie zastępuje pełnego Zeeka/Wiresharka.

### 29.8. Bramka jakości wydania v5 — oczekuje na wykonanie

Poniższe testy nie oznaczają brakującej implementacji. Ich zaliczenie zmienia
status wydania z `IMPLEMENTED` na `HARDWARE-VERIFIED`.

- [x] Pełny build bieżącego `development` w ESP-IDF 5.4.1 zakończony sukcesem;
  kod przeszedł `-Werror`. Pozostały trzy wcześniejsze ostrzeżenia o nieznanych
  symbolach w `sdkconfig.defaults`, niezwiązane z ESPShark.
- [ ] Otworzyć PCAP bez cache v5; sprawdzić pełny indexing i zapis v5.
- [ ] Otworzyć go ponownie; potwierdzić `CACHE HIT` oraz szybkie odtworzenie
  Investigation.
- [ ] Sprawdzić cztery karty `FINDINGS/TIMELINE/BASELINE/INTEL` i powroty z
  findingów do tabeli pakietów.
- [ ] Otworzyć `LOCAL DEVICES -> PROFILE`; sprawdzić aliasy, role, peers,
  DNS/SNI, risk i `FILTER MAC`.
- [ ] Dodać testowe IP/domenę/port do list intel, wykonać `REANALYZE` i
  potwierdzić CRITICAL IOC match.
- [ ] Dodać prefiks MAC do `oui.txt` i sprawdzić producenta w dossier.
- [ ] Ustawić baseline na znanym dobrym capture, otworzyć inny PCAP i
  sprawdzić wszystkie liczniki różnic.
- [ ] Wyeksportować JSON, HTML i filtered PCAP; sprawdzić pliki na SD oraz
  otwarcie HTML w przeglądarce i PCAP w Wireshark/Zeek.
- [ ] Sprawdzić PCAP z HTTP Basic lub FTP testowym kontem; ma powstać wyłącznie
  marker bez sekretu w cache/JSON/HTML.
- [ ] Wielokrotnie otworzyć/zamknąć dossier i Investigation, obserwując PSRAM,
  task stack oraz brak resetu/watchdoga.

### 29.9. Poprawki po pierwszej kompilacji użytkownika

- [x] Usunięto `-Werror=restrict` w opisie TLS. GCC po inliningu traktował
  `application_detail` i `tls_fingerprint` jako potencjalnie nakładające się
  pola tego samego obiektu `flow`; fingerprint i wersja TLS są teraz kopiowane
  do lokalnych zmiennych przed `snprintf`.
- [x] Usunięto `-Werror=stringop-overread` z `inv_copy`. `strnlen` z limitem
  wynikającym z bufora docelowego powodował fałszywy alarm dla krótkich
  literałów; helper wykonuje teraz jawne kopiowanie znak po znaku do limitu.
- [x] Ponowiona pełna kompilacja przeszła oba komponenty i cały link firmware.

Commit implementacyjny: `1816750 feat(espshark): add offline investigation workspace`.
Jego zakres:

```text
feat(espshark): add offline investigation workspace

- build bounded findings and timeline views from cached PCAP evidence
- add device dossiers with roles, risk, peers, domains and optional OUI data
- detect admin exposure, legacy services, encrypted DNS and credential markers
- recognize RDP, VNC, RTSP, CoAP, Redis and common database services
- match local IP, domain and forbidden-port intelligence lists
- compare captures against a recoverable known-good network baseline
- export investigation data in JSON v5 and standalone HTML reports
- preserve one-tap evidence filtering and Wireshark-compatible PCAP export
- avoid GCC restrict and string-bound false positives in optimized builds
```

## 30. ESPShark — FQDN w Source/Destination i cache — 2026-08-11

### 30.1. Zachowanie interfejsu

- [x] Pasek filtrów tabeli pakietów ma przełącznik `SHOW FQDN` / `FQDN ON`.
- [x] Przy włączonej opcji znany FQDN jest pierwszą linią endpointu, a druga
  linia zachowuje dokładny adres IP i port.
- [x] Brak nazwy, ruch bez IP oraz nierozpoznana odpowiedź DNS pozostawiają
  dotychczasowy zapis IP/MAC bez zgadywania nazwy.
- [x] Przełączanie odświeża tylko widoczną stronę pakietów i nie uruchamia
  ponownego skanowania PCAP.
- [x] Tryb jest domyślnie wyłączony przy otwarciu nowego capture, więc nie
  zmienia dotychczasowego widoku bez decyzji użytkownika.

### 30.2. Źródło i wiarygodność nazw

- [x] Analizator buduje ograniczoną tabelę maksymalnie 128 relacji
  `adres IPv4/IPv6 → FQDN` wyłącznie z odpowiedzi DNS/mDNS obecnych w PCAP.
- [x] Parser zachowuje pierwszy adres A/AAAA niezależnie od tego, czy przed
  nim w odpowiedzi wystąpił CNAME.
- [x] Końcowa kropka DNS jest usuwana wyłącznie na potrzeby prezentacji.
- [x] Jedno IP może reprezentować wiele usług wirtualnych; v6 świadomie
  pokazuje pierwszą zaobserwowaną nazwę i nie traktuje jej jako trwałej
  własności serwera.
- [x] Po zapełnieniu tabeli ustawiana jest flaga `dns_name_limited`; kolejne
  pakiety nadal są analizowane i wyświetlane po IP.

### 30.3. Cache i raport

- [x] Raport JSON ma schema v6, a bieżący cache analizy schema v7; cache v6
  jest automatycznie uznawany za niezgodny i przebudowywany przy pierwszym
  otwarciu capture po korekcie wieloadresowych odpowiedzi DNS.
- [x] Mapa nazw jest przechowywana w `pcap_flow_analysis_t`, dlatego `FQDN ON`
  działa również po `CACHE HIT` bez ponownego parsowania DNS.
- [x] JSON v6 eksportuje `dns_names` z adresem, nazwą i liczbą obserwacji oraz
  `flow_limits.dns_names_limited`.
- [x] Filtry i eksport filtered PCAP nadal działają na dokładnych adresach IP;
  FQDN jest warstwą prezentacji, nie zmienia semantyki dowodu.

### 30.4. Bramka jakości wydania v6

- [x] Pełny build `development` w ESP-IDF 5.4.1 zakończony sukcesem bez błędów
  i bez warningów kompilatora objętych `-Werror`.
- [ ] Otworzyć PCAP zawierający DNS A, AAAA oraz CNAME → A/AAAA.
- [ ] Włączyć `SHOW FQDN` i potwierdzić układ `nazwa` nad `IP:port` w obu
  kolumnach Source i Destination.
- [ ] Sprawdzić, że endpoint bez mapowania pozostaje adresem IP.
- [ ] Zamknąć i ponownie otworzyć capture; potwierdzić cache v7, `CACHE HIT`
  i identyczne mapowania.
- [ ] Wyeksportować JSON v6 i zweryfikować `dns_names` oraz flagę limitu.
- [ ] Przetestować capture bez DNS, uszkodzoną odpowiedź DNS i ponad 128
  unikalnych mapowań bez resetu, wycieku pamięci ani blokowania LVGL.

## 31. ESPShark — interaktywny DNS drill-down — 2026-08-11

### 31.1. Zachowanie interfejsu

- [x] `DNS` zachowuje dotychczasowe liczniki, RCODE, QTYPE, top answers,
  klientów i serwery, ale `TOP DOMAINS` jest listą klikalną.
- [x] Kliknięcie domeny pokazuje `WHO QUERIED IT`, `RESOLVED ADDRESSES` oraz
  `RELATED FLOWS` posortowane malejąco według liczby bajtów.
- [x] Klient jest prezentowany jako `hostname (IP)`, gdy nazwa jest dostępna,
  albo jako dokładny adres IP bez zgadywania.
- [x] Każdy powiązany flow ma `FILTER`, `FOLLOW` ASCII i `HEX`; filtr wraca do
  tabeli pakietów, a `BACK TO DNS` wraca do Top Domains.
- [x] Brak DNS, brak adresu A/AAAA i brak skorelowanego flow mają jawne puste
  stany zamiast nieaktywnego lub pustego popupu.

### 31.2. Korelacja i limity

- [x] Szczegół domeny jest wyliczany na żądanie z maksymalnie 4096 już
  zindeksowanych rekordów; nie skanuje całego pliku i nie korzysta z sieci.
- [x] Flow jest kandydatem, gdy łączy się z adresem A/AAAA przypisanym domenie
  albo ma dokładnie zgodny TLS SNI/HTTP Host. Współdzielony adres CDN może
  skorelować dodatkowe flow, dlatego UI i raport traktują wynik jako wskazówkę.
- [x] Pojedynczy drill-down przechowuje maksymalnie 16 unikalnych klientów,
  16 adresów i 24 największe flow; przekroczenie limitu jest jawnie oznaczane.
- [x] Funkcja używa istniejącego indeksu, mapy DNS i tabeli flow, więc nie
  zmienia schema cache/report v6 i działa po `CACHE HIT`.

### 31.3. Bramka jakości interaktywnego DNS

- [x] Pełny build `development` w ESP-IDF 5.4.1 zakończony sukcesem; obraz
  aplikacji ma rozmiar `0x2ecf30`, a najmniejsza partycja app ma 71% wolnego.
- [ ] Na capture DNS kliknąć domenę i porównać klientów oraz A/AAAA z
  Wiresharkiem; następnie sprawdzić `BACK TO DNS`.
- [ ] Dla skorelowanego TCP/UDP uruchomić `FILTER`, `FOLLOW` i `HEX` oraz
  potwierdzić, że wszystkie trzy akcje wskazują ten sam numer flow.
- [ ] Sprawdzić domenę bez odpowiedzi, odpowiedź bez późniejszego flow,
  współdzielony adres CDN i capture bez DNS.
- [ ] Zamknąć i ponownie otworzyć capture z `CACHE HIT`; drill-down ma zwrócić
  te same wyniki dla indeksowanej próbki bez ponownej pełnej analizy.

## 32. ESPShark — korekta `SHOW FQDN` — 2026-08-11

### 32.1. Przyczyna

Pierwsza implementacja zapisywała tylko pierwszy adres A/AAAA z odpowiedzi.
Wieloadresowa odpowiedź CDN mogła więc zawierać nazwę, ale właściwy flow szedł
do drugiego lub kolejnego IP i tabela nie znajdowała mapowania. DNS po TCP
większy od 512 bajtów był oznaczany jako DNS, lecz nie wykorzystywano jego
częściowo dostępnych rekordów. Tabela pomijała też dokładny SNI/HTTP Host
przypisany do bieżącego flow.

### 32.2. Poprawione zachowanie

- [x] Jedna odpowiedź przekazuje do mapy maksymalnie cztery unikalne adresy
  A/AAAA; limit w pakiecie jest jawny.
- [x] DNS/TCP większy od okna dekodera jest analizowany do granicy dostępnych
  danych zamiast odrzucenia całej odpowiedzi.
- [x] Odpowiedź mDNS/DNS bez question może użyć owner name pierwszego A/AAAA.
- [x] Source/Destination najpierw używa SNI/HTTP Host dokładnego flow, potem
  mapy DNS/device, zachowując IP i port w drugiej linii.
- [x] `SHOW FQDN` pokazuje liczbę `name hints`; aktywne zero ma kolor bursztynowy
  i pager pokazuje `FQDN 0 hints`, więc brak danych nie wygląda jak awaria UI.
- [x] Cache schema v7 automatycznie unieważnia niepełny cache v6.

### 32.3. Weryfikacja właściciela projektu

- [x] Kontrola statyczna zmienionych plików i `git diff --check` bez błędów.
- [ ] Wykonać kompilację ESP-IDF — Codex nie uruchamia jej bez jawnego polecenia.
- [ ] Otworzyć capture ponownie i poczekać na automatyczną przebudowę cache v7.
- [ ] Sprawdzić liczbę `name hints`, włączyć `FQDN ON` i potwierdzić nazwy nad
  IP:port dla DNS wieloadresowego oraz HTTPS z SNI.
