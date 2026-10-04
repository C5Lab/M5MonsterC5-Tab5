# Tool Order — TODO

Data ustaleń: 2026-10-04.
Status: zaimplementowano 2026-10-04. Kompilacja ESP32-P4 i testy host/LVGL przeszły. Użytkownik potwierdził działanie na Tab5 i zachowanie kolejności po restarcie.

## Cel i ustalenia

- Nowa opcja **Settings → Tool Order** umożliwia własną kolejność kafli.
- Edytor pokazuje siatkę kafli i pozwala przesuwać je przez przytrzymanie i przeciąganie, z podglądem zmian na żywo.
- Save zapisuje i stosuje kolejność bez restartu; Cancel odrzuca zmiany.
- Reset to Default przywraca domyślną kolejność w edytorze; wymaga Save, aby utrwalić reset.
- Podczas edycji kafle nie uruchamiają narzędzi. Przeciąganie przy krawędzi przewija siatkę.
- Uwzględnić wszystkie cztery orientacje ekranu. Zapisujemy kolejność, nie współrzędne; liczba kolumn wynika z orientacji.
- Obrót podczas przeciągania anuluje bieżący gest, zachowuje wcześniejsze zmiany robocze i odświeża układ.
- Zapewnić możliwość zmiany kolejności również z klawiatury.

## Zakres implementacji

- Wspólna kolejność dla zakładek Monster, osobna dla INTERNAL.
- Jedna kolejność dla wszystkich orientacji.
- Edycja przez Settings → Tool Order. Skrót przez przytrzymanie na ekranie głównym pozostaje opcjonalny; użytkownik nie rozstrzygnął tego pytania.
- Pierwsza wersja nie zmienia kolejności Settings ani podmenu.

## NVS

- Namespace: `tool_order`; klucze: `monster`, `internal`.
- Każdy klucz zawiera blob: wersja formatu, liczba elementów, tablica stałych ID narzędzi.
- ID są niezależne od etykiet i indeksów obiektów LVGL; nie przypisywać ponownie wycofanego ID innemu narzędziu.
- Przeciąganie zmienia tylko RAM. Save używa `nvs_set_blob()` i `nvs_commit()`; błąd zapisu pozostawia edytor otwarty z komunikatem.
- Przy odczycie sprawdzać format, wersję, długość i liczbę elementów; usuwać duplikaty i nieznane ID; nowe narzędzia dopisywać na końcu.
- Niedostępne NFC/Sub-GHz pomijać w widoku bez pustych miejsc, zachowując ich ID i kolejność w zapisie.
- Brak zapisu oznacza domyślną kolejność z kodu. Reset + Save usuwa klucz wybranego zestawu i zatwierdza zmianę.
- Zaprojektować rozróżnienie błędnego zapisu od błędu odczytu NVS; bezpieczny fallback bez utraty dostępu do narzędzi.

## Plan implementacji

- [x] Sprawdzić aktualny stan repo i potwierdzić nierozstrzygnięty zakres powyżej.
- [x] Dodać `main/tile_order.c/.h`: stałe ID, domyślne układy, przesuwanie i normalizacja zapisanej kolejności.
- [x] Dodać wersjonowany zapis/odczyt NVS i reset wybranego zestawu.
- [x] Zintegrować kolejność z tworzeniem i odświeżaniem siatek Monster oraz INTERNAL.
- [x] Dodać Tool Order w Settings oraz edytor z wyborem zestawu i Save / Cancel / Reset to Default.
- [x] Dodać przeciąganie, podgląd miejsca docelowego, przesuwanie pozostałych kafli i przewijanie przy krawędzi.
- [x] Obsłużyć obrót podczas edycji i nawigację klawiaturą.
- [x] Przetestować logikę: przesunięcia, duplikaty, nieznane ID, nowe narzędzia, niepoprawny zapis i reset.
- [x] Skompilować dla ESP32-P4; sprawdzić w LVGL cztery orientacje, przeciąganie, obrót podczas gestu, scroll, klawiaturę i błędy NVS.
- [ ] Sprawdzić na fizycznym Tab5: dotyk, trwałość zapisu po restarcie, zmianę motywu, wykrywanie NFC/Sub-GHz i kolejność w zakładkach Monster.

## Punkty integracji znalezione podczas analizy

- `main/main.c`: `create_uart_tiles_in_container()` — główna siatka Monster; warunkowe NFC/Sub-GHz; osobny dashboard poniżej siatki.
- `show_internal_tiles()` — trzy kafle INTERNAL.
- `show_settings_page()` / `settings_tile_event_cb()` — wejście do nowego edytora.
- `rebuild_all_home_tiles()` / `refresh_theme_visuals()` — odbudowa ekranów musi stosować własną kolejność.
- `load_screen_settings_from_nvs()` — istniejący wzorzec odczytu ustawień.
- `main/app_keyboard_navigation.c` — nawigacja uwzględnia drzewo dzieci LVGL; sprawdzić zgodność z nową kolejnością.
- LVGL udostępnia `lv_obj_move_to_index()`; ocenić użycie do aktualizacji kolejności istniejących kafli.

Zmiana jest lokalna dla Tab5, bez zmian protokołu UART i firmware Monsterów. Nie rozszerzać dużego `main.c` o całą logikę modelu kolejności.

W chwili analizy repo zawierało niezacommitowane zmiany OTA i binariów. Zachować je i sprawdzić ponownie przed implementacją.

## Wynik weryfikacji

- `tile_order_test.c`: przesuwanie, normalizacja, kodowanie/odczyt i aktualizacja zestawu ID; GCC z AddressSanitizer i UndefinedBehaviorSanitizer — PASS.
- `tests/tile_order_ui`: produkcyjny edytor i rzeczywiste LVGL 9.4 z backendem NVS do symulacji błędów — PASS.
- `test_wpa_psk_auditor_contract.py`: 74 testy — PASS.
- ESP-IDF 5.4.1, target ESP32-P4 — build PASS. Firmware: `build/M5MonsterC5-Tab5.bin`.
- Niezależny przegląd kodu: poprawiono ograniczenie autoscrolla, anulowanie gestu po półobrocie i naprawę uszkodzonego zapisu przez Save.
- Układ edytora obejrzano na renderach LVGL w pionie i poziomie. Bez flashowania urządzenia.
