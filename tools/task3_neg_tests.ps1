# Task 3: negative SPDM tests for the P4 (ML-DSA-87 / ML-KEM-1024) responder.
#  Part A (host side, normal responder): bad trust anchors from task3_make_neg_roots.py.
#  Part B (device side): rebuild the responder with SPDM_FAULT_INJECT=<mode>, program it,
#          run the requester op that must detect the fault (expected exit=1).
#  Finally the clean responder (SPDM_FAULT_INJECT=0) is rebuilt/programmed and a
#  positive 'exchange' proves recovery / reconnect.
# Usage:
#   powershell -ExecutionPolicy Bypass -File C:\Users\kpato\KP\Hackathon\tools\task3_neg_tests.ps1
#   [-Profile P4] [-Modes 1,2,3,4,5,6] [-SkipHost] [-SkipDevice] [-Com COM4] [-Hwid KitProg3-...]
param(
    [ValidateSet('P2','P4')][string]$Profile = 'P4',
    [int[]]$Modes = @(1,2,3,4,5,6),
    [switch]$SkipHost,
    [switch]$SkipDevice,
    [string]$Hwid = '',
    [string]$Com = 'COM4'
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"

$hack   = 'C:\Users\kpato\KP\Hackathon'
$req    = "$SPDM\psoc-spdm-requester\spdm-requester.exe"
$resp   = "$SPDM/psoc-spdm-responder"
$stamp  = Get-Date -Format 'yyyyMMdd_HHmm'
$outDir = "$hack\results\task3_NEG_${Profile}_$stamp"
New-Item -ItemType Directory -Force $outDir | Out-Null
$roots  = "$hack\results\task3_neg_roots"
if (-not (Test-Path "$roots\good_root.der")) {
    & py -3.13 "$PSScriptRoot\task3_make_neg_roots.py" "$SPDM\psc3-provisioning-host" $roots
}
$good = "$roots\good_root.der"
if ($Profile -eq 'P2') {
    # P2 baseline: genuine root is the P-384 demo CA.
    & py -3.13 -c "import sys;from cryptography import x509;from cryptography.hazmat.primitives.serialization import Encoding as E;open(sys.argv[2],'wb').write(x509.load_pem_x509_certificate(open(sys.argv[1],'rb').read()).public_bytes(E.DER))" "$SPDM\psc3-provisioning-host\test\ca_ec384.crt" "$outDir\good_root_p2.der"
    $good = "$outDir\good_root_p2.der"
}

if (-not $Hwid) {
    $hw = & $dfuh --display-hw 2>&1 | Out-String
    $m = [regex]::Match($hw, 'I2C:\s*(\S+)')
    if (-not $m.Success) { Write-Host $hw; throw 'No I2C bridge found.' }
    $Hwid = $m.Groups[1].Value
}

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
function Mark([string]$text) { Add-Content "$outDir\timeline.txt" ('{0:HH:mm:ss.fff} {1}' -f (Get-Date), $text) }
function Run-Op([string]$name, [string]$expect, [string[]]$opArgs) {
    Mark "START $name"
    $t = [Diagnostics.Stopwatch]::StartNew()
    $o = & $req @opArgs @common --work-dir "$outDir\work_$name" 2>&1 | Out-String
    $t.Stop()
    $o | Set-Content "$outDir\$name.txt"
    $code = $LASTEXITCODE
    $ok = (($expect -eq 'PASS') -and ($code -eq 0)) -or (($expect -eq 'REJECT') -and ($code -ne 0))
    $err = ($o -split "`n" | Where-Object { $_ -match 'error|FAIL' } | Select-Object -First 1)
    if ($err) { $err = $err.Trim() }
    Write-Host ("{0,-26} expect={1,-6} exit={2} {3} wall={4,6:N1}s  {5}" -f $name, $expect, $code, ($(if ($ok) {'OK'} else {'UNEXPECTED'})), $t.Elapsed.TotalSeconds, $err)
    $script:summary += [pscustomobject]@{ test = $name; expect = $expect; exit = $code; ok = $ok; wall_s = [math]::Round($t.Elapsed.TotalSeconds,1); evidence = $err }
}
function Flash-Responder([int]$fi) {
    Mark "BUILD FI=$fi"
    & $make -C $resp build -j8 SPDM_PROFILE=$Profile SPDM_FAULT_INJECT=$fi 2>&1 | Out-File -Encoding utf8 "$outDir\build_fi$fi.log"
    if ($LASTEXITCODE -ne 0) { throw "build FI=$fi failed, see build_fi$fi.log" }
    Mark "PROGRAM FI=$fi"
    & $make -C $resp qprogram SPDM_PROFILE=$Profile SPDM_FAULT_INJECT=$fi 2>&1 | Out-File -Encoding utf8 "$outDir\program_fi$fi.log"
    if ($LASTEXITCODE -ne 0) { throw "program FI=$fi failed, see program_fi$fi.log" }
    Start-Sleep -Seconds 3
    $banner = Select-String -Path $uartLog -Pattern "\[FI\] FAULT-INJECTION BUILD, mode $fi" -Quiet
    if (($fi -ne 0) -and -not $banner) { Write-Host "WARNING: FI=$fi banner not seen on UART" }
    Mark "READY FI=$fi (banner=$banner)"
}

try {
    if (-not $SkipHost) {
        # Part A: wrong / malformed trust anchors against the genuine responder.
        $hostCases = [ordered]@{
            'A1_rogue_mldsa87_root'   = 'rogue_mldsa87_root.der'
            'A2_mldsa65_root'         = 'mldsa65_root.der'
            'A3_classical_ec384_root' = 'ec384_root.der'
            'A4_malformed_key_root'   = 'malformed_key_root.der'
            'A5_truncated_root'       = 'truncated_root.der'
        }
        foreach ($k in $hostCases.Keys) { Run-Op $k 'REJECT' @('get-cert', '--root', "$roots\$($hostCases[$k])") }
    }
    if (-not $SkipDevice) {
        # Part B: device fault injection. op = requester operation that must detect it.
        $dev = @{
            1 = @('B1_modified_cert',        'get-cert')
            2 = @('B2_invalid_signature',    'challenge')
            3 = @('B3_modified_transcript',  'session')
            4 = @('B4_modified_kem_ct',      'session')
            5 = @('B5_mitm_finish_rsp',      'session')
            6 = @('B6_mitm_app_data',        'exchange')
        }
        foreach ($fi in $Modes) {
            Flash-Responder $fi
            $name, $op = $dev[$fi]
            $a = @($op, '--root', $good)
            if ($op -eq 'get-cert') { $a += @('--out', "$outDir\${name}_chain.der") }
            if ($op -eq 'exchange') { $a += @('--message', 'task3 tamper test') }
            Run-Op $name 'REJECT' $a
        }
        # Restore the genuine responder and prove reconnect after the bad images.
        Flash-Responder 0
        for ($i = 1; $i -le 3; $i++) {
            Run-Op "R${i}_restore_exchange" 'PASS' @('exchange', '--root', $good, '--message', "task3 after restore $i")
        }
    }
} finally {
    Start-Sleep -Seconds 1
    Stop-Job $uart; Remove-Job $uart
    $summary | Export-Csv -NoTypeInformation -Encoding UTF8 "$outDir\summary.csv"
    $summary | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object "$outDir\summary.txt"
    Write-Host "Results in $outDir"
}
