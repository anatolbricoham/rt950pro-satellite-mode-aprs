# APRS messaging — RT-950 Pro (custom firmware)

*[Versión en español](README.md)*

Two-way APRS text messages with acknowledgements, like those of a Kenwood
TH-D74 or a Yaesu FTM-400: the radio receives messages addressed to your
call sign, acknowledges them by itself, keeps an inbox and lets you type and
send messages from the keypad.

![Messaging screens](img/screens.png)

*Inbox, received message (bulletin), composing a reply and the inbox after
sending it. Host simulator captures with the real firmware code.*

## Does it work with the stock firmware?

No. Radtel's firmware only sends the position beacon with a **fixed
message** (*Custom Messages*, 40 characters), configured with the CPS or with
RT-950 Toolkit. It has no inbox, no compose screen and no acknowledgements.
Messaging only exists in this repository's custom firmware.

## What it does

- **Receive**: the BK4829 AFSK demodulator decodes every frame. If it is a
  message for your call sign and SSID (or a `BLNx` bulletin), it is stored in
  the inbox and `MSG` appears in the status bar.
- **Automatic ack**: every numbered message (`{id`) is answered with
  `ack<id>` after a random 1.5 to 3 s delay so as not to collide with the
  digipeater. Repeated copies (digipeated or resent by the sender) are acked
  again but stored only once.
- **Send**: every message gets a number. If no `ack` arrives it is repeated
  after 30, 60 and 120 s (3 retries by default, 0 to 5). If 60 s pass after
  the last copy without an ack it is marked **X** (not acknowledged). With an
  ack it is marked **OK**.
- **Format**: APRS 1.0.1, chapter 14:
  `:EA4BBB   :text{12` (message), `:EA4BBB   :ack12` (ack),
  `:EA4BBB   :rej12` (reject), `:BLN1     :text` (bulletin). AX.25
  destination `APZ950` (experimental `APZxxx` range) and the path configured
  in the radio (WIDE1-1, WIDE1-1,WIDE2-1 or custom).
- Up to 67 characters per message and 24 messages in the inbox (in RAM: lost
  at power-off).

## How to use it

### Opening it

- **Menu → APRS Set → Messages** (the value shows the unread count), or
- a PF key programmed with action 9.

### Inbox

Newest first. `<` = received, `>` = sent. On the right: elapsed time and
status (`*` unread, `..` waiting for ack, `OK` acknowledged, `X` not
acknowledged).

| Key | Action |
|---|---|
| Encoder | Move |
| `MENU` | Open the message |
| `*` | New message |
| `#` | Exit |

### Message

`MENU` replies to the sender; `#` goes back.

### Compose

1. **To**: addressee call sign with multi-tap (2 = ABC2, 3 = DEF3, …,
   0 = space and 0, 1 = `1.,-?!/@#*`). `*` deletes. `MENU` moves to the text.
2. **Text**: multi-tap text; `B` cycles through quick texts (`QSL? 73`,
   `QRV`, `QRT, 73`, …). `MENU` sends. `#` goes back.

### Settings (APRS Set menu)

| Option | Values | Default |
|---|---|---|
| Messages | enter | — |
| Msg RX | Off / On (keep the demodulator running) | On |
| Auto ACK | Off / On | On |
| Msg Retries | 0–5 | 3 |

They are stored in the SPI flash at 0x0C9000, with CRC-32.

Call sign, SSID and path come from the radio's APRS configuration (the same
values RT-950 Toolkit edits in its APRS tab).

## Requirements and limits

- Set your call sign and SSID first; nothing is sent without a call sign.
- The channel must be your region's APRS frequency (144.800 MHz in Europe)
  and the radio must be listening to it to receive.
- Transmission uses the same AFSK chain as the custom firmware's beacon.
  **It has not been tested on a physical radio yet**: encoding, engine and
  user interface are verified on the PC (below), but the BK4829 modulation is
  not.

## Verification

`tests/test_aprs_msg.py` (part of `tests/run_tests.sh`) builds the real
codec, engine and user interface into the host simulator and checks:

- correct outgoing frames, decoded by an independent AX.25 decoder and by
  `aprslib`;
- ack received → **OK**; unanswered message → 1 + 3 retries, then **X**;
- automatic ack (also of the duplicate copy) and a single inbox entry;
- traffic for other stations ignored; bulletins stored; text containing `{`
  rejected;
- reply typed with multi-tap and sent; screens saved as PNG.

The design decisions are in
[DESIGN_DECISIONS.en.md](../satellite/DESIGN_DECISIONS.en.md) (D-39 to D-42).
