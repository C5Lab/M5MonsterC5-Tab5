# Katalog pól tekstowych UI

Stan audytu: 2026-09-24. Przegląd statyczny natywnego UI LVGL na Tab5 w `main/`, bez zależności, testów, webowego HTML i kopii emulatora. Numery linii w tabelach pochodzą z audytu, sprzed rozszerzenia stylu kursora na wszystkie pola. Dokument opisuje kontrolki, nie sposób uruchamiania funkcji narzędzi.

## Liczby i zakres

- **23 miejsca tworzenia `lv_textarea`**: 22 w `main/main.c`, jedno w `main/screens/subghz_text_input_popup.c`.
- Po rozwinięciu wywołań trzech fabryk w `main.c`: **32 wystąpienia pól w kodzie tworzącym ekrany**. Dwie alternatywne gałęzie Nmap tworzą to samo pole hasła, dlatego katalog obejmuje **31 logicznych pól tekstowych**.
- Dodatkowo jedna fabryka `lv_spinbox` tworzy **6 logicznych pól liczbowych** Scan Setup (min/max dla Grove, USB i MBus).
- To liczba rodzajów pól/pozycji formularzy, **nie liczba obiektów jednocześnie w pamięci**. Część pól jest warunkowa, okna powstają ponownie, a konteksty zakładek mogą mieć własne instancje.
- Wszystkie znalezione `lv_textarea` są edytowalne i jednolinijkowe. Nie znaleziono konsoli/logu zbudowanego z textarea ani dodatkowej własnej klasy pola tekstowego. Listy, dropdowny, suwaki i przełączniki nie są wliczone jako input boxy tekstowe.

## Pełny katalog pól tekstowych

`C+F` = własny handler aktywacji podpięty pod `CLICKED` i `FOCUSED`; `C` = tylko `CLICKED`; `F` = tylko `FOCUSED`. Każda grupa poniżej korzysta z `app_keyboard_create` / `app_keyboard_set_textarea`; przy polach współdzielących klawiaturę binding jest przełączany na aktywne pole.

