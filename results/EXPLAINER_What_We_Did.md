# What we actually built: a plain-language guide to the PQC hackathon project

*For: someone with an electrical-engineering and Android background who is new to cryptography.
Covers Tasks 1–4. Every number below comes from our own hardware runs on 2026-11-11.*

---

## 1. The problem in one paragraph

Our board, like most connected hardware today, proves who it is and protects its traffic with
**elliptic-curve cryptography (ECC)**. ECC is safe against every computer that exists now. A large enough
**quantum computer** could break it, though. Nobody has built one yet, but two things make this urgent anyway.
First, **"harvest now, decrypt later"**: an attacker can record encrypted traffic today and decrypt it in ten years.
Second, hardware like datacenter cards stays in service for 10–15 years, so a device shipped today with ECC-only
identity will still be running when that risk becomes real. The US government's **CNSA 2.0** rules
already tell national-security suppliers to move away from ECC. The hackathon asked us to take a real
ECC-based device and migrate it to **post-quantum cryptography (PQC)**, meaning algorithms believed to resist
quantum computers. That covers **(a)** how it boots and **(b)** how it talks to its host, and we had to measure what the
change costs.

---

## 2. Crypto crash course (only what you need)

### 2.1 Hash functions: fingerprints
A **hash** (here SHA-384 or SHA-512) turns any amount of data into a fixed-size fingerprint (48 or 64 bytes).
Change one bit of the input and the fingerprint changes completely. You can't work backwards from the
fingerprint to the data, and you can't find two inputs with the same fingerprint. The number in the name is the
output size in bits.

*Android analogy:* the SHA-256 checksum you compare after downloading a ROM.

