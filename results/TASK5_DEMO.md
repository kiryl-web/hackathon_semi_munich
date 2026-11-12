# Task 5 – live demo cheat sheet

**NEW (12 Nov): full-screen demo app** – `tools\demo_app\START DEMO.cmd`. Press **P** about 8 min before the
jury slot (pre-flight), then **Home**, then **Space** at the start of the deep dive. Keys and steps:
`tools/demo_app/README.md`. The `.cmd` buttons below remain the backup.

## Backup: button-based demo (5 minutes)

Slides: `results/Task5_Presentation.pptx` (PDF copy `Task5_Presentation.pdf`). Speaker notes under every slide =
what to say. Demo buttons: folder `C:\Users\kpato\KP\Hackathon\tools\demo\` -> double-click the `.cmd` files.

## The one constraint to remember
One SPDM operation takes **~6-10 minutes** over this 127-byte I2C tool (host-bound, the device needs < 1 s).
A board reset kills a running SPDM operation -> **never press a boot/update button while an SPDM window is still running.**

## Before your slot (10-15 min before)
1. Board connected: J-Link USB (J13) + KitProg3 I2C bridge (wires SDA J16.6, SCL J16.5, VTARG J15.7, GND J15.8).
2. Double-click **`0 PREPARE (before talk).cmd`** (~20 s) -> green "image verified with ML-DSA-87, responder started".
3. Double-click **`3a SPDM measure v1.0 (start before talk).cmd`** and leave that window open. It finishes during the
   talk with `measure: OK ... signature verified` and three green `MATCH` lines (the device attests exactly the
   EPBL-verified v1.0.0 image).

## Live demo (after the 3a window shows PASS)
| # | Button | Say | Expect (seconds) |
|---|---|---|---|
| 1 | `1 BOOT.cmd` | "Reset: EPBL verifies the responder's ML-DSA-87 signature before it runs." | green `validated successfully`, responder banner (7 s) |
| 2 | `2a TAMPER rogue key.cmd` | "An attacker's update, correctly formatted, signed with a different ML-DSA-87 key." | red `secondary slot is not valid`, old image keeps running (16 s) |
| 3 | `2b VALID update v1.1.cmd` | "A genuine signed update." | green `Image upgrade ... Copying`, INSTALLED (20 s) |
| 4 | `2c ROLLBACK v1.0.cmd` | "A genuine but older image - the classic downgrade attack." | red `insufficient version`, REJECTED (13 s) |
| 5 | `3a` window (already finished) | "Before the talk: SPDM 1.4 authentication with ML-DSA-87; the signed measurements match the release file." | green MATCH x3 |
| 6 | `4 RECORDED evidence (backup).cmd` | "Reconnect after update and the SPDM tamper campaign from last night's run." | T0-T8 table, MATCH after v1.1, 12/12 rejected (instant) |
| 7 | optional `3b SPDM exchange after update.cmd` | "Started now: ML-KEM-1024 session + encrypted message against v1.1 - it completes during Q&A." | `exchange: OK` after ~7 min |

Required by the brief and covered: authorized boot (1), tampering + rejection (2, 4), valid update (3), SPDM
authentication + protected session + encrypted traffic (5/6/7), reconnect after update (6 recorded, 7 live).

## If something goes wrong
- A button prints "No I2C bridge" -> replug the KitProg3 USB, wait 5 s, retry.
- Board silent / NO BOOT -> run `0 PREPARE` again (20 s); the SPDM identity is never touched by these scripts.
- SPDM fails or is too slow -> use `4 RECORDED evidence` (all evidence is from the real hardware runs).
- Never use "Erase device" or the provisioning `--freeze` (shared board).

## Dry run 2026-11-11 23:xx
prepare 19 s, boot 7 s, rogue 16 s (rejected 0x2e), valid 20 s (installed), rollback 13 s (rejected),
SPDM measure v1.1: see `results/task5_demo_dryrun_spdm.log`.