| Ekran / okno | Pole | Źródło pola lub wywołania fabryki | Lokalna aktywacja / uwaga |
|---|---|---|---|
| Scan → Filter | SSID CONTAINS | `main/main.c:8132` | C+F; `scan_filter_keyboard` |
| Wspólne okno ukrytego SSID | SSID | `main/main.c:9836` | C+F; `hidden_ssid_keyboard`; współdzielone przez kilka ekranów |
| MITM | Password | `main/main.c:10902` | C oraz VALUE_CHANGED; `mitm_keyboard` |
| GITM | Upstream pass | `main/main.c:12982` | C+F; `gitm->keyboard`; pole warunkowo widoczne |
| GITM | AP SSID | `main/main.c:13011` | C+F; ta sama klawiatura |
| GITM | AP password | `main/main.c:13040` | C+F; ta sama klawiatura; pole warunkowo widoczne |
| GITM | PCAP prefix | `main/main.c:13053` | C+F; ta sama klawiatura |
| Rogue GITM | Uplink pass | `main/main.c:13605` | C+F; `rogue_gitm_keyboard` |
| Rogue GITM | Mirror pass | `main/main.c:13608` | C+F; ta sama klawiatura |
| Nmap Port Scanner | Password | `main/main.c:15990`, `main/main.c:16099` | C; `nmap_keyboard`; dwie alternatywne gałęzie tworzenia jednego pola |
| ARP Poison | Password | `main/main.c:16459` | C; `arp_keyboard`; pole warunkowe |
| Phishing Portal | Enter SSID | `main/main.c:23933` | Handler przez ALL, obsługuje focus; `phishing_portal_keyboard` |
| Wardrive → WiGLE, dane Wi-Fi | SSID | `main/main.c:26416` | C+F; `wardrive_wigle_keyboard`; tylko wariant `with_ssid` |
| Wardrive → WiGLE, dane Wi-Fi | WiFi password | `main/main.c:26440` | C+F; ta sama klawiatura |
| Home networks | SSID | `main/main.c:29297` | F; DEFOCUSED/READY chowają klawiaturę; `home_mgmt_keyboard` |
| Home networks | Password | `main/main.c:29305` | Jak wyżej |
| Home networks | BSSID, opcjonalny | `main/main.c:29313` | Jak wyżej |
| Wardrive → Setup | Channels → custom | `main/main.c:31310` | F; DEFOCUSED/READY chowają klawiaturę; `wardrive_setup_keyboard`; widoczne przy custom |
| Wardrive → MAC Blacklist | MAC address | `main/main.c:32385` | F; DEFOCUSED/READY chowają klawiaturę; `wardrive_blacklist_keyboard` |
| Rogue AP | WiFi password | `main/main.c:38813` | F; `rogue_ap_keyboard`; pole warunkowe |
| WPA-SEC Upload, dane Wi-Fi | SSID | `main/main.c:41083` | C+F; `wpasec_keyboard`; tylko wariant `with_ssid` |
| WPA-SEC Upload, dane Wi-Fi | WiFi password | `main/main.c:41107` | C+F; ta sama klawiatura |
| Beacon SSIDs → Add New SSID | SSID | `main/main.c:51040` | C+F; `beacon_ssids_keyboard` |
| Monster OTA | WiFi SSID | `main/main.c:57284` | C+F; `g_ota.keyboard` |
| Monster OTA | WiFi password | `main/main.c:57311` | C+F; ta sama klawiatura |
| Monster OTA → adres ręczny | IP | `main/main.c:57345` | C+F; ta sama klawiatura |
| Monster OTA → adres ręczny | Netmask | `main/main.c:57347` | C+F; ta sama klawiatura |
| Monster OTA → adres ręczny | Gateway | `main/main.c:57349` | C+F; ta sama klawiatura |
| Monster OTA → adres ręczny | DNS | `main/main.c:57351` | C+F; ta sama klawiatura |
| SD Admin | WPA2 password | `main/main.c:58028` | C+F; dodatkowo INSERT; `sd_admin_keyboard` |
| Sub-GHz → Manage → Rename signal | Nazwa sygnału | `main/screens/subghz_text_input_popup.c:131`; wywołanie `main/screens/subghz_manage_screen.c:451` | Binding od razu przy otwarciu; klawiatura w kontekście popupu |

## Fabryki i okna współdzielone

- `gitm_make_field`, definicja `main/main.c:12723`, tworzenie textarea `:12744`: cztery wywołania `:12982`, `:13011`, `:13040`, `:13053`.
- `rogue_gitm_pass_row`, definicja `main/main.c:13468`, tworzenie textarea `:13487`: dwa wywołania `:13605`, `:13608`.
- `ota_make_textarea`, definicja `main/main.c:56847`, tworzenie textarea `:56849`: sześć wywołań `:57284`, `:57311`, `:57345`, `:57347`, `:57349`, `:57351`.
- `subghz_show_text_input_popup`, definicja `main/screens/subghz_text_input_popup.c:82`: obecnie jedno wywołanie aplikacji, `main/screens/subghz_manage_screen.c:451` (Rename signal). Jest to współdzielony interfejs popupu, ale aktualnie nie zwiększa liczby pól przez wiele wywołań.
- `show_hidden_ssid_popup`, definicja `main/main.c:9715`: siedem wywołań, `:10098` (ARP), `:10125` (MITM), `:10139` (GW), `:10165` (Rogue GITM), `:10214` (Rogue AP), `:26267` (Wardrive/WiGLE), `:40966` (WPA-SEC). Nadal jedna logiczna kontrolka SSID we wspólnym popupie.
- `wardrive_wigle_create_credentials_prompt`, definicja `main/main.c:26384`: wywołania `:26825` (SSID+hasło), `:26931`, `:27077` (samo hasło).
- `wpasec_create_credentials_prompt`, definicja `main/main.c:41058`: wywołania `:41401` (SSID+hasło), `:41501`, `:41598` (samo hasło).

