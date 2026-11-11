# PSC3 provisioning and SPDM quick start

This guide walks through provisioning a PSC3 device with an ML-DSA-87 key and
SPDM certificate chain, then running the SPDM Responder and Requester examples.
It is written for the Windows workspace ZIP.

For detailed requirements and troubleshooting, see the
[provisioning app README](psc3-provisioning-app/README.md),
[provisioning host README](psc3-provisioning-host/README.md), and
[SPDM Responder README](psoc-spdm-responder/README.md). The Requester has its
own [user guide](psoc-spdm-requester/USER_GUIDE.pdf).

## 1. Unzip, open, and connect the hardware

1. Unzip the complete workspace to a local folder. In Eclipse IDE for
   ModusToolbox, open that unzipped folder as the workspace.
2. Connect the PSC3M6 EVAL to the PC using its J-Link USB connection for
   programming/debug. Connect an external MiniProg4 as the I2C-to-USB bridge
   for provisioning and SPDM communication. Follow the pin table in the
   [provisioning app hardware setup](psc3-provisioning-app/README.md#hardware-setup).

## 2. Build and flash the provisioning application

1. In Eclipse, choose **Import Existing Application In-Place** and import
   `psc3-provisioning-app` from the unzipped workspace.
2. Build the application. This workspace is configured for the `ml-dsa-87`
   provisioning signature suite; confirm `PROV_SIG_SUITES` in
   `psc3-provisioning-app/common.mk` still includes `ml-dsa-87` if the build
   configuration has been changed.
3. Before provisioning assets, perform a **full-device erase** using the
   programmer, then program the provisioning application. This erase is
   destructive; do it before provisioning, not when switching to the
   responder.

After flashing, the device should be running the provisioning app and waiting
for host commands over I2C.

## 3. Configure and run the one-step provisioning flow

1. Edit `psc3-provisioning-host/config.yaml`:
   - Set `device.hwid` to the connected MiniProg4 hardware ID.
   - Set `tools.dfuh_cli` to the installed DFU Host Tool CLI command or its
     full path, for example `'C:\path\to\dfuh-cli.exe'`.
   - The default I2C address is `0x08`; it must match the provisioning
     application configuration.
2. Install the host dependencies if they are not already installed:

   ```powershell
   cd .\psc3-provisioning-host
   python -m pip install -r requirements.txt
   ```

3. From the `psc3-provisioning-host` directory, run:

   ```powershell
   python .\test\spdm_provision.py --key-slot 0 --cert-slot 1 --alg ml-dsa-87 --cn "PSC3 DEV 01" --org "ICW" --country "IN" --ou "APP" --serial "SN-0001" --ca-key test/ca_mldsa87.key --ca-crt test/ca_mldsa87.crt --days 365 --chain-cert test/ca_mldsa87.crt --hash-alg sha384 --out-dir test/_work/spdm --freeze
   ```

The script generates the device key, creates and signs a CSR with the supplied
CA, installs and verifies the SPDM certificate chain, and then freezes
provisioning. **`--freeze` is irreversible:** after it succeeds, the
provisioned slots cannot be changed through the provisioning application.
The root CA used for this example is
`psc3-provisioning-host/test/ca_mldsa87.crt`. The Requester must also be
configured to trust this same root CA for certificate authentication and
secure-session establishment. The bundled CA is for this example; use your
controlled CA and certificates for a real deployment.

## 4. Build and flash the SPDM Responder

1. In Eclipse, choose **Import Existing Application In-Place** again and
   import `psoc-spdm-responder` into the same workspace.
2. Confirm `SPDM_PROFILE` is `P4` in `psoc-spdm-responder/common.mk`. P4 uses
   ML-DSA-87 and SHA-384, matching the assets provisioned above. If you change
   the profile, clean the project before rebuilding.
3. Build and program the responder application using its Eclipse **Quick
   Panel** actions.

**Do not erase the complete device or its provisioning asset store when
flashing the responder.** The key and certificate chain are stored in the
device's secure asset store and must survive the application change. Program
the responder application only.

When it starts, the responder waits for SPDM commands over the same I2C
connection.

## 5. Exercise SPDM with the Requester

Run `psoc-spdm-requester/spdm-requester.exe` and follow the bundled
[Requester user guide](psoc-spdm-requester/USER_GUIDE.pdf) for its command
syntax and allowed commands. Supply the same bridge interface details used
for provisioning (hardware ID and DFU Host Tool path), and pass
`..\psc3-provisioning-host\test\ca_mldsa87.crt` as the trusted root CA as
described in the user guide.

The guide describes how to query the responder, authenticate its identity and
measurements, establish a secure session, exchange an encrypted message and
close the session.

## More information

- [Provisioning app hardware setup and operation](psc3-provisioning-app/README.md)
- [Provisioning host configuration and command reference](psc3-provisioning-host/docs/using_the_host.md)
- [SPDM Responder hardware setup and operation](psoc-spdm-responder/README.md)
