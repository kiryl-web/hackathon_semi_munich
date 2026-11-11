# Task 1, step B: provision the ECC (ECDSA P-384) SPDM identity.
# Precondition: psc3-provisioning-app is programmed on the board and the
# MiniProg4/KitProg3 I2C bridge is connected.
# Usage:  powershell -ExecutionPolicy Bypass -File C:\Users\kpato\KP\Hackathon\tools\task1_provision_p2.ps1 [-Hwid KitProg3-XXXX]
param([string]$Hwid = '')
# 'Continue': PowerShell 5.1 turns native stderr (INFO logs) into errors; we check $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"

if (-not $Hwid) {
    $hw = & $dfuh --display-hw 2>&1 | Out-String
    $m = [regex]::Match($hw, 'I2C:\s*(\S+)')
    if (-not $m.Success) { Write-Host $hw; throw 'No I2C bridge found. Connect the MiniProg4/KitProg3 (I2C mode) or pass -Hwid.' }
    $Hwid = $m.Groups[1].Value
}
Write-Host "Using I2C bridge: $Hwid"

$out = 'C:\Users\kpato\KP\Hackathon\results\task1_provision_p2.log'
Push-Location $SPDM\psc3-provisioning-host
try {
    & py -3.13 provisioner.py --hwid $Hwid info 2>&1 | Tee-Object -FilePath $out
    if ($LASTEXITCODE -ne 0) { throw 'provisioner info failed (is the provisioning app running? bridge wired?)' }
    & py -3.13 provisioner.py --hwid $Hwid erase --slot 1 --yes 2>&1 | Tee-Object -FilePath $out -Append
    & py -3.13 provisioner.py --hwid $Hwid erase --slot 0 --yes 2>&1 | Tee-Object -FilePath $out -Append
    # NOTE: no --freeze (irreversible). Slots stay re-provisionable for Task 3.
    & py -3.13 .\test\spdm_provision.py --hwid $Hwid --key-slot 0 --cert-slot 1 --alg ecdsa-p384 `
        --cn "PSC3 DEV 01" --org "ICW" --country "IN" --ou "APP" --serial "SN-0001" `
        --ca-key test/ca_ec384.key --ca-crt test/ca_ec384.crt --chain-cert test/ca_ec384.crt `
        --days 365 --hash-alg sha384 --out-dir test/_work/spdm_p2 2>&1 | Tee-Object -FilePath $out -Append
    if ($LASTEXITCODE -ne 0) { throw 'spdm_provision.py failed - see log' }
    Get-ChildItem test/_work/spdm_p2 | Select-Object Name, Length | Tee-Object -FilePath $out -Append
    Write-Host "`nOK. Log: $out"
} finally { Pop-Location }

