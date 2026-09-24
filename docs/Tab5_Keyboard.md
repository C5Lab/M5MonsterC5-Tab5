# Tab5 physical keyboard

The A164 [M5Stack Tab5 Keyboard](https://docs.m5stack.com/en/tab5/Tab5_Keyboard)
is detected automatically on Ext.Port1: SDA GPIO0, SCL GPIO1, default I2C
address `0x6D`. No Setup toggle is needed.

- After successful identification and Character-mode initialization, physical
  input replaces the on-screen keyboard in the application's text fields.
- Detection runs at startup and every 500 ms while disconnected. After three
  consecutive read failures, the on-screen keyboard becomes available again.
  A keyboard that was dismissed stays dismissed until its field is selected.
- Tap a field to select it. Letters, digits, symbols and spaces come from the
  keyboard firmware, including its Aa/Sym behavior. Backspace, Delete, arrow
  keys and Tab are supported. Tab follows the navigation rules below.
- Enter acts as OK for single-line fields and inserts a newline in multiline
  fields. Esc follows the dismissal rules below, including while editing.
  Existing Save and Cancel touch buttons remain available. Ctrl/Alt shortcuts
  are not assigned.
- All catalogued text fields (including passwords and Sub-GHz rename) and
  Scan Setup's six numeric fields use a 2 px caret matching their text color.
  It blinks every 400 ms while focused and disappears on defocus. Normal LVGL
  touch focus drives it with either keyboard. Numeric fields retain their
  existing plus/minus controls; cursor styling does not add keyboard bindings.
- The first key wakes a sleeping display without entering text. A locked
  screen still requires the normal unlock action.

## Navigation

When no text field is being edited:

- **Arrow keys:** select tiles by their positions on screen. The first arrow
  selects the first available tile; further arrows move the magenta focus
  frame (3 px wide, inset 4 px inside the tile to avoid clipping). Moving
  beyond a scrolling tile grid's viewport reveals the next
  tile. On pages/dialogs without tiles, arrows select visible buttons.
- **Scan results Up/Down:** the first arrow highlights the first visible
  result. Each following arrow moves the outline by one available row in the
  current filtered/sorted order and scrolls it into view, stopping at either
  end. Hidden and disabled rows are skipped. This is a read-only highlight:
  Enter/Space do not click a row or change its checkbox. It requires a detected
  physical keyboard. An active text field keeps the arrows for editing, and
  a popup covering the results blocks browsing of the list underneath.
- **Tab:** switch to the next available tab in the tab bar's displayed order
  (GROVE, USB, MBUS, INTERNAL, omitting unavailable tabs), wrapping at the end,
  including while a text field is active if the tab bar remains accessible.
- **1–9, 0:** activate the first through tenth visible tiles, left to right,
  top to bottom. Numbering follows the current viewport and layout. Hidden
  tiles do not count; disabled tiles keep their position but cannot activate.
- **Enter / Space:** outside read-only result browsing, activate the selected
  tile/button. With no selection, select and activate the first available
  tile/button. Rows of a read-only results list are excluded from activation.
- **Esc:** activate the frontmost registered Close/Cancel/Back control. A dialog
  closes before its parent page; only one action runs per keypress. Existing
  exit confirmations still apply. A hidden, disabled or covered exit consumes
  Esc without acting on controls behind it. Entire hidden pages/dialogs are
  ignored. An editor in front of the exit controls, without its own registered
  exit, retains the on-screen keyboard's Cancel action. With neither an exit
  nor an active editor, Esc only clears the selection.
  Like all physical shortcuts, this works only while the keyboard is detected.

While editing, digits and Space remain text and arrows move the cursor. In a
popup that covers the tab bar, Tab moves between fields. Enter retains the
text-field behavior above; Esc gives dismissal priority over text editing.
Covered controls cannot receive keyboard activation; dialog buttons can be
selected and confirmed without activating the tiles behind them. Selection is
cleared when its widget is deleted, a tab changes, or the keyboard disconnects.
Screen code explicitly registers existing dismissal controls with
`app_keyboard_navigation_register_escape()`. New dialogs should register their
Close/Cancel button too; the router never guesses an action from a label/icon
and does not substitute a Stop, Save or confirmation action for a missing exit.

The driver uses I2C controller 1 at 100 kHz and polls every 20 ms in a worker
task, so bus timeouts do not block LVGL. GPIO50 (INT) is not required. System
I2C remains on controller 0 / GPIO31–32. The BSP's unused Grove I2C helper also
claims controller 1: it cannot be enabled simultaneously on GPIO53–54 without
redesigning bus ownership. Failure to initialize the keyboard leaves touch
input available. Devices with a changed I2C address are not auto-discovered.

Protocol reference:
[M5Stack driver](https://github.com/m5stack/M5Tab5-Keyboard-UserDemo/tree/main/components/m5_tab5_keyboard_component).
Detection checks the address and version registers and verifies Character mode
before hiding the touch keyboard; an address ACK alone is insufficient.
Character-event register `0x40` reports the **entire packet size**, including
the modifier byte: a letter takes 2 bytes and `backspace` takes 10 bytes.
The driver follows the
[STM32 firmware implementation](https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW/blob/main/code/Keyboard_APP/Core/User/i2c/user_i2c_callback.c)
and reads that exact number of bytes from `0x50`.

## Verification

Run on Linux or WSL with GCC and Python 3:

```sh
python3 tests/run_keyboard_tests.py
```

The tests exercise production protocol code and the bundled LVGL with address
and undefined-behavior sanitizers: hotplug, initialization failure, malformed
events, key mapping, editing constraints, Tab, modal input blocking, widget
deletion, preservation of virtual-keyboard visibility, spatial navigation,
numeric shortcuts, disabled tiles, tab cycling, scrolling and text priority.
Esc tests cover nested dialogs, one-level dismissal, editing priority, blocked
controls, the root screen, disconnection and deletion during callbacks.
Read-only list browsing tests cover row steps, sort/filter order, both bounds,
no row activation/checking (including Enter/Space after a refresh), modal
blocking, editing priority, disconnection and rebuilding/emptying results.
The filter-field regression feeds literal I2C event packets through both the
production decoder and LVGL into a tapped, nested SSID input field while the
virtual keyboard stays hidden.

On a Tab5, verify insertion/removal both before boot and while editing a field,
Aa/Sym, fast typing, wake/lock, single/multiline Enter, arrow navigation,
Tab cycling and numeric shortcuts in both orientations. Automated tests do
not replace this physical hardware check.
