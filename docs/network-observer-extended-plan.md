# Network Observer Extended — plan funkcjonalny

Status: propozycja do omówienia; bez zmian implementacyjnych.

## Cel i zakres

Pokazać dane JanOS `ext_ver=1` w Network Observerze i pozwolić użytkownikowi wybrać widoczne pola oraz ich kolejność. Klasyczny skan i jego planowany dodatek WPS pozostają osobnym zadaniem. Nie dodajemy nowych funkcji RF ani fingerprintingu OS.

## Widok główny

- Nagłówek: Back, Network Observer, Options, Export, Start, Stop. W orientacji pionowej tytuł i przyciski zajmują osobne rzędy, aby uniknąć kolizji.
- Nazwa sieci i przycisk liczby klientów są stałe. BSSID jest kluczem danych niezależnie od widoczności pola.
- Ukryta sieć: `(Hidden)`; po odkryciu `(Hidden) - nazwa`, z nazwą na niebiesko. Nie zmieniamy historycznego SSID używanego przez starsze odpowiedzi.
- Opcjonalne pola AP zawijają się do kolejnych wierszy. Nie stosujemy poziomego przewijania ani zmniejszania fontu przy wielu polach.
- Rozwinięcie klientów i kliknięcie AP pozostają oddzielnymi akcjami.

## Options

Modal z zakładkami Networks i Clients, przewijaną zawartością oraz stałym paskiem Cancel / Restore defaults / Apply.

Networks: uporządkowane wiersze z checkboxem i przyciskami góra/dół. Pola: Security, PMF, WPS, Channel/band, AP RSSI, Last seen, BSSID, Uptime, Vendor, Pairwise ciphers, Group cipher, AKM, SSID discovery source. Nazwa i liczba klientów nie są konfigurowalne.

Clients: analogiczne sterowanie polami Vendor, RSSI, Last seen. MAC pozostaje stały. Zakres nie obejmuje cech klienta, których JanOS nie raportuje.

Przyciski góra/dół nieaktywne na granicach listy. Kolejność dotyczy pól informacji, nie sortowania sieci lub klientów. Wyłączone pola zachowują pozycję na wypadek ponownego włączenia.

Domyślne pola AP w kolejności: Security, PMF, WPS, Channel/band, AP RSSI, BSSID, Uptime, Vendor. Last seen i szczegółowe pola wyłączone. WPS ma etykietę WPS / WPS not advertised / WPS unknown, bez sugerowania podatności.

Domyślne pola klienta: Vendor, RSSI, Last seen.

Zmiany są robocze do Apply. Cancel nie zmienia widoku ani NVS. Restore defaults zmienia roboczy zestaw; zapis następuje dopiero po Apply. Apply nie wysyła komend RF i nie restartuje obserwacji. Aktualizacje UART trwają, gdy Options jest otwarte.

Preferencje wspólne dla transportów, trwałe w NVS, wersjonowane i walidowane (duplikaty/nieznane identyfikatory/uszkodzone dane wracają do poprawnego zestawu). Zapis tylko po zmianie. Widoczność Vendor korzysta z istniejącej usługi rozpoznawania; nie tworzymy drugiego sprzecznego ustawienia. Przy wyłączonym rozpoznawaniu pole wyjaśnia brak danych.

## Szczegóły AP

Popup dopasowany do ekranu, z nagłówkiem, zakładkami i dotychczasowym paskiem akcji. Przewija się zawartość zakładki; przyciski pozostają dostępne.

- Overview: pełne SSID, hidden, BSSID, kanał/pasmo, RSSI, age, uptime, vendor, źródło odkrycia nazwy i źródło profilu.
- Security: reklamowany profil, listy AKM/cipher i PMF, oddzielnie RSN i legacy WPA. Reklamowany profil nie oznacza potwierdzonego użycia przez klientów.
- WPS: obecność, stan, metody konfiguracji, setup locked, selected registrar, manufacturer/model/device name, jeśli dostępne. Przy WPS absent pojedynczy komunikat zamiast pustej listy atrybutów.
- Clients: istniejąca nawigacja klienta, MAC/vendor/RSSI/age według preferencji Clients.
- Rozwijane Technical details w odpowiednich zakładkach: surowe selektory, status ramki/IE, truncated, nieznane wartości. Wszystkie otrzymane pola mają reprezentację; pola unknown można grupować w komunikat.

Options nie ukrywa danych w zakładkach Overview/Security/WPS. Nie personalizujemy kolejności zakładek w pierwszej wersji.

## Dane i parser