### 2.2 Symmetric encryption: one shared key
**AES-256-GCM** encrypts data with a 256-bit secret key that both sides know. The "GCM" part also produces a
16-byte **authentication tag**. If anyone changes even one bit of the encrypted message, the tag check fails and the
message is thrown away. This is called **AEAD** (Authenticated Encryption with Associated Data):
secrecy and tamper-detection in one step. AES is fast and is *not* seriously threatened by quantum
computers. The best known quantum attack (Grover's algorithm) roughly halves the effective key length, so
AES-256 still gives about 128 bits of security, which is plenty.

The catch with symmetric crypto is that both sides need the same secret key, and you can't simply send it over the wire.
That's what the next two tools solve.

### 2.3 Public-key cryptography: a key pair
Each party has a **private key** (kept secret) and a **public key** (given to everyone). It is used for two jobs:

1. **Digital signatures (authentication).** The owner signs a message with the private key. Anyone holding the
   public key can check that the signature is real and that the message wasn't changed. Nobody without the private key
   can make a valid signature.
   *Android analogy:* APK signing. Your phone only installs an update if it is signed by the same key as the
   installed app. **Android Verified Boot** is exactly the "secure boot" idea in Section 4 below.

2. **Key establishment.** Two parties who have never met agree on a fresh shared secret over a public
   channel, and then use it as the AES key. The classical way is **ECDHE** (Elliptic-Curve Diffie-Hellman,
   Ephemeral). "Ephemeral" means a new key pair for every session, so stealing one session's key later
   doesn't reveal the others. This property is called **forward secrecy**.

### 2.4 Certificates and the chain of trust
A public key alone doesn't tell you *whose* key it is. A **certificate** (X.509 format, the same as HTTPS)
is a small document that says "this public key belongs to device *PSC3 DEV 01*", and it is signed by a **certificate
authority (CA)**. The verifier keeps a short list of CA public keys it trusts unconditionally. These are called
**trust anchors** or **root certificates**. Checking certificate → CA → root is called **validating the chain**.
Your browser does this for every HTTPS site.

### 2.5 Why quantum computers break ECC but not AES
ECC (and RSA) are safe because certain math problems are infeasible for normal computers. For ECC the problem is the
"elliptic-curve discrete logarithm". **Shor's algorithm** on a large quantum computer solves these problems
efficiently. Once that happens, an attacker can compute your private key from your public key, forge signatures, and
decrypt recorded ECDHE sessions. Symmetric ciphers and hashes are only weakened, not broken. So the fix is: **replace
the public-key parts and keep AES-256 and SHA-384/512.**

### 2.6 The replacements: ML-DSA and ML-KEM
In 2024 NIST standardised new algorithms based on **lattice** math. Very roughly, the hard problem is
solving a huge system of linear equations where every answer has been blurred by small random errors. No known
quantum algorithm solves this efficiently.

| Job | Classical (what we had) | Post-quantum (what we moved to) | Standard |
|---|---|---|---|
| Signatures | ECDSA P-384 | **ML-DSA-87** ("Module-Lattice Digital Signature Algorithm") | FIPS 204 |
| Key establishment | ECDHE P-384 | **ML-KEM-1024** ("Module-Lattice Key Encapsulation Mechanism") | FIPS 203 |

**The numbers in the names are parameter sets, which set the strength level.** ML-DSA comes in 44/65/87 and ML-KEM in
512/768/1024. NIST defines "security categories" 1–5. The biggest sets (ML-DSA-87, ML-KEM-1024) are
**category 5**, comparable to breaking AES-256. **CNSA 2.0 allows only category 5** for national-security
systems. The brief warned us not to assume ML-DSA-65 was enough. It isn't for CNSA 2.0, and we proved our device
*rejects* ML-DSA-65 material (more on this below). CNSA 2.0 also requires SHA-384/512 and AES-256, which is why we
use SHA-512 at boot and SHA-384 + AES-256-GCM in sessions.

**A KEM works a little differently from Diffie-Hellman.** In ECDHE both sides send a public value and each computes the
same secret. With a KEM:
1. The requester makes a temporary key pair and sends the **encapsulation key** (1,568 bytes).
2. The device **encapsulates**: it picks a random 32-byte secret, locks it with that key, and sends back the
   **ciphertext** (1,568 bytes).
3. The requester **decapsulates** with its private half and gets the same 32-byte secret.
If someone alters the ciphertext, decapsulation doesn't report an error. It quietly produces a *different*
secret ("implicit rejection"). The mismatch is then caught one step later, when both sides prove they hold the same key.

**The price is size.** This is the main engineering cost of the whole project:

| Object | ECC P-384 | PQC | Factor |
|---|---:|---:|---:|
| Public key | ~97 B | 2,592 B (ML-DSA-87) | ~27× |
| Signature | 96–103 B | 4,627 B (ML-DSA-87) | ~45× |
| Key-exchange public value | 97 B | 1,568 B (ML-KEM-1024) | ~16× |
| Our certificate chain | 1,218 B | 15,187 B | 12.5× |

**Hybrid** means using ECC and PQC together. The brief explicitly excluded hybrid modes, so we did a clean switch.

---

## 3. The hardware and software

### 3.1 The board
- **KIT_PSC3M6_EVAL** with an Infineon **PSoC Control C3M6** microcontroller: an Arm **Cortex-M33**,
  **512 KB flash, 128 KB SRAM**. That's tiny next to a phone, and a single ML-DSA-87 certificate chain fills 12%
  of the whole RAM.
- **Arm TrustZone** splits the chip into a **secure world (SPE, Secure Processing Environment)** and a
  **non-secure world (NSPE)**. The hardware stops non-secure code from reading secure memory. The non-secure
  side calls secure functions only through fixed doorways called **NSC veneers** (Non-Secure Callable).
  *Android analogy:* the TEE/Keystore. Your app can ask Keystore to sign something, but can never read the key.
  In our design, **the device's private key lives only in the secure world** and every signature and
  ML-KEM operation runs there. The SPDM protocol code (the open-source **libspdm** library) runs in the
  non-secure world.
- **Lifecycle state DEVELOPMENT.** The chip has one-way lifecycle states (development → production…). In
  DEVELOPMENT the debug port is open and nothing irreversible has been done. The board was shared, so we
  deliberately never ran anything irreversible (no "erase device", no `--freeze`, no eFuse blowing).

### 3.2 How the PC talks to the board
The host PC plays the **SPDM requester**. It reaches the board over **I2C** through a KitProg3 adapter. Each
transfer carries at most **127 bytes**, using the DFU (Device Firmware Update) protocol as the carrier and MCTP
as the framing. Big messages therefore get **chunked**. SPDM has CHUNK_SEND / CHUNK_GET messages for this.
The host tool starts a new process for every transfer (about 1.4 s each). That is why one PQC session took about
6 minutes of wall-clock time, even though the *device* spends only a few hundred milliseconds computing. This is
a property of the test tool, not of PQC.

### 3.3 Toolchain
Infineon **ModusToolbox** (GCC 14.2, Makefiles, VS Code extension), **Edge Protect Tools** for signing
images and keys, J-Link for flashing, a UART log on COM4. We added `[PERF]` timing lines to the firmware. They
use the SysTick timer, because the CPU's cycle counter reads 0 from the non-secure world on this chip.

---

## 4. Plane 1: secure boot

### 4.1 The boot chain
Secure boot means: **at every power-on, each stage checks the signature of the next stage before running it.**
If anything was modified, it refuses to boot. Our chain:

```
Reset
 └─ ROM_BOOT     (burned into silicon, immutable, Infineon's own keys)
     └─ FLASH_BOOT  (Infineon code in protected flash; applies lifecycle & boot policy)
         └─ EPBL     (Edge Protect Bootloader, our first own code, at 0x32000000)
             └─ Application (the SPDM responder), checked by EPBL with ML-DSA-87
```

- **ROM_BOOT** is in mask ROM, so it can never change. It only knows classical ECDSA. **It cannot verify ML-DSA**, and the
  brief explicitly forbids claiming otherwise.
- **FLASH_BOOT** *can* check EPBL's signature with an OEM ECDSA key, but only after "ownership transfer"
  and an OEM boot policy called `SECURE_APP` are provisioned. That needs an Infineon-signed OEM certificate,
  which we didn't have, and includes steps we couldn't risk on a shared board. On our board the policy is
  `SIMPLE_APP`: **FLASH_BOOT launches EPBL without checking it.** We report this openly as the main remaining
  gap (Section 9).
- **EPBL** is based on **MCUboot**, the standard open-source bootloader. It is configurable and **supports
  ML-DSA**. This is where the PQC check happens.

### 4.2 Image format (MCUboot)
A signed image is laid out like this:

```
[ header 0x400 B: magic, version 1.0.0+0, sizes ] [ firmware ] [ TLV area ]
```
**TLV** = Type-Length-Value, a list of tagged records after the firmware. Ours contains the SHA-512 hash of
header+firmware, a hash of the public key that signed it (so EPBL can tell "wrong key" from "bad signature"),
and the ML-DSA-87 signature (4,627 B). The version in the header is part of what gets hashed and signed, so it
can't be edited without breaking the signature.

### 4.3 Update slots and rollback
Flash is split into a **primary slot** (the image that runs) and an **upgrade slot** (where a new image is
dropped). At boot, if the upgrade slot holds something, EPBL **verifies it, checks its version is not lower
than the running one, copies it over the primary, and verifies again.** If it fails, it erases it.
**Rollback protection** is that version check. Attackers like rolling back to an old version that had a
known bug, so the bootloader refuses older versions.

Our final flash map (Task 4):

| Area | Size | Content |
|---|---|---|
| EPBL | 108 KB | bootloader + embedded ML-DSA-87 public key |
| Primary slot | 180 KB | running SPDM responder |
| PAS | 24 KB | device identity (private key + certificate chain) |
| Upgrade slot | 180 KB | candidate new firmware |

---

## 5. Plane 2: SPDM, runtime protection

**SPDM (Security Protocol and Data Model)** is a DMTF standard used in datacenters. Think of a server's
management controller (BMC) checking a GPU or SSD before trusting it. The checker is the **requester** and the
device is the **responder**. A normal conversation looks like this:

| Step | Messages | What it achieves |
|---|---|---|
| 1 | GET_VERSION, GET_CAPABILITIES, NEGOTIATE_ALGORITHMS | agree on version 1.4 and algorithms |
| 2 | GET_DIGESTS, GET_CERTIFICATE | fetch the device's certificate chain; requester validates it against its trust anchor |
| 3 | CHALLENGE | requester sends a random number; device signs it → "prove you own the private key, right now" |
| 4 | GET_MEASUREMENTS | device returns signed fingerprints of its firmware ("what software are you running?") |
| 5 | KEY_EXCHANGE, FINISH | establish an encrypted session (ECDHE or ML-KEM) |
| 6 | application data | encrypted with AES-256-GCM |

Two terms you'll see often:
- **Transcript**: a running hash of *every* message exchanged so far. The device signs the transcript, so if
  a man-in-the-middle changed any earlier message (for example, removed the strong algorithms to force a weak
  one, a **downgrade attack**), the signature won't verify.
- **Key schedule**: the recipe that turns the 32-byte shared secret into the actual keys. SPDM uses **HKDF**
  (a hash-based key-derivation function) with SHA-384, mixed with the transcript hash. It derives handshake
  keys, then "finished" keys that both sides use to prove they computed the same thing, then the AES keys
  for application data.

---

## 6. Task 1: measuring the ECC baseline

**Goal:** before changing anything, document and measure the original ECC system so we have something to
compare against.

**Boot side.** We configured EPBL for **ECDSA P-384** and signed Infineon's demo app (CE243041). On hardware:
- a correctly signed v1.0.0 booted;
- a **corrupted image** (1 byte flipped), an image **signed with a rogue key**, a modified **header version**,
  a modified **protected TLV**, and a modified **signature byte** were all rejected;
- a **rollback** to v0.9.0 was rejected ("insufficient version in secondary slot"); we read the flash back
  before and after as evidence;
- a valid update to v2.0.0 was installed.

Numbers: ECDSA verification **≈170 ms**, reset→app **≈266 ms**, EPBL size 35,888 B.

**SPDM side.** We provisioned an ECDSA P-384 identity onto the device (key generated *on the device*,
certificate signed by our test CA) and ran the full requester suite: connect, get certificate, challenge,
measurements, session, encrypted exchange. All passed. A requester with the wrong trust anchor was rejected.

Baseline numbers: ECDSA sign 84 ms, ECDHE ≈163 ms, whole KEY_EXCHANGE 252 ms on the device. Certificate chain
1,218 B, largest message 1,226 B, one session = 24 transfers / 2,569 bytes on the wire. Responder flash 130 KB.

---

## 7. Task 2: post-quantum secure boot

**Goal:** make EPBL verify the application with a PQC signature.

**What we changed:**
- EPBL configuration → `ML-DSA-87`, image hash **SHA-512**. The documented default was SHA-256, but CNSA 2.0
  allows only SHA-384/512. We also found that the EPBL and the signing tool defaulted to *different* hashes
  (SHA-512 vs SHA-256), which produces images that never boot. We added an `IMAGE_SHA` option to the signing
  script so both sides are set explicitly.
- The ML-DSA-87 public key is **compiled into EPBL** as a C array. (This chip's special key storage only
  supports classical keys for this purpose.)
- We used ML-DSA in "**HashML-DSA**" mode: hash the image with SHA-512 first, then sign the 64-byte hash.
  That's practical for a 180 KB image.

**Tests on hardware: 4/4 positive passed and 11/11 negative cases were rejected.** The negative cases:

| Attack | Result |
|---|---|
| payload byte flipped | rejected (hash mismatch) |
| header version byte changed | rejected |
| protected TLV changed | rejected |
| key-hash TLV changed | rejected (no matching key) |
| byte inside the ML-DSA signature changed | rejected (signature check fails) |
| malformed TLV length | rejected |
| complete image signed with a **rogue ML-DSA-87 key** | rejected |
| image signed with **classical ECDSA** (downgrade attempt) | rejected; no fallback to ECC |
| ML-DSA-87 with **SHA-256** pre-hash (non-compliant) | rejected |
| image signed with **ML-DSA-65** (too weak for CNSA 2.0) | rejected |
| **rollback** to v0.9.0 via the update slot | rejected |

**A surprising result: PQC was *faster*.** ML-DSA-87 verification took **≈61 ms vs 170 ms** for ECDSA, and
reset→app went from 266 to **146 ms**. The reason is that on this chip P-384 runs purely in software (only P-256 has hardware
acceleration), while ML-DSA verification is mostly fast integer arithmetic (a "number-theoretic transform",
an FFT-like trick) plus SHAKE hashing.

**The costs:** EPBL flash +11 KB (47 KB total, 43% of its slot); peak boot RAM 10.7 → **25.2 KB**, because
the ML-DSA key object alone is about 12.5 KB; each image carries **4.8 KB of metadata instead of 215 B**.

**Limitation found (L1):** an old but genuinely signed image written *directly into the primary slot with a
debugger* still boots. The version check only guards the update path. The real fix is a hardware anti-rollback
counter (eFuses, irreversible) plus a closed debug port. Both are production steps we couldn't do on a shared board.

**Why ML-DSA-87 and not LMS?** CNSA 2.0 also allows LMS/XMSS (hash-based signatures) for firmware. We chose
ML-DSA-87 because it is *stateless*. LMS requires the signer to never reuse a one-time key, which is a painful
bookkeeping problem in a build server. It is also the same algorithm family the device uses for SPDM, so one PQC algorithm covers both planes.

---

## 8. Task 3: post-quantum SPDM

**Goal:** switch the runtime protocol to SPDM 1.4 with ML-DSA-87 for identity and ML-KEM-1024 for session keys.

### 8.1 What we did
1. Rebuilt the provisioning firmware for the ML-DSA-87 suite and **re-provisioned the identity**. The device
   generates its ML-DSA-87 key pair *inside the secure world*, so the private key never leaves the chip. It
   produces a **CSR** (Certificate Signing Request: "here's my public key, please certify it"). Our ML-DSA-87
   test CA signs it, and the resulting 15,187-byte chain is written into the **PAS (Provisioning Asset Store)**,
   a protected flash area owned by the secure world.
2. Rebuilt the responder with **profile P4**: SPDM 1.4, ML-DSA-87, ML-KEM-1024, SHA-384, AES-256-GCM.
   Crucially, **no classical algorithm is offered at all**, so an attacker can't negotiate the device down to ECC.
3. Added a **test-only fault-injection build** (`SPDM_FAULT_INJECT=1..6`, compiled out by default). It makes
   the device deliberately misbehave in six ways, so we could prove the host detects each one.

### 8.2 How the host validates the device
The requester holds the ML-DSA-87 root certificate as its trust anchor. It downloads the chain, checks every
ML-DSA-87 signature up to that root, then sends CHALLENGE. The device signs the transcript with its private key, and
the requester verifies it with the public key from the leaf certificate. Only then does it trust measurements or open a
session.

### 8.3 How the session key is made
The requester sends a fresh ML-KEM-1024 encapsulation key inside KEY_EXCHANGE (1,638 B). The secure world
encapsulates and gets a 32-byte secret plus a ciphertext. The device sends KEY_EXCHANGE_RSP (**6,305 B**:
ciphertext + ML-DSA-87 signature over the transcript + a proof value). Both sides run the HKDF-SHA-384 key
schedule, confirm with encrypted FINISH messages, and switch to AES-256-GCM. Each session has fresh KEM keys,
which gives forward secrecy.

### 8.4 Tests
- **Positive:** connect, slots, get-cert, challenge, measurements, session, encrypted exchanges all passed. One
  of the first three exchanges failed once (the device seemed to lose state mid-handshake). It **failed closed**,
  meaning no insecure fallback. It never reproduced in 9 later full handshakes. The root cause is unknown and is documented as such.
- **Negative: 12/12 rejected** in PQC mode, plus **6/6** of the same tampering against the ECC baseline:

| Attack | Detected at |
|---|---|
| requester trusts a classical ECDSA root / a rogue ML-DSA-87 root with the same name / an ML-DSA-65 root / a root with 1 flipped key byte / a truncated root | certificate validation |
| device certificate name altered | certificate validation |
| one byte of the ML-DSA signature flipped | CHALLENGE verification |
| valid signature over a **modified transcript** | session start |
| ML-KEM **ciphertext modified** | key confirmation (implicit rejection) |
| AEAD tag of FINISH / of app data altered (man-in-the-middle) | AES-GCM tag check |

### 8.5 What it cost

| | ECC (P2) | PQC (P4) |
|---|---:|---:|
| Signature on device | 84 ms (constant) | 66–346 ms, median 112 ms (*) |
| Key exchange computation | 163 ms (ECDHE) | **11 ms** (ML-KEM encaps) |
| KEY_EXCHANGE response | 302 B | 6,305 B |
| Largest message | 1,226 B | 7,168 B |
| Bytes on the wire per session | 2,569 B | 28,199 B (11×) |
| Transfers per session | 24 | 214 |
| Non-secure RAM buffers | 19,952 B | 57,816 B (+38 KB) |
| Peak secure heap | 2.6 KB | 27.7 KB |
| Responder flash | 130 KB | 128 KB (*smaller*) |

(*) ML-DSA signing uses **rejection sampling**: it makes a candidate signature, throws it away if it would leak
information about the key, and retries. The time therefore varies randomly.

**The lesson: PQC on this MCU costs RAM and bandwidth, not CPU time or flash.** The certificate slot in the PAS is
15 KB, and our chain is 15,187 B (99% full), so there's no room for an intermediate CA. That's a real design
constraint we wrote down.

---

## 9. Task 4: tying boot and runtime into one lifecycle

**Goal:** show that boot-time and runtime protection form one coherent, updatable device. In Task 3 the SPDM
responder ran *without* EPBL (it overwrote it), so you could argue the device answering SPDM wasn't
verified at boot. Task 4 fixed that.

### 9.1 What we did
- **Put the real SPDM responder behind EPBL.** This required a new board-support variant with a new flash map
  (the map in Section 4.3), regenerating the TrustZone memory-protection config, and a custom signing script
  (`tools/resp_epbl_sign.py`) that merges the secure and non-secure halves into one ML-DSA-87-signed MCUboot image.
  The PAS kept its address, so **the identity survived every firmware update without re-provisioning.**
- **Real SPDM measurements.** The original example returned placeholder measurements. We changed the secure
  firmware to report:
  1. SHA-384 of the EPBL area,
  2. SHA-384 of exactly the image bytes EPBL verified,
  3. the firmware version (security version number),
  4. a version string.
  These are signed with the ML-DSA-87 identity. A host script (`tools/task4_verify_meas.py`) recomputes them from the
  release files. **They matched after every boot.** So the host gets proof, signed by a quantum-safe key, of which
  verified firmware is running.

### 9.2 Firmware update tests (18/18 as expected)
Boot v1.0.0 → measure/exchange OK → **valid update to v1.1.0** installed (4.95 s, mostly flash erase) → **reconnect**
and measure OK with the same identity → rollback to v1.0.0 **rejected** → corrupted payload **rejected** → modified header
**rejected** → rogue-key image **rejected** → measurement still shows 1.1.0 (so no rejected image ever ran) →
**tampered primary image: device refuses to boot**, SPDM unreachable → recovery via signed update v2.0.0 → measure and
exchange OK.

### 9.3 Credential update tests
- **Rotation:** a new on-device ML-DSA-87 key and new certificate (same root), followed by successful encrypted sessions.
- **Rejected:** request for an ML-DSA-65 key, request for an ECDSA key in the PQC build, a chain too big for the
  slot, and a chain whose hash format didn't match the negotiated one (host rejects).
- A certificate containing a *foreign* public key gets stored (the device doesn't parse X.509), but the
  proof-of-possession check fails, and so would SPDM CHALLENGE. So the provisioning process must check that
  proof before accepting.
- **Finding:** while the PAS is not frozen, the key can be regenerated (replaced). This is by design during
  development; a production device must be frozen and have its debug port closed.

### 9.4 Where the device's identity is anchored
**Both:** a separately provisioned key in the PAS (survives updates, private key never leaves the secure world),
**plus** the verified-boot state reported in signed measurements. The consequence: if EPBL were bypassed, malicious
firmware could still try to use the key through the secure world. It couldn't fake the measurements without
also compromising the secure firmware, though, and in production BootROM authentication of EPBL closes that path.

### 9.5 Migrating a device that shipped with ECC
Since hybrid was excluded, each plane is cut over per device:
1. Field-update the firmware (still verified by the old ECDSA EPBL) to a version containing the PQC code.
2. Replace EPBL with the ML-DSA-87 EPBL. In production FLASH_BOOT authenticates this with the OEM ECDSA key.
3. Generate a new ML-DSA-87 identity on the device. The certificate request is vouched for by the still-valid ECC
   identity, so the CA knows it is the same device.
4. Switch to the PQC-only profile.
5. Requesters drop the ECC root and the ECC CA is revoked.
We executed steps 1–4 on the board across Tasks 1→4.

---

## 10. What is still classical: our honest limits

| Item | Status | What production needs |
|---|---|---|
| ROM_BOOT → FLASH_BOOT → EPBL | classical (ECDSA) by silicon design; on our board EPBL isn't even checked (SIMPLE_APP) | OEM ownership transfer + SECURE_APP policy. Still ECDSA, since the ROM can't be changed; a quantum-safe root needs new silicon |
| EPBL → application | **ML-DSA-87, PQC** | done |
| SPDM identity, signatures, key exchange | **ML-DSA-87 / ML-KEM-1024, PQC**, no classical fallback | done |
| Session encryption | AES-256-GCM, SHA-384 (quantum-adequate) | done |
| Anti-rollback | software version check only | eFuse hardware counter |
| Debug port / PAS | open / not frozen (development) | close DAP, `--freeze` |
| Updates | delivered by debugger | an in-field update transport |

So we **don't** claim the whole chain is quantum-resistant. The first link (immutable ROM, ECDSA) stays
classical. Everything we control from EPBL onward is PQC.

Smaller open items:
- signature verification and decapsulation on the *host* side were not timed. The host tool is a prebuilt
  binary, and the brief's "decapsulation time" is a host-side number in our design because the device only encapsulates;
- message-size limits of application data were not stress-tested;
- the one unexplained mid-handshake fault (failed closed, never repeated).

---

## 11. The comparison on one page

| | ECC baseline | PQC | Verdict |
|---|---|---|---|
| Boot signature | ECDSA P-384, 170 ms | ML-DSA-87 + SHA-512, **61 ms** | faster |
| Reset → app | 266 ms | 146 ms (157 ms with responder) | faster |
| EPBL flash / boot RAM | 36 KB / 10.7 KB | 47 KB / 25.2 KB | +11 KB / +14.5 KB |
| Image metadata | 215 B | 4,771 B | +4.5 KB per slot |
| SPDM signature | 84 ms | 66–346 ms (median 112) | similar, variable |
| Key exchange compute | 163 ms | 11 ms | 15× faster |
| Certificate chain | 1.2 KB | 15.2 KB | fills the 15 KB slot |
| Session traffic | 2.6 KB, 24 transfers | 28 KB, 214 transfers | 11× |
| Runtime RAM buffers | 20 KB | 58 KB | the biggest cost |
| Responder flash | 130 KB | 128 KB | same |

---

## 12. Did we meet the brief? (Tasks 1–4)

Mostly, yes. Every required task bullet, every listed positive test, and every listed negative test was run on
hardware with saved logs:

- **Positive tests:** ECC boot ✔, PQC boot via EPBL ✔, valid update + reboot ✔, ECC SPDM session ✔, PQC SPDM session
  ✔, responder authentication after boot ✔, reconnect after update ✔.
- **Negative tests:** modified responder certificate ✔, invalid signature ✔ (boot and SPDM), modified MCUboot metadata ✔,
  rollback ✔, modified transcript ✔, unsupported/malformed PQC keys ✔ (ML-DSA-65 at boot, in SPDM and in provisioning;
  corrupted and truncated root).
- **Measurements:** security, memory, performance and communication for both planes; migration path documented.
- **Deliverables:** code (git branch `pqc-task1-task2` + `patches/` for the nested responder repo), configs for both
  profiles, provisioning scripts, four PDF reports with reproduction sections.

The places where we didn't fully meet the brief are all documented rather than hidden. The biggest is that
**BootROM doesn't authenticate EPBL on this board**: it needed irreversible steps and an Infineon-signed certificate
we didn't have. The others are the host-side decapsulation/verify timings, which weren't measured, and the
software-only rollback protection.

---

## 13. Glossary

| Term | Meaning |
|---|---|
| **AEAD** | encryption that also detects tampering (AES-GCM) |
| **AES-256-GCM** | symmetric cipher with 256-bit key + authentication tag |
| **BMC** | Baseboard Management Controller, a server's management chip, a typical SPDM requester |
| **CA / root / trust anchor** | the authority whose public key the verifier trusts unconditionally |
| **Category 5** | NIST's top PQC strength level (≈ AES-256); required by CNSA 2.0 |
| **Chunking** | splitting large SPDM messages into small transfers (CHUNK_SEND/GET) |
| **CNSA 2.0** | NSA's algorithm list for national-security systems: ML-KEM-1024, ML-DSA-87, LMS/XMSS, AES-256, SHA-384/512 |
| **CSR** | Certificate Signing Request: "please certify my public key" |
| **DAP** | debug access port (the JTAG/SWD door into the chip) |
| **ECC / ECDSA / ECDHE** | elliptic-curve cryptography; its signature scheme; its key exchange |
| **eFuse** | one-time-programmable bits in silicon (used for irreversible settings) |
| **EPBL** | Edge Protect Bootloader, Infineon's MCUboot-based bootloader that can verify ML-DSA |
| **FIPS 203 / 204** | NIST standards defining ML-KEM / ML-DSA |
| **Forward secrecy** | stealing a long-term key later doesn't decrypt past sessions |
| **HashML-DSA** | ML-DSA applied to a hash of the data instead of the full data |
| **HKDF** | a function that derives many keys from one secret |
| **KEM** | Key Encapsulation Mechanism: encapsulate / decapsulate a shared secret |
| **Lattice cryptography** | math family behind ML-DSA/ML-KEM, believed quantum-resistant |
| **libspdm** | the open-source SPDM library used on the device |
| **Lifecycle state** | one-way chip state (DEVELOPMENT, PRODUCTION…) controlling debug and boot policy |
| **MCUboot** | open-source bootloader and image format (header + image + TLVs) |
| **Measurement** | signed fingerprint of the firmware a device is running |
| **ML-DSA-87** | post-quantum signature, category 5 (2,592 B public key, 4,627 B signature) |
| **ML-KEM-1024** | post-quantum KEM, category 5 (1,568 B key and ciphertext) |
| **NSC veneer** | the fixed entry points from non-secure into secure code (TrustZone) |
| **PAS** | Provisioning Asset Store, secure flash area holding the device key and certificate chain |
| **PQC** | post-quantum cryptography |
| **Rejection sampling** | retry-until-safe technique in ML-DSA signing; makes time variable |
| **Rollback protection** | refusing to install firmware older than the current version |
| **Shor's algorithm** | quantum algorithm that breaks ECC and RSA |
| **SPDM** | DMTF protocol for device authentication, measurements and secure sessions |
| **SPE / NSPE** | secure / non-secure processing environment (TrustZone halves) |
| **TLV** | Type-Length-Value record; MCUboot stores hashes and signatures this way |
| **Transcript** | running hash of all handshake messages; signed to prevent tampering and downgrade |
| **TrustZone** | Arm hardware isolation between secure and non-secure code |
