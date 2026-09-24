# M5MonsterC5-Tab5

![IMG_8095](https://github.com/user-attachments/assets/40db0663-ff23-4420-b6fd-5f4a6f0641b1)
---

## Info / Flash 

The project is based on the ESP32C5-Wroom-1U and consists of two software components:

- JanOS on the MonsterC5 board - flasher: https://c5lab.github.io/projectZero/janos_flash.html
- App for the M5Stack Tab5 - flasher: https://c5lab.github.io/M5MonsterC5-Tab5/janos_flash.html

M5MonsterC5-Tab5 is an add-on board built around the M5MonsterC5 platform and based on JanOS from the Project Zero repository.
It is intended for M5Stack Tab5 devices, and it can also be used with Android and PC/MacOS over a USB connection.

Project Zero (JanOS): https://github.com/C5Lab/projectZero

---

## Documentation

- [Tab5 physical keyboard — automatic detection and text input](docs/Tab5_Keyboard.md)

An English, section-by-section introduction to the offline packet-analysis
workflow is available here:

- [ESPShark — Offline packet investigation for M5Stack Tab5](docs/ESPShark.md)
- [On-device handshake dictionary check — SD setup and usage](docs/Handshake_Dictionary_Check.md)

Full documentation and usage details are available on the wiki:
https://github.com/C5Lab/M5MonsterC5-Tab5/wiki

## Local browser emulator

Run these commands from the repository root in **Windows Command Prompt**.
With Docker Desktop running, build the emulator after changing its sources:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
```

Start the local preview and leave this terminal open:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Open **http://127.0.0.1:8765/**. Stop the server with **Ctrl+C**. If the virtual
environment does not exist, `py -m http.server` can serve the already built folder
with the same arguments. Do not put `py` before the virtual environment's
`python.exe` path. If the browser refuses the connection, check that the server
terminal is still running; if port 8765 is occupied, use 8766 in the command and URL.

The emulator uses synthetic data. For setup, tests and a scan-to-PCAP walkthrough,
see [the emulator README](tools/ui_emulator/README.md). Current scope and remaining
work are tracked in [Live Emulator TODO](docs/Live_Emulator_TODO.md).

## Availability

[The board available on Tindie.](https://www.tindie.com/products/lab/m5monsterc5-esp32c5-marauder/)

## Community

Discord: https://discord.gg/57wmJzzR8C
