# PSOC&trade; Control C3 MCU: Secure provisioning (device application)

This code example implements a reusable, on-device **provisioning framework** for the PSOC&trade; Control C3 MCU. It provides the building blocks to inject and protect per-device cryptographic assets: on-device key-pair generation (private key sealed in the secure world), CSR generation for certification by an external CA, certificate/asset import with transfer-integrity checks, and a CRC-protected secure-flash asset store with an irreversible freeze — all behind a DFU command transport and a TrustZone&reg; secure boundary.

The framework is intentionally **generic** and use-case-agnostic. A deployment selects the crypto suite, the asset-store layout, and which commands it uses; the core firmware is reused unchanged. The repository ships with one concrete configuration as a worked example so it can be exercised out of the box — see [Example deployment and walkthrough](docs/design_and_implementation.md#example-deployment-and-walkthrough).

The application has a two-project structure for the CM33 core (secure + non-secure). Provisioning commands arrive over I2C using Infineon's Device Firmware Update (DFU) middleware as a custom command; the non-secure command router forwards each request across the TrustZone&reg; boundary to the secure provisioning service. The host side of the flow is driven by the companion **psc3-provisioning-host** Python CLI.

See the [Design and implementation](docs/design_and_implementation.md) for the project structure, capabilities, command set, and the worked example. To build an independent host tool, see the full wire protocol in [Packet formats](docs/packet_formats.md).


## Requirements

- [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) v3.9 or later (tested with v3.9)
- Board support package (BSP) minimum required version for:
   - KIT_PSC3M5_EVK : v3.0.1
   - KIT_PSC3M6_EVAL: v1.0.0
   - KIT_PSC3M8_EVK : v2.2.0
- Programming language: C (device firmware), Python (host tool)
- Associated parts: [PSOC&trade; Control C3 MCU](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-control-arm-cortex-m33-mcu)


## Supported toolchains (make variable 'TOOLCHAIN')

- GNU Arm&reg; Embedded Compiler v14.2.1 (`GCC_ARM`) – Default value of `TOOLCHAIN`


## Supported kits (make variable 'TARGET')

- [PSOC&trade; Control C3M5 Evaluation Kit](https://www.infineon.com/KIT_PSC3M5_EVK) (`KIT_PSC3M5_EVK`) – Default value of `TARGET`
- [PSOC&trade; Control C3M6 Evaluation Kit](https://www.infineon.com/KIT_PSC3M6_EVAL) (`KIT_PSC3M6_EVAL`)
- [PSOC&trade; Control C3M8 Evaluation Kit](https://www.infineon.com/KIT_PSC3M8_EVK) (`KIT_PSC3M8_EVK`)


## Hardware setup

This example uses the kit's default configuration. See the kit user guide to ensure that the kit is configured correctly.

The provisioning host drives the device over I2C, and the same link carries the DFU command transport. How that link is provided depends on the kit:

- **KIT_PSC3M5_EVK (M5):** the on-board KitProg3 bridge provides both programming/debug and the I2C-DFU transport over a single USB cable — no extra hardware is required.
- **KIT_PSC3M8_EVK (M8):** like the M5 kit, the on-board KitProg3 bridge provides both programming/debug and the I2C-DFU transport over a single USB cable — no extra hardware is required.
- **KIT_PSC3M6_EVAL (M6):** the on-board SEGGER J-Link handles programming/debug only. The I2C-DFU transport requires an external I2C-USB bridge — an Infineon **MiniProg4** — wired to the kit as follows:

   Board connector/pin | Signal | MiniProg4 pin
   :------------------ | :----- | :------------
   J16.5 (P9.0)        | SCL    | Pin 4
   J16.6 (P9.2)        | SDA    | Pin 2
   J15.7               | GND    | Pin 12
   J15.8               | VTARG  | Pin 1


## Software setup

See the [ModusToolbox&trade; tools package installation guide](https://www.infineon.com/ModusToolboxInstallguide) for information about installing and configuring the tools package.

Install Python if it is not already installed — download it from [Python.org](https://www.python.org/downloads/). The host tool dependencies are listed in *psc3-provisioning-host/requirements.txt*.


<details><summary><b>ModusToolbox&trade; Edge Protect Security Suite</b></summary>

The device-policy provisioning steps use the **Edge Protect Tools** (`edgeprotecttools`) CLI.

1. Download and install the [Infineon Developer Center Launcher](https://www.infineon.com/cms/en/design-support/tools/utilities/infineon-developer-center-idc-launcher).

2. Login using your Infineon credentials.

3. Download and install the "ModusToolbox&trade; Edge Protect Security Suite" from the Developer Center Launcher.

    > **Note:** The default installation directory of the Edge Protect Security Suite on Windows is *C:/Users/`<USER>`/Infineon/Tools*.

4. After installing the Edge Protect Security Suite, add the Edge Protect tools executable to the system PATH variable. The executable is located in *<Edge-Protect-Security-Suite-install-path>/ModusToolbox-Edge-Protect-Security-Suite-`<version>`/tools/edgeprotecttools/bin*.

</details>


## Operation

See [Using the code example](docs/using_the_code_example.md) for instructions on getting the application, opening it in the supported IDEs, and building, programming, and debugging it.

1. Connect the board to your PC using the provided USB cable. On KIT_PSC3M6_EVAL, also connect the external MiniProg4 I2C bridge to the device (see [Hardware setup](#hardware-setup)) — it carries the I2C-DFU host transport.

2. (Optional) Open a terminal program and select the kit's COM port (KitProg3 on M5 and M8, J-Link virtual COM on M6). Set the serial port parameters to 8N1 and 115200 baud to view firmware log output.

3. **(KIT_PSC3M5_EVK only) Provision the device with the OEM policy.** On M5 the device must be provisioned with a policy that configures the correct secure/non-secure memory split (MPC) for this two-project application. Helper scripts are provided.

   From the *psc3-provisioning-app* directory, run the provisioning script with modus-shell or Git Bash:

   ```
   sh scripts/provisioning.sh
   ```

   To restore the device to its default configuration later, run:

   ```
   sh scripts/restore_provisioning.sh
   ```

   > **Note:** If you change the application's memory map — either the secure/non-secure split or the Provisioning Asset Store (PAS) region defined in *proj_cm33_s/pas/pas_config.h* — update the MPC configuration in *ns_policy/policy_oem_provisioning.json* accordingly and re-provision the device. The PAS region must be mapped as secure by the MPC.

   > **Note (KIT_PSC3M6_EVAL):** M6 does **not** require this provisioning step to run the secure + non-secure application. On M6 the secure/non-secure memory split (SAU/MPC/PPC) is owned by the Device Configurator (*design.modus*) and applied during `cybsp_init()`, so no OEM policy needs to be provisioned to set it up. Skip straight to building and programming.

4. **Build and program the firmware** to the device:

   ```
   make program TOOLCHAIN=GCC_ARM
   ```

   The top-level Makefile builds and programs both the secure (`proj_cm33_s`) and non-secure (`proj_cm33_ns`) projects. After programming, the device boots into the DFU command loop and waits for provisioning commands over I2C.

   > **Note (signature suite):** The firmware builds the ECDSA P-256 suite by default. Add `PROV_SIG_SUITES` to compile in a different set — `ecdsa-p256`, `ecdsa-p384`, `ecdsa-p521`, `ml-dsa-87` (post-quantum ML-DSA-87 / FIPS 204, **KIT_PSC3M6_EVAL / KIT_PSC3M8_EVK only**), in any combination, e.g. `make program TOOLCHAIN=GCC_ARM TARGET=KIT_PSC3M6_EVAL PROV_SIG_SUITES="ecdsa-p256 ml-dsa-87"`. `PROV_GET_INFO` then advertises exactly the enabled suites. Selecting `ml-dsa-87` on an M5 target is a build error.

5. **Exercise the example.** The device is now provisioning-ready. To drive the shipped example deployment end-to-end from the host, follow the [Example deployment and walkthrough](docs/design_and_implementation.md#example-deployment-and-walkthrough) using the companion **psc3-provisioning-host** Python CLI. The project structure, capabilities, and full command set are described in [Design and implementation](docs/design_and_implementation.md).


## Related resources

Resources  | Links
-----------|----------------------------------
Documentation | [PSOC&trade; Control C3 MCU documents](https://documentation.infineon.com/psoccontrolc3/docs/kfc1732622054982)
Development kits | [PSOC&trade; Control C3 development kits](https://documentation.infineon.com/psoccontrolc3/docs/yyw1732688626489)
Tools, BSPs, libraries, and code examples | [ModusToolbox&trade;](https://documentation.infineon.com/modustoolbox/) – ModusToolbox&trade; software is a collection of easy-to-use libraries and tools enabling rapid development with Infineon MCUs for applications ranging from wireless and cloud-connected systems, edge AI/ML, embedded sense and control, to wired USB connectivity using PSoC&trade; Industrial/IoT MCUs, AIROC&trade; Wi-Fi and Bluetooth&reg; connectivity devices, XMC&trade; Industrial MCUs, and EZ-USB&trade;/EZ-PD&trade; wired connectivity controllers. ModusToolbox&trade; incorporates a comprehensive set of BSPs, HAL, libraries, configuration tools, and provides support for industry-standard IDEs to fast-track your embedded application development

<br>


## Other resources

Infineon provides a wealth of data at [www.infineon.com](https://www.infineon.com) to help you select the right device, and quickly and effectively integrate it into your design.


## Document history

 Version | Description of change
 ------- | ---------------------
 1.0.0   | Initial provisioning device application documentation (Supporting PSC3M5)
 2.0.0   | Added support for PSC3M6 device
 3.0.0   | Added ML-DSA-87 (post-quantum) signature suite support on PSC3M6
 4.0.0   | Added support for PSC3M8 device
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
