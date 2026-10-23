# Wi-Fi STA MAC w Tab5

Ekran **Settings → Wi-Fi MAC** steruje polityką adresu MAC interfejsu Wi-Fi STA w podłączonym JanOS. Gdy Tab5 rozpoznaje jednego Monstera na **Grove**, **USB** albo **MBus**, ekran wybiera go i odczytuje `sta_mac status` automatycznie po otwarciu. Przy kilku rozpoznanych urządzeniach wybierz port; odczyt zacznie się po wyborze. Przycisk **Refresh** pozwala ponowić odczyt. Ustawienie zapisuje JanOS w swojej pamięci NVS; nie jest to ustawienie MAC samego Tab5.

Tab5 rozpoznaje USB podczas wykrywania urządzeń przy starcie. Po odłączeniu USB nie uznaje kolejnego urządzenia CDC za wcześniej rozpoznanego Monstera; ponowne wykrycie może wymagać restartu Tab5.

## Co pokazuje status

| Pole | Znaczenie |
| --- | --- |
| Polityka zapisana | Wybór zapisany w JanOS na następne uruchomienie. |
| Polityka tego uruchomienia | Wybór, według którego JanOS uruchomił się obecnie. |
| Efektywny MAC tego uruchomienia | Adres wybrany na początku bieżącego uruchomienia JanOS. |
| Aktualny MAC STA | Adres zgłaszany teraz przez sterownik Wi-Fi albo `null`, gdy sterownik nie działa. |
| Stan rekordu NVS | Informacja, czy rekord istnieje i czy jest poprawny. |
| Wymagany restart | Czy zapisany wybór czeka na ponowne uruchomienie JanOS. |
| Gotowość tożsamości STA | Czy JanOS może bezpiecznie uruchomić Wi-Fi z wybranym adresem. |

Podczas WPS, Pixie lub JWPS aktualny MAC STA może być tymczasowym adresem sesji. Nie oznacza to zmiany polityki ani efektywnego adresu tego uruchomienia. Po sesji JanOS przywraca efektywny adres startowy. Komenda `sta_mac status` jest w JanOS bezpiecznym odczytem również podczas pracy WPS lub analizatora. Jeśli jednak inne zadanie **Tab5** zajmuje konsolę wybranego portu (np. Wi-Fi Analyzer w Tab5), ekran pokaże lokalny stan zajętości. Ponów odczyt po zakończeniu tego zadania.

## Zmiana polityki

1. Otwórz **Settings → Wi-Fi MAC**. Przy jednym rozpoznanym Monsterze status wczyta się sam; przy kilku wybierz właściwy port **Grove**, **USB** lub **MBus**.
2. Sprawdź odczytany status, aby potwierdzić, które urządzenie zmieniasz.
3. Wybierz **Factory**, **Random** albo **Fixed**. Dla Fixed wpisz adres w formacie `XX:XX:XX:XX:XX:XX`, np. `02:11:22:33:44:55`.
4. Zapisz wybór. Sprawdź wynik zapisu i ponownie odczytaj status.
5. Uruchom ponownie **JanOS**, gdy chcesz zastosować zapisany wybór. Tab5 nie wykonuje restartu automatycznie.

**Factory** korzysta z fabrycznego MAC STA. **Random** losuje lokalnie administrowany, unicastowy adres raz na każde uruchomienie JanOS; zatrzymanie i ponowna inicjalizacja sterownika Wi-Fi nie losują go ponownie. **Fixed** używa podanego adresu przy każdym uruchomieniu. Dla Fixed pierwszy oktet musi mieć ustawiony bit adresu lokalnego i wyzerowany bit multicast, np. `02`; błędny format lub adres nie jest zapisywany. Zapis w NVS pozostaje po aktualizacji OTA.

JanOS może odmówić zapisu, jeśli współdzielone radio ma aktywnego właściciela albo jest w kwarantannie. Sprawdź komunikat błędu i spróbuj ponownie po zakończeniu operacji. Zapis nie zmienia MAC działającego radia. Polityka dotyczy wyłącznie Wi-Fi STA, nie adresów SoftAP, BLE ani 802.15.4.

Brak rekordu NVS oznacza **Factory**. Rekord uszkodzony lub w nieobsługiwanej wersji jest zgłaszany w statusie, a JanOS używa dla tego uruchomienia **Factory** bez nadpisania rekordu. Błąd odczytu NVS jest osobnym stanem, a nie brakiem rekordu.

## Odpowiadające komendy JanOS

```text
sta_mac status
sta_mac set factory
sta_mac set random
sta_mac set fixed 02:11:22:33:44:55
```

Funkcja wymaga wersji firmware JanOS zawierającej komendę `sta_mac`.
