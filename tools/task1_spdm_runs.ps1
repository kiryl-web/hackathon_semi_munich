# Task 1, step D: run the SPDM requester against the responder and capture
# requester output + device [PERF] UART log for each operation.
# Precondition: psoc-spdm-responder (P2 or P4) is programmed, credentials provisioned,
# I2C bridge connected. Close any serial terminal on the board COM port first.
# Usage:
#   powershell -ExecutionPolicy Bypass -File C:\Users\kpato\KP\Hackathon\tools\task1_spdm_runs.ps1 -Profile P2
#   (P4: -Profile P4)   optional: -Hwid KitProg3-XXXX  -Com COM4
param(
    [ValidateSet('P2','P4')][string]$Profile = 'P2',
    [string]$Hwid = '',
    [string]$Com = 'COM4',
    [int]$Repeat = 3,
    [string]$Tag = 'task1'
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"

$caPem  = @{ P2 = 'ca_ec384.crt'; P4 = 'ca_mldsa87.crt' }[$Profile]
$badPem = @{ P2 = 'ca_ec256.crt'; P4 = 'ca_ec384.crt' }[$Profile]   # wrong trust anchor
$req    = "$SPDM\psoc-spdm-requester\spdm-requester.exe"
$stamp  = Get-Date -Format 'yyyyMMdd_HHmm'
$outDir = "C:\Users\kpato\KP\Hackathon\results\${Tag}_${Profile}_$stamp"
New-Item -ItemType Directory -Force $outDir | Out-Null

if (-not $Hwid) {
    $hw = & $dfuh --display-hw 2>&1 | Out-String
    $m = [regex]::Match($hw, 'I2C:\s*(\S+)')
    if (-not $m.Success) { Write-Host $hw; throw 'No I2C bridge found. Connect it or pass -Hwid.' }
    $Hwid = $m.Groups[1].Value
}

# Requester expects DER trust anchors; convert the demo CA PEMs.
$conv = @"
import sys
from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding
for src, dst in zip(sys.argv[1::2], sys.argv[2::2]):
    open(dst,'wb').write(x509.load_pem_x509_certificate(open(src,'rb').read()).public_bytes(Encoding.DER))
"@
$root = "$outDir\root.der"; $bad = "$outDir\wrong_root.der"
& py -3.13 -c $conv "$SPDM\psc3-provisioning-host\test\$caPem" $root "$SPDM\psc3-provisioning-host\test\$badPem" $bad

# Device UART capture ([PERF] lines) in the background.
$uartLog = "$outDir\device_uart.log"
$uart = Start-Job -ArgumentList $Com, $uartLog -ScriptBlock {
    param($com, $file)
    $p = New-Object System.IO.Ports.SerialPort $com, 115200, 'None', 8, 'One'
    $p.ReadTimeout = 500; $p.Open()
    while ($true) {
        try { Add-Content -Path $file -Value ('{0:HH:mm:ss.fff} {1}' -f (Get-Date), $p.ReadLine().TrimEnd()) }
        catch [System.TimeoutException] {}
    }
}
Start-Sleep -Seconds 1

$common = @('--hwid', $Hwid, '--dfuh-cli', $dfuh)
$summary = @()
function Run-Op([string]$name, [string[]]$opArgs) {
    $wd = "$outDir\work_$name"
    Add-Content "$outDir\timeline.txt" ('{0:HH:mm:ss.fff} START {1}' -f (Get-Date), $name)
    $t = [Diagnostics.Stopwatch]::StartNew()
    $o = & $req @opArgs @common --work-dir $wd 2>&1 | Out-String
    $t.Stop()
    $o | Set-Content "$outDir\$name.txt"
    $pass = ($LASTEXITCODE -eq 0)
    Write-Host ("{0,-22} exit={1} wall={2,6:N1}s" -f $name, $LASTEXITCODE, $t.Elapsed.TotalSeconds)
    $script:summary += [pscustomobject]@{ op = $name; exit = $LASTEXITCODE; pass = $pass; wall_s = [math]::Round($t.Elapsed.TotalSeconds,1) }
}

# Positive tests
Run-Op 'connect'   @('connect')
Run-Op 'slots'     @('slots')
Run-Op 'get_cert'  @('get-cert', '--root', $root, '--out', "$outDir\chain.der")
Run-Op 'challenge' @('challenge', '--root', $root)
Run-Op 'measure'   @('measure', '--root', $root)
Run-Op 'session'   @('session', '--root', $root)
for ($i = 1; $i -le $Repeat; $i++) {
    Run-Op "exchange_$i" @('exchange', '--root', $root, '--message', "task1 $Profile msg $i")
}
# Negative test: wrong trust anchor must be rejected (expected exit=1)
Run-Op 'NEG_wrong_root' @('challenge', '--root', $bad)

Start-Sleep -Seconds 1
Stop-Job $uart; Remove-Job $uart

$summary | Format-Table -AutoSize | Out-String | Tee-Object "$outDir\summary.txt"
"NEG_wrong_root is EXPECTED to fail (exit=1)." | Tee-Object "$outDir\summary.txt" -Append
Write-Host "Results in $outDir  (device [PERF] lines: device_uart.log)"