## Pola liczbowe

`create_scan_time_spinbox_row` (`main/main.c:53416`) tworzy `lv_spinbox` w `:53451`. Użytkownik ma sześć pozycji, zależnie od wykrytych modułów:

| Okno Scan Setup → Channel Scan Time | Minimum | Maksimum |
|---|---|---|
| Grove | `main/main.c:53584` | `main/main.c:53585` |
| USB | `main/main.c:53595` | `main/main.c:53596` |
| MBus | `main/main.c:53606` | `main/main.c:53607` |

Zakres 100–1500 ms, krok przycisków 50 ms. Te kontrolki mają własne przyciski minus/plus i nie są podpięte do `app_keyboard_set_textarea`; nie należy obiecywać im identycznej obsługi tekstowej jak textarea.

## Pola wyglądające jak input, ale tylko wyświetlające tekst

Logi/statusy używają `lv_label`, nie edytowalnego pola: m.in. GPS Debug `main/main.c:31898`, log operacji porządkowania danych `:35515`, monitor OTA `:56565`, status/log Handshaker `:14264` i `:23381`. Listy wyników i zapisanych sieci również nie są polami do wpisywania. Kursor tekstowy nie powinien się w nich pojawiać.

## Obserwacje dotyczące kursora

- Po potwierdzeniu pilotażu na sprzęcie rozszerzono `app_keyboard_style_cursor` na wszystkie 31 logicznych pól tekstowych i 6 pól liczbowych z katalogu: kursor 2 px w kolorze tekstu, miganie 400 ms przy fokusie, przezroczysty po utracie fokusu. Styl jest podpięty w 23 miejscach tworzenia textarea i w jednej fabryce spinboxów; obejmuje też pola generowane przez fabryki i popup Sub-GHz. Obsługa wpisywania oraz przycisków plus/minus pozostaje dotychczasowa.
- W stanie sprzed pilotażu kod `main/` nie miał własnego stylowania `LV_PART_CURSOR` ani jawnego ustawienia czasu migania czy `cursor_click_pos`. Poniższe ustalenia opisują ten stan audytu.
- Pola mają zwykle ręcznie ustawione ciemne tło i jasny tekst, natomiast konfiguracja włącza domyślny jasny motyw LVGL. Domyślny tryb aplikacji jest ciemny (`main/main.c:1700`), tła mają m.in. kolory `#050A14` i `#0C1A2A` (`:203`, `:205`), a kursor motywu jasnego dziedziczy ciemnoszary kolor tekstu. To wskazuje na problem kontrastu.
- Domyślny motyw LVGL wiąże styl kursora ze stanem `LV_STATE_FOCUSED` i ustawia czas animacji 400 ms. Samo wskazanie pola przez `app_keyboard_set_textarea` nie ustawia jawnie focusu. Nie oznacza to, że kliknięcie nie ustawia focusu: bazowa obsługa `LV_EVENT_FOCUSED` w LVGL robi to automatycznie. Lokalna obsługa aktywacji klawiatury jest niejednolita (C, F, C+F, binding od otwarcia); Tab w warstwie wspólnej ustawia focus jawnie. Szczególnej uwagi wymaga gotowość pola od razu po otwarciu popupu, bez dotknięcia.
- Są to ustalenia z kodu i prawdopodobne przyczyny słabo widocznej gotowości do wpisywania. Katalog nie jest testem wizualnym na Tab5 i nie potwierdza zachowania każdego ekranu na sprzęcie.

## Weryfikacja katalogu

Przeszukano wszystkie źródła C/H pod `main/` pod kątem tworzenia textarea/spinbox, klas niestandardowych, API kursora, powiązań klawiatury i wywołań fabryk. Zestawiono miejsca tworzenia z etykietami, warunkami widoczności i handlerami focusu. Sam audyt nie zmieniał kodu aplikacji ani nie uruchamiał jej funkcji. Po rozszerzeniu stylu kursora ponownie sprawdzono pokrycie wszystkich 24 miejsc tworzenia kontrolek.
