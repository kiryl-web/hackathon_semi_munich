# PSOC&trade; Control C3 MCU: Secure provisioning (host tool)

This is the PC-side command-line tool that drives the companion on-device [secure provisioning application](../psc3-provisioning-app). It exposes each provisioning operation — generate a key pair, export a CSR, import a certificate, prove key possession, freeze — as a simple sub-command, and carries them to the device over I2C using Infineon's **Device Firmware Update (DFU) Host Tool** (`dfuh-cli`).

The tool is intentionally **generic** and use-case-agnostic. It mirrors the capabilities the firmware advertises (slots, algorithms, transfer sizes) rather than assuming any particular asset set, so the same CLI works across deployments. It is also a worked reference for building your own host: the protocol is isolated in one module and new commands are added by dropping a single file under `commands/`.

See the [Design and implementation](docs/design_and_implementation.md) for the architecture and module roles, [Using the host tool](docs/using_the_host.md) for configuration and the full command reference, and [Adding commands](docs/adding_commands.md) to extend it. The on-wire protocol is specified in the device app's [Packet formats](../psc3-provisioning-app/docs/packet_formats.md).


## Requirements

- **Python** 3.8 or later.
- **DFU Host Tool** (`dfuh-cli`) — installed with [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) (under `tools_<version>/dfuh-tool/`). This is the only external tool the host shells out to.
- A **KitProg3 / MiniProg** debug bridge providing the I2C link to the target.
- The **device provisioning application** programmed onto the target and running its DFU command loop (see the [device app README](../psc3-provisioning-app/README.md)).


## Install

From this directory:

```
python -m pip install -r requirements.txt
```

`PyYAML` is required; `cryptography` is needed only by the `prove-cert` command (imported lazily, so every other command runs without it). Verifying an **ML-DSA-87** proof-of-possession additionally requires `cryptography >= 46`; ECDSA-P256/P384/P521 works with any recent version.


## Configure

Settings live in [config.yaml](config.yaml) and can be overridden per-invocation by command-line flags. The key values are the debug-bridge `hwid`, the device `i2c_address`, and the path to `dfuh-cli`:

```yaml
device:
  hwid: KitProg3-XXXXXXXXXXXXXXXX   # null => let dfuh-cli auto-detect
  i2c_address: 0x08
  i2c_speed: 400                    # kHz
tools:
  dfuh_cli: dfuh-cli                # path or command name of the DFU Host Tool CLI
```

See [Using the host tool](docs/using_the_host.md#configuration) for every setting and its CLI override.


## Operation

1. Connect the kit and confirm the device application is running.
2. Verify the link with a read-only command:

   ```
   python provisioner.py status
   python provisioner.py info --json
   ```

3. Run any provisioning command, overriding config from the CLI as needed:

   ```
   python provisioner.py --hwid <KitProg3-id> --i2c-address 0x08 gen-key --slot 0
   ```

4. Develop or inspect a flow without hardware using `--dry-run`, which builds the `.mtbdfu` request and prints the decoded on-wire frames instead of invoking `dfuh-cli`:

   ```
   python provisioner.py gen-key --slot 0 --dry-run
   ```

The complete command list, arguments, and global flags are in [Using the host tool](docs/using_the_host.md).


## Related resources

Resources  | Links
-----------|----------------------------------
Device application | [Secure provisioning device application](../psc3-provisioning-app/README.md)
On-wire protocol | [Packet formats](../psc3-provisioning-app/docs/packet_formats.md)
Tools  | [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) — includes the DFU Host Tool (`dfuh-cli`)
Device documentation | [PSOC&trade; Control C3 MCU documents](https://www.infineon.com/cms/en/product/microcontroller/32-bit-psoc-arm-cortex-microcontroller/32-bit-psoc-control-arm-cortex-m33-mcu/psoc-control-c3m/#!documents)


## Other resources

Infineon provides a wealth of data at [www.infineon.com](https://www.infineon.com) to help you select the right device, and quickly and effectively integrate it into your design.


## Document history

 Version | Description of change
 ------- | ---------------------
 1.0.0   | Initial provisioning host tool documentation
 1.1.0   | Added support for SPDM cert chain provisioning + test scripts
 3.0.0   | Added support for PQC (ML-DSA-87)
 5.0.0   | Added ECDSA P-384 and P-521 signature suite support
<br>


All referenced product or service names and trademarks are the property of their respective owners.

The Bluetooth&reg; word mark and logos are registered trademarks owned by Bluetooth SIG, Inc., and any use of such marks by Infineon is under license.

PSOC&trade;, formerly known as PSoC&trade;, is a trademark of Infineon Technologies. Any references to PSoC&trade; in this document or others shall be deemed to refer to PSOC&trade;.

---------------------------------------------------------

(c) 2026, Infineon Technologies AG, or an affiliate of Infineon Technologies AG. All rights reserved.
This software, associated documentation and materials ("Software") is owned by Infineon Technologies AG or one of its affiliates ("Infineon") and is protected by and subject to worldwide patent protection, worldwide copyright laws, and international treaty provisions. Therefore, you may use this Software only as provided in the license agreement accompanying the software package from which you obtained this Software. If no license agreement applies, then any use, reproduction, modification, translation, or compilation of this Software is prohibited without the express written permission of Infineon.
<br>
Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A SPECIFIC USE/PURPOSE OR MERCHANTABILITY. Infineon reserves the right to make changes to the Software without notice. You are responsible for properly designing, programming, and testing the functionality and safety of your intended application of the Software, as well as complying with any legal requirements related to its use. Infineon does not guarantee that the Software will be free from intrusion, data theft or loss, or other breaches ("Security Breaches"), and Infineon shall have no liability arising out of any Security Breaches. Unless otherwise explicitly approved by Infineon, the Software may not be used in any application where a failure of the Product or any consequences of the use thereof can reasonably be expected to result in personal injury.