- Rozszerzone zapytania w zwykłym i fokusowanym odpytywaniu: show_sniffer_results extended lub show_sniffer_results_vendor extended.
- Tryb zgodności ze starszym JanOS: potwierdzony brak obsługi prowadzi do starych komend, z informacją Extended unavailable. Timeout/utrata odpowiedzi nie są automatycznie dowodem braku obsługi. Możliwości ustalane per transport, ponownie po reconnect/zmianie firmware.
- Dopasowanie AP po pełnym BSSID. Identyczne nazwy i kanały nie łączą wpisów. AP pominięty w skanie może powstać jako wpis bez scan_index; działania wymagające indeksu muszą najpierw ustalić prawidłową tożsamość.
- Parser oddziela stary prefiks od dodatkowych key=value i toleruje kolejność oraz nieznane klucze.
- Bufor zgodny z udokumentowanym maksimum protokołu JanOS, nie tylko 1006 znakami z próbki. Niepełna/przepełniona linia jest odrzucana do końca linii i nie jest częściowo interpretowana.
- Komunikaty Sniffer packet count i inne znane logi między AP a klientami nie kasują bieżącego przypisania. Uszkodzony nagłówek AP kasuje przypisanie, aby nie dopisać jego klientów do poprzedniego AP.
- Dekodowanie hex z walidacją długości i bez wstrzykiwania kodów recolor LVGL. Niepoprawny UTF-8/bajty kontrolne są prezentowane bezpiecznie, a oryginalne bajty zachowane do eksportu.
- Wartości valid/absent/unknown/invalid/truncated są rozróżniane. PMF nie jest wyliczane z brakujących pól. Surowe selektory mają mapowanie z fallbackiem dla nieznanych OUI/type.
- Age dotyczy ostatniej obserwacji, nie dowodu rozłączenia. Rosnący wiek obliczany od czasu odebrania snapshotu; pauza zamraża prezentację snapshotu i jest oznaczona.
- Aktualizacja nie zamyka popupów, nie zmienia zakładki i nie gubi scrolla/rozwinięcia klientów. Dotychczasowy cykl stop/back i izolacja kontekstów transportów zostają zachowane.

## Eksport

Widoczność pól nie wpływa na eksport. Zachować istniejące kolumny i dopisać rozszerzone dane do CSV; uwzględnić dane klientów w dotychczasowym sposobie eksportowania ich rekordów. Zachować surowe selektory i źródła, poprawnie escapować tekst.

## Etapy wykonania

1. Uzgodnić finalny kontrakt JanOS i maksymalne długości; zabezpieczyć fixture z dostarczonego logu.
2. Model danych i parser extended wraz z testami rzeczywistego logu, pustych SSID, dwóch BSSID o tym samym SSID/kanale, wtrąconych logów, błędnego hex i przepełnienia.
3. Podłączyć oba tryby odpytywania i zgodność starego JanOS, zachowując istniejące mechanizmy transportu.
4. Dodać Options, uporządkowane pola i zapis preferencji; sprawdzić Cancel/Apply/defaults/reboot.
5. Wprowadzić prezentację główną i zakładki AP; rozszerzyć eksport.
6. Sprawdzić emulator w obu orientacjach i regresje observer exit, incremental UI, station navigation oraz vendor.
7. Ręczny test na urządzeniu: trzy ukryte AP, odkrycie nazw, identyczne SSID, długa lista/metody WPS, otwarte Options podczas odpytywania, popup focus, stop/back, restart i zmiana transportu.

## Warunki akceptacji

- Żadna sieć nie jest łączona z inną po SSID/kanale, gdy dostępny jest BSSID.
- Użytkownik może zmienić widoczność i kolejność pól oraz odtworzyć domyślne ustawienia.
- Wszystkie dane extended można znaleźć w szczegółach lub eksporcie.
- UI mieści się w landscape i portrait; lista zawija metadane, szczegóły przewijają zawartość.
- Brak danych nigdy nie staje się fałszywą informacją o wyłączonej funkcji.
- Starszy JanOS zachowuje działający podstawowy Observer.

## Implementacja i test sprzętowy

Zaimplementowano odbiór Extended w obu pętlach Observera, identyfikację po BSSID, nazwę ukrytego AP, Options z preferencjami NVS, zakładki szczegółów oraz dodatkowe kolumny eksportu. Klasyczny scan pozostaje poza zakresem tej zmiany; WPS można dodać osobno.

Walidacja: build ESP-IDF zakończony powodzeniem (66% wolnej partycji), 28 testów Observera przechodzi przy dwóch pominiętych testach wymagających lokalnego kompilatora hosta. Emulator potwierdza Apply/Cancel/defaults, kolejność i widoczność pól, odtworzenie preferencji, zakładki szczegółów w obu orientacjach oraz stop/back we wszystkich czterech obrotach. Test na fizycznym urządzeniu pozostaje do wykonania.

Przed użyciem na urządzeniu sprawdzić:

1. Trzy ukryte AP: nazwa pojawia się jako `(Hidden) - nazwa`, klient pozostaje przy właściwym BSSID.
2. Options: zmiana kolejności i widoczności AP/klientów, Cancel, Restore defaults, Apply i restart Tab5.
3. Overview/Security/WPS/Clients podczas odpytywania, długie nazwy i pełne dane WPS.
4. Stop zamraża snapshot, Back zatrzymuje aktywną sesję po wyborze Stop and exit.
5. Starszy JanOS: odrzucenie `extended` powoduje ponowienie starego polecenia; UART oraz pozostałe używane transporty działają.
6. Eksport zachowuje stare kolumny i surowe informacje Extended niezależnie od Options.
