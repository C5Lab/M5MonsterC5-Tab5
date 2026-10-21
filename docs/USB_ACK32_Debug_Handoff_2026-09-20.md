# USB ACK32 — analiza awarii i zadania na później

Data: 2026-09-20

Status: przyczyna potwierdzona na podstawie pełnego logu sprzętowego. W tej
sesji nie zmieniono już transportu ani firmware; dokument jest handoffem do
następnej sesji.

## Wynik analizy

JanOS poprawnie odbiera pierwszy wznowiony blok i wysyła prawidłowy ACK32,
ale host USB w Tab5 nie przekazuje tego ACK do ring-buffera od razu.

CH34X ma endpoint BULK IN o `wMaxPacketSize=32`, ACK32 ma dokładnie 32 bajty,
a sterownik `iot_usbh_cdc` w Tab5 składa transfer IN o długości
`CONFIG_IN_TRANSFER_BUFFER_SIZE=512`. Pełny pakiet USB długości 32 bajtów nie
jest pakietem krótkim i nie wypełnia żądanych 512 bajtów, więc transfer hosta
pozostaje otwarty. ACK trafia do aplikacji dopiero wtedy, gdy JanOS po swoim
timeoutcie dopisuje tekstowy `SYNC_ERROR`; końcowy krótki pakiet USB domyka
transfer.

To nie jest błąd CRC, resume, numeru bloku ani parsera ACK32. Jest to konflikt
rozmiaru transferu BULK IN z 32-bajtową ramką odpowiedzi.

## Dowody z logu

1. Deskryptor CH34X pokazuje `BULK IN wMaxPacketSize 32`.
2. Tab5 negocjuje poprawnie:
   `READY off=1024 bsize=1024 rx_ms=1273 ack_size=32`.
3. Pierwszy blok zostaje zapisany przez JanOS:
   `ACK32 status=0x06 blk=0 offset=2048`.
4. ACK pojawia się dopiero około 1406 ms po enqueue bloku, czyli już po
   `rx_ms=1273` po stronie JanOS.
5. JanOS kończy oczekiwanie na następny nagłówek:
   `DIAG phase=header reason=block_timeout header=0/16 offset=2048 block=1`.
   `header=0/16` dowodzi, że worker nie dostał następnego bloku przed timeoutem.
6. Tab5 następnie czyta pierwsze 32 bajty tekstowego `SYNC_ERROR` jako kolejną
   ramkę i raportuje `invalid: magic/version`.
7. Po tym odczycie sterownik pokazuje `rx_buf=53`. Tekst
   `[CRACK/1] SYNC_ERROR code=block_timeout received=2048 size=9101075`,
   `END` i prompt mają łącznie 85 bajtów; `85 - 32 = 53`. To dokładnie zgadza
   się z buforem i potwierdza, co zostało pomylone z drugą ramką ACK32.
8. Źródło `iot_usbh_cdc` ustawia `in_xfer->num_bytes` na
   `CONFIG_IN_TRANSFER_BUFFER_SIZE`; bieżący `sdkconfig` ma wartość 512.

## Plan naprawy

### Zalecany pierwszy fix — tylko Tab5

- [x] Ustawić `CONFIG_IN_TRANSFER_BUFFER_SIZE=32` w aktywnym `sdkconfig`.
- [x] Dodać tę samą wartość do `sdkconfig.defaults`, żeby czysty build nie
      wrócił do domyślnych 512 bajtów.
- [x] Dodać kontrolę kompilacyjną oraz jednoznaczny log startowy pokazujący
      rozmiar transferu IN oraz MPS endpointu CH34X.
- [x] Nie zmieniać ACK32, resume ani timeoutów JanOS w tym kroku. Jedna zmienna
      na raz pozwoli potwierdzić przyczynę.
- [x] Nie kompilować firmware po stronie agenta; użytkownik kompiluje i
      flashuje Tab5.

Dlaczego 32: żądanie hosta zostanie zakończone po jednym pełnym pakiecie
endpointu, bo osiągnie żądaną długość transferu. Ring-buffer aplikacji nadal
ma 4096 bajtów, więc ta zmiana nie redukuje jego pojemności; zwiększa jedynie
częstotliwość callbacków USB.

### Testy przed flashowaniem

- [x] Dodać możliwy do uruchomienia na hoście test/guard spójności konfiguracji
      (`sdkconfig` oraz `sdkconfig.defaults` muszą wskazywać 32).
- [x] Uruchomić `usb_vcp_config_test`.
- [x] Uruchomić `hs_crack_remote_core_test` z ASan/UBSan.
- [x] Uruchomić exact-read harness ACK32.
- [x] Wykonać scoped `git diff --check`.

### Test sprzętowy

- [ ] Flashować tylko Tab5; JanOS 1.7.5/protocol 3 pozostaje bez zmian.
- [ ] Uruchomić tę samą wordlistę. Resume powinno zacząć od offsetu 2048.
- [ ] Oczekiwać kolejnych ramek:
      `ACK32 blk=0 offset=3072`, `blk=1 offset=4096`, itd., bez przerwy około
      1,3 s i bez `SYNC_ERROR`.
- [ ] Potwierdzić `SYNCED`, a przy drugim uruchomieniu cache hit.
- [ ] Sprawdzić podstawowe komendy tekstowe USB: ping, SD, wersja i listowanie.
- [ ] Sprawdzić, że Grove/M-BUS nadal używają swojego dotychczasowego transportu.

### Plan awaryjny, jeśli globalne IN=32 ma skutki uboczne

Zamiast wracać do podnoszenia timeoutów JanOS, zmienić lokalnie
`iot_usbh_cdc`, aby dla CH34X żądanie BULK IN miało długość równą MPS endpointu
(32), przy zachowaniu ring-buffera 4096. To jest bardziej inwazyjne i może być
nadpisane przy aktualizacji managed component, dlatego nie jest pierwszym
wyborem.

## Czego nie robić na początku

- Nie zwiększać ponownie timeoutu ACK po stronie Tab5 — Tab5 już czeka 8 s;
  problemem jest wcześniejszy timeout JanOS i niedomknięty transfer USB.
- Nie zwiększać w ciemno `rx_ms` JanOS. To zamaskowałoby opóźnienie i
  spowolniło wykrywanie realnie utraconych bloków.
- Nie zmieniać rozmiaru ACK32 na kolejną losową wartość. Bez naprawy długości
  transferu hosta pełne pakiety mogą ponownie pozostać otwarte, a krótkie mogą
  być buforowane przez mostek.
- Nie resetować cache przed testem: offset 2048 jest użytecznym dowodem, że
  resume zachowuje zatwierdzony postęp.

## Oczekiwany rezultat

Po ustawieniu transferu BULK IN na 32 bajty callback hosta powinien otrzymać
ACK32 natychmiast po jego wysłaniu, zanim upłynie `rx_ms=1273`. JanOS zdąży
wtedy odebrać następny nagłówek i nie wygeneruje `block_timeout`.
