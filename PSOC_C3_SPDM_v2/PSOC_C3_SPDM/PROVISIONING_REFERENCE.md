# SPDM provisioning reference

This reference records the successful ML-DSA-87 provisioning flow captured on
the PSC3M6 EVAL. It provisions the device key in slot 0, stores the SPDM
certificate chain in slot 1, proves possession of the private key, and freezes
the provisioning store.

## Before running

- Connect the PSC3M6 EVAL's I2C pins to the PC through the MiniProg4 and have
  the provisioning application running on the device.
- Run the command from the `psc3-provisioning-host` directory.
- Replace `<DFUH-CLI-PATH>` and `<MINIPROG4-HWID>` with the values for the
  local setup.
- The example uses `test/ca_mldsa87.crt` as the root CA and corresponding
  `test/ca_mldsa87.key` as the signing key.

## Command

```powershell
python .\test\spdm_provision.py --key-slot 0 --cert-slot 1 --alg ml-dsa-87 --cn "PSC3 DEV 01" --org "ICW" --country "IN" --ou "APP" --serial "SN-0001" --ca-key test/ca_mldsa87.key --ca-crt test/ca_mldsa87.crt --days 365 --chain-cert test/ca_mldsa87.crt --hash-alg sha384 --out-dir test/_work/spdm --freeze --dfuh-cli "<DFUH-CLI-PATH>" --hwid "<MINIPROG4-HWID>"
```

For example, `<DFUH-CLI-PATH>` is the full path to the ModusToolbox
`dfuh-cli.exe`. The capture used ModusToolbox 3.8. The device I2C address and
speed use the configured defaults (`0x08` and `400 kHz`).

## Expected successful output

The full output includes DFU transfer logs and generated certificate details.
The key success indicators from the captured run were:

```text
SPDM provisioning (cert chain)
key slot=0  cert slot=1  alg=ml-dsa-87
RootHash alg=sha384
freeze=True

prov_cmd_status : PROV_OK
provisioned     : NOT_PROVISIONED
slot[0]         : FREE
slot[1]         : FREE
[ok] status

  "algs": [
    {
      "alg_id": 259,
      "pub_len": 2592,
      "sig_len": 4627,
      "alg_id_name": "ML_DSA_87"
    }
  ],
  "prov_cmd_status_name": "PROV_OK"
[ok] info

prov_cmd_status : PROV_OK
alg             : ML_DSA_87 (0x0103)
pub_len         : 2592 bytes
[ok] gen-key

prov_cmd_status : PROV_OK
csr_len         : 7355 bytes
[ok] get-csr

[ok] CA-signed leaf certificate: test\_work\spdm\leaf.crt

prov_cmd_status : PROV_OK
committed       : yes
[ok] put-cert (--spdm-chain)

prov_cmd_status : PROV_OK
[ok] get-cert (read-back)

prov_cmd_status : PROV_OK
alg             : ML_DSA_87 (0x0103)
sig_len         : 4627
OK: device proved possession of the certified private key.
[ok] prove-cert

prov_cmd_status : PROV_OK
provisioned     : PROVISIONED
[ok] finalize (freeze)

prov_cmd_status : PROV_ERR_FROZEN
[ok] reprovision correctly rejected with PROV_ERR_FROZEN

PASS: SPDM cert_chain provisioned + freeze + negative test succeeded.
Stored SPDM blob: test\_work\spdm\spdm_cert_chain.bin
```

Lengths and generated cryptographic values in a new run may differ. The
important success conditions are `PROV_OK` through provisioning and proof of
possession, `PROVISIONED` after finalize, the expected `PROV_ERR_FROZEN`
negative test, and the final `PASS`.

The run writes the CSR, signed leaf certificate, SPDM certificate-chain blob,
and read-back blob under `test/_work/spdm/`. The SPDM chain is root-first:
`test/ca_mldsa87.crt` followed by the device leaf certificate.

> **Warning:** `--freeze` permanently makes the provisioning slots read-only.
> Do not run this flow against a device that has already been provisioned and
> frozen. Do not full-chip erase when changing from the provisioning
> application to the SPDM Responder; that would erase the provisioned assets.
