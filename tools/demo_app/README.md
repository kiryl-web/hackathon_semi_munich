# PQC live demo app (full screen)

Double-click **`START DEMO.cmd`**. A console window starts the server and Chrome opens full screen (kiosk).
Close the window with **Alt+F4**; close the console window to stop the server.
If Chrome was closed by accident, run **`OPEN UI ONLY.cmd`**.

Needs: board on USB J13 (J-Link + COM4) and the KitProg3 I2C bridge (only for SPDM).
Python packages (already installed for `py -3.13`): pyserial, pylink-square.

## Simple version (click-to-run)
Double-click **`START SIMPLE DEMO.cmd`** (or open http://127.0.0.1:8765/simple while the server runs).
One button per experiment; a click starts it immediately and clears the terminal at once. If another experiment
is still running it is cut short (a running live SPDM operation is aborted; a boot step first lets EPBL
finish its flash work, a few seconds at most). "Known state (v1.0)" = pre-flight without SPDM.
The SPDM replays take 20 s and show the real bytes of a recorded run; the live ones take ~7 min.

## Before the jury (about 8 min)
Press **P** (pre-flight). It programs EPBL + responder v1.0.0 (the SPDM identity is not touched) and runs one
live SPDM attestation. Do not reset the board meanwhile. When the screen says READY, press **Home**.

## During the deep dive (~3.5 min automatic, pause any time)
**Space** start / pause · **→ / ←** next / previous step · **R** redo the current step · **1-8** jump ·
**Home** back to the start · **A** abort a live SPDM run · **L** LED test · **F** toggle fullscreen

| # | Step | Live on the board | Shown |
|---|---|---|---|
| 0 | Overview | - | chain of trust, CNSA 2.0 mapping |
| 1 | Verified boot | reset | image header bytes, SHA-512 stored vs recomputed, key hash vs key in EPBL, EPBL log + check time |
| 2 | Rogue key | update signed with another ML-DSA-87 key | key hash mismatch -> ATTACK BLOCKED |
| 3 | One byte | genuine update, 1 code byte changed | genuine vs tampered bytes, digest mismatch -> BLOCKED |
| 4 | Valid update v1.1 | genuine update | INSTALLED, version read back from flash |
| 5 | Rollback | genuine v1.0 over v1.1 | version check -> BLOCKED |
| 6 | SPDM attestation | replay (~20 s) of the pre-flight run, real bytes | decoded SPDM messages, ML-DSA-87 OID, measurements vs release file: MATCH x3 |
| 7 | Encrypted session | **live** SPDM exchange against v1.1 (~7 min) | ML-KEM-1024 KEY_EXCHANGE, ciphertext on the wire, device reply |
| 8 | Evidence & cost | - | recorded Task 3/4 results, measured cost, what is still classical |

Step 7 continues in the background; a pop-up shows the result during Q&A (press 7 to view it).
While it runs, board steps are refused (a reset would kill the session) - press A first if you must.
Redo is safe for every step: before "valid" the app restores v1.0 in the primary slot if needed, and before
"rollback" it installs v1.1 if needed.

## Board LEDs
- **LED3**: the responder firmware's heartbeat. It blinks while idle and pauses while SPDM traffic is handled.
- **LED4**: driven by this app over SWD (GPIO P8.5 register writes, core keeps running).
  Steady = verified image running, fast blink = attack rejected, off = reset / no boot,
  one toggle per SPDM transfer during live SPDM.
The two LED icons on screen mirror the real GPIO output register (read over SWD), not a simulation.

## Where the data comes from
- Input panels: parsed from the signed `.hex` files in `results/task4_boot` (MCUboot header and TLVs,
  SHA-512 recomputed, KEYHASH = SHA-512 of the raw 2,592-byte ML-DSA-87 public key; that key is located inside
  `epbl_full.hex`).
- Verdicts: from the live EPBL console on COM4. Check times come from the EPBL log timestamps.
- SPDM: every 127-byte DFU transfer of `spdm-requester.exe` (work dir `results/task5_demo/spdm_*`) is decoded live.
  Measurements are checked against SHA-384 values recomputed from the release files.
