[Click here](../README.md) to view the README.

## Adding a command

The CLI is a plugin registry: every sub-command is a `ProvCommand` subclass in its own module under `commands/`, decorated with `@register`. At startup `core/registry.py`'s `discover()` imports every module in that package, the decorator records the class in `COMMANDS`, and `provisioner.py` builds an `argparse` sub-parser from it. **Adding a command therefore means writing one new file — no edits to the core or the CLI entry point.**

For the architecture context see [Design and implementation](design_and_implementation.md).


## The `ProvCommand` interface

A command declares a few class attributes and overrides the hooks it needs (defined in [core/registry.py](../core/registry.py)):

Member                         | Purpose
:----------------------------- | :------
`name`                         | CLI verb (e.g. `"gen-key"`). Required and unique.
`opcode`                       | Provisioning opcode from `packet.Opcode`.
`help`                         | One-line help shown in the top-level command list.
`description`                  | Fuller paragraph shown above the argument list in `<command> -h` (falls back to `help`).
`examples`                     | Sequence of example command lines rendered in the help epilog.
`add_arguments(cls, parser)`   | Declare command-specific CLI arguments.
`build_request(self, args)`    | Serialize the request payload (the bytes **after** the opcode). Returns `bytes`.
`parse_response(self, payload)`| Decode the response payload into a dict. `payload[0]` is the result code (`prov_cmd_status`).
`render(self, result, args, ctx)` | Print the human-readable result.
`run(self, ctx, args)`         | Optional. Override only for multi-packet / custom flows; otherwise the base class drives the standard single-packet transaction.

The base `run()` already performs `build_request → Session.send_command → parse_response → render → exit code`, so most commands only implement the four small hooks.


## Step-by-step

1. **Reserve the opcode and any shared structures in `core/packet.py`.** Add the value to the `Opcode` enum (and any new `ProvStatus`, struct parser, or builder the command needs). Keeping all wire constants here keeps the protocol in one place.

2. **Create `commands/<your_command>.py`.** Subclass `ProvCommand`, set the class attributes, and implement the hooks. Example — a hypothetical read-only command that returns a 32-bit counter:

   ```python
   """PROV_GET_COUNTER (0x54) -- read the provisioning attempt counter."""
   from __future__ import annotations

   from core.packet import Opcode, ProtocolError, ProvStatus, enum_name
   from core.registry import ProvCommand, register


   @register
   class CounterCommand(ProvCommand):
       name = "get-counter"
       opcode = Opcode.GET_COUNTER          # add to packet.Opcode first
       help = "Read the provisioning attempt counter (PROV_GET_COUNTER, 0x54)"
       description = "Read the monotonic provisioning attempt counter (read-only)."
       examples = ("provisioner get-counter",)

       @classmethod
       def add_arguments(cls, parser):
           parser.add_argument("--json", action="store_true",
                               help="Emit the decoded value as JSON")

       def build_request(self, args) -> bytes:
           return b""                        # no request payload

       def parse_response(self, payload: bytes) -> dict:
           if len(payload) < 5:
               raise ProtocolError(
                   f"PROV_GET_COUNTER response too short: {len(payload)} bytes (need >=5)")
           return {
               "prov_cmd_status": payload[0],
               "counter": int.from_bytes(payload[1:5], "little"),
           }

       def render(self, result: dict, args, ctx) -> None:
           print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
           if result.get("prov_cmd_status") == ProvStatus.PROV_OK:
               print(f"counter         : {result['counter']}")
   ```

3. **Done — no registration plumbing.** The `@register` decorator adds the class to `COMMANDS`, `discover()` imports the module automatically, and the verb appears in `provisioner.py -h`.

4. **Test without hardware** using `--dry-run`, which prints the generated `.mtbdfu` and decoded frames:

   ```
   python provisioner.py get-counter --dry-run
   ```


## Commands that transfer more than one packet

For blobs larger than one packet (CSR, certificate, public key), override `run()` instead of the four hooks and use the `Session` helpers in [core/session.py](../core/session.py), which keep all chunks inside a single Enter/Exit-DFU session:

- `Session.run_commands(items)` — emit several commands in one DFU transaction (needed when device state is transient and must survive across chunks).
- `Session.read_blob(opcode, slot_id, template, max_chunk)` — probe the total length at offset 0, then loop fixed-size reads.
- `Session.read_pubkey(slot_id)` — the public-key variant with its own response header.

Use `gen-key`/`status` as templates for simple single-packet commands, and `get-csr`/`get-cert`/`read-pubkey` as templates for chunked transfers.


## Conventions

- Keep all wire constants, framing, and field offsets in `core/packet.py`; command modules should reference its enums and parsers rather than hard-coding bytes.
- Raise `ProtocolError` (from `core/packet.py`) on malformed or truncated responses.
- Return `prov_cmd_status` in the result dict so the base `run()` can map it to the process exit code (`0` on `PROV_OK`, else the status value).
