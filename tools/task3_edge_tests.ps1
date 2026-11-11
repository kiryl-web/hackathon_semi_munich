# Task 3: hardware edge-case verification (runs in phases; each phase is independent and resumable).
#
#  -Phase P4       genuine P4 responder already on the board (no re-flash):
#                  E1 session stability x2, E2 requester killed mid-handshake -> recovery,
#                  E3 empty cert slot, E4 app-message sizes (small / over transport limit) + recovery,
#                  E5 J-Link reset -> identity persists (challenge).
#  -Phase PROV     programs the provisioning app (ml-dsa-87): device-side credential checks
#                  V1 info, V2 erase, V3/V4 unsupported key algs, V5 ML-DSA-87 keygen, V6 oversize chain,
#                  V7 leaf that does not match the slot key, V8 provision with WRONG RootHash alg (SHA-256),
#                  then programs the P4 responder: V9 requester must reject the SHA-256 RootHash chain.
#  -Phase P2FI     ECC baseline negative tests (Task 1 open item): provisions the P-384 identity, builds the P2
#                  responder with SPDM_FAULT_INJECT=1,2,3,5,6 (4 = ML-KEM only) and a clean P2 image (positive).
#  -Phase RESTORE  provisions the genuine P4 identity again (SHA-384), programs the clean P4 responder,
#                  positive exchange. Leaves the board in the Task 3 end state.
#
# Every test appends one row to <outdir>\summary.csv immediately (survives an aborted run).
# Usage: powershell -ExecutionPolicy Bypass -File tools\task3_edge_tests.ps1 -Phase P4 [-Com COM4] [-Hwid ...]
param(
    [Parameter(Mandatory)][ValidateSet('P4','PROV','P2FI','RESTORE')][string]$Phase,
    [string]$Hwid = '',
    [string]$Com = 'COM4',
    [string[]]$Only = @()      # P4 phase: run only these test ids (e.g. -Only E5); empty = all
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"

$hack   = 'C:\Users\kpato\KP\Hackathon'
$req    = "$SPDM\psoc-spdm-requester\spdm-requester.exe"
$resp   = "$SPDM/psoc-spdm-responder"
$prov   = "$SPDM/psc3-provisioning-app"
$hostd  = "$SPDM\psc3-provisioning-host"
$roots  = "$hack\results\task3_neg_roots"
$jlink  = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$stamp  = Get-Date -Format 'yyyyMMdd_HHmm'
$outDir = "$hack\results\task3_EDGE_${Phase}_$stamp"
New-Item -ItemType Directory -Force $outDir | Out-Null
$csvPath = "$outDir\summary.csv"

if (-not $Hwid) {
    $hw = & $dfuh --display-hw 2>&1 | Out-String
    $m = [regex]::Match($hw, 'I2C:\s*(\S+)')
    if (-not $m.Success) { Write-Host $hw; throw 'No I2C bridge found.' }
    $Hwid = $m.Groups[1].Value
}

# ---- robust UART capture: reopens the port after any error and logs that it did --------------------
$uartLog = "$outDir\device_uart.log"
$uart = Start-Job -ArgumentList $Com, $uartLog -ScriptBlock {
    param($com, $file)
    while ($true) {
        try {
            $p = New-Object System.IO.Ports.SerialPort $com, 115200, 'None', 8, 'One'
            $p.ReadTimeout = 500; $p.ReadBufferSize = 1048576; $p.Open()
            Add-Content -Path $file -Value ('{0:HH:mm:ss.fff} [CAPTURE] port opened' -f (Get-Date))
            while ($true) {
                try { Add-Content -Path $file -Value ('{0:HH:mm:ss.fff} {1}' -f (Get-Date), $p.ReadLine().TrimEnd()) }
                catch [System.TimeoutException] {}
            }
        } catch {
            Add-Content -Path $file -Value ('{0:HH:mm:ss.fff} [CAPTURE] error: {1}' -f (Get-Date), $_.Exception.Message)
            try { $p.Close() } catch {}
            Start-Sleep -Milliseconds 500
        }
    }
}
Start-Sleep -Seconds 1

function Want([string]$id) { return ($Only.Count -eq 0) -or ($Only -contains $id) }
function Mark([string]$t) { Add-Content "$outDir\timeline.txt" ('{0:HH:mm:ss.fff} {1}' -f (Get-Date), $t) }
function Record([string]$name, [string]$expect, [int]$code, [bool]$ok, [double]$wall, [string]$ev) {
    $row = [pscustomobject]@{ test = $name; expect = $expect; exit = $code; ok = $ok; wall_s = [math]::Round($wall,1); evidence = $ev }
    $row | Export-Csv -NoTypeInformation -Append -Encoding UTF8 $csvPath
    Write-Host ("{0,-30} expect={1,-6} exit={2} {3} wall={4,6:N1}s  {5}" -f $name, $expect, $code, $(if ($ok) {'OK'} else {'UNEXPECTED'}), $wall, $ev)
}
function Evidence([string]$o) {
    $l = ($o -split "`n" | Where-Object { $_ -match 'error:|RESULT|exchange: OK|FAIL|reject' } | Select-Object -First 1)
    if ($l) { return $l.Trim() } else { return '' }
}
# expect: PASS (exit 0), REJECT (exit != 0), INFO (record only)
function Req([string]$name, [string]$expect, [string[]]$a) {
    Mark "START $name"
    $t = [Diagnostics.Stopwatch]::StartNew()
    $o = & $req @a --hwid $Hwid --dfuh-cli $dfuh --work-dir "$outDir\work_$name" 2>&1 | Out-String
    $t.Stop(); $code = $LASTEXITCODE
    $o | Set-Content "$outDir\$name.txt"
    $ok = switch ($expect) { 'PASS' { $code -eq 0 } 'REJECT' { $code -ne 0 } default { $true } }
    Record $name $expect $code $ok $t.Elapsed.TotalSeconds (Evidence $o)
    return $o
}
function Prov([string]$name, [string]$expect, [string[]]$a) {
    Mark "START $name"
    $t = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $hostd
    $o = & py -3.13 provisioner.py --hwid $Hwid --timeout-ms 60000 @a 2>&1 | Out-String
    $code = $LASTEXITCODE
    Pop-Location; $t.Stop()
    $o | Set-Content "$outDir\$name.txt"
    $ok = switch ($expect) { 'PASS' { $code -eq 0 } 'REJECT' { $code -ne 0 } default { $true } }
    $ev = ($o -split "`n" | Where-Object { $_ -match 'error|ERROR|status|ok|OK|fail|FAIL' } | Select-Object -Last 1)
    Record $name $expect $code $ok $t.Elapsed.TotalSeconds $(if ($ev) { $ev.Trim() } else { '' })
    return @{ out = $o; code = $code }
}
function Provision([string]$tag, [string]$alg, [string]$ca, [string]$hash) {
    Mark "START provision_$tag"
    Push-Location $hostd
    & py -3.13 provisioner.py --hwid $Hwid erase --slot 1 --yes 2>&1 | Out-Null
    & py -3.13 provisioner.py --hwid $Hwid erase --slot 0 --yes 2>&1 | Out-Null
    $o = & py -3.13 .\test\spdm_provision.py --hwid $Hwid --key-slot 0 --cert-slot 1 --alg $alg `
        --cn "PSC3 DEV 01" --org "ICW" --country "IN" --ou "APP" --serial "SN-0001" `
        --ca-key "test/$ca.key" --ca-crt "test/$ca.crt" --chain-cert "test/$ca.crt" `
        --days 365 --hash-alg $hash --out-dir "test/_work/edge_$tag" 2>&1 | Out-String
    $code = $LASTEXITCODE
    Pop-Location
    $o | Set-Content "$outDir\provision_$tag.txt"
    if ($code -ne 0) { throw "provisioning $tag failed, see provision_$tag.txt" }
    Mark "DONE provision_$tag"
}
function Flash([string]$proj, [string[]]$vars, [string]$tag, [switch]$Clean) {
    Mark "BUILD $tag"
    if ($Clean) { & $make -C $proj clean @vars 2>&1 | Out-Null }
    & $make -C $proj build -j8 @vars 2>&1 | Out-File -Encoding utf8 "$outDir\build_$tag.log"
    if ($LASTEXITCODE -ne 0) { throw "build $tag failed" }
    Mark "PROGRAM $tag"
    & $make -C $proj qprogram @vars 2>&1 | Out-File -Encoding utf8 "$outDir\program_$tag.log"
    if ($LASTEXITCODE -ne 0) { throw "program $tag failed" }
    Start-Sleep -Seconds 4
    Mark "READY $tag"
}
function DerOf([string]$pem, [string]$der) {
    & py -3.13 -c "import sys;from cryptography import x509;from cryptography.hazmat.primitives.serialization import Encoding as E;open(sys.argv[2],'wb').write(x509.load_pem_x509_certificate(open(sys.argv[1],'rb').read()).public_bytes(E.DER))" $pem $der
}

$good = "$roots\good_root.der"
try {
    switch ($Phase) {
    'P4' {
        if (Want 'E1') { Req 'E1a_session_stability' 'PASS' @('session', '--root', $good) | Out-Null
        Req 'E1b_session_stability' 'PASS' @('session', '--root', $good) | Out-Null }
        if (Want 'E2') {
        # E2: abandon a handshake half-way (requester killed during KEY_EXCHANGE chunking), then reconnect.
        Mark 'START E2_killed_mid_handshake'
        $p = Start-Process -FilePath $req -PassThru -NoNewWindow -RedirectStandardOutput "$outDir\E2_killed.txt" `
             -RedirectStandardError "$outDir\E2_killed.err" `
             -ArgumentList 'session','--root',$good,'--hwid',$Hwid,'--dfuh-cli',$dfuh,'--work-dir',"$outDir\work_E2_killed"
        $deadline = (Get-Date).AddSeconds(400); $killed = $false
        $kx0 = @(Select-String -Path $uartLog -Pattern 'large=1638').Count
        while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
            # kill once THIS run's KEY_EXCHANGE request (1,638 B) has been assembled on the device
            if (@(Select-String -Path $uartLog -Pattern 'large=1638').Count -gt $kx0) {
                Start-Sleep -Seconds 8
                Get-Process dfuh-cli -ErrorAction SilentlyContinue | Stop-Process -Force
                Stop-Process -Id $p.Id -Force; $killed = $true; break
            }
            Start-Sleep -Milliseconds 500
        }
        Record 'E2_killed_mid_handshake' 'INFO' $(if ($killed) {0} else {1}) $killed 0 $(if ($killed) {'requester killed during KEY_EXCHANGE_RSP transfer'} else {'kill point not reached'})
        Req 'E2b_reconnect_after_abort' 'PASS' @('exchange', '--root', $good, '--message', 'after aborted handshake') | Out-Null }
        if (Want 'E3') { Req 'E3_empty_cert_slot' 'REJECT' @('get-cert', '--root', $good, '--slot', '1') | Out-Null }
        if (Want 'E4') {
        Req 'E4a_app_msg_90B' 'PASS' @('exchange', '--root', $good, '--message', ('A' * 90)) | Out-Null
        Req 'E4b_app_msg_400B' 'INFO' @('exchange', '--root', $good, '--message', ('B' * 400)) | Out-Null
        Req 'E4c_reconnect_after_large' 'PASS' @('exchange', '--root', $good, '--message', 'after large msg') | Out-Null }
        if (Want 'E5') {
        # E5: target reset via J-Link, identity must persist (PAS), clean image must boot (no [FI] banner)
        Mark 'START E5_reset'
        Set-Content "$env:TEMP\edge_reset.jlink" "connect`nr`ng`nexit`n" -Encoding ASCII
        & $jlink -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile "$env:TEMP\edge_reset.jlink" 2>&1 | Out-File "$outDir\E5_jlink.txt"
        Start-Sleep -Seconds 5
        Req 'E5_challenge_after_reset' 'PASS' @('challenge', '--root', $good) | Out-Null }
    }
    'PROV' {
        # keep the Task 3 identity artifacts for the record
        Copy-Item -Recurse -Force "$hostd\test\_work\spdm_p4" "$hack\results\task3_cred_p4_original"
        Flash $prov @('PROV_SIG_SUITES=ml-dsa-87') 'prov_mldsa87' -Clean
        Prov 'V1_info' 'PASS' @('info') | Out-Null
        Prov 'V2a_erase_cert' 'PASS' @('erase', '--slot', '1', '--yes') | Out-Null
        Prov 'V2b_erase_key' 'PASS' @('erase', '--slot', '0', '--yes') | Out-Null
        Prov 'V3_genkey_mldsa65_unsupported' 'REJECT' @('gen-key', '--slot', '0', '--alg', 'ml-dsa-65') | Out-Null
        Prov 'V4_genkey_ecdsa384_not_built' 'REJECT' @('gen-key', '--slot', '0', '--alg', 'ecdsa-p384') | Out-Null
        Prov 'V5_genkey_mldsa87' 'PASS' @('gen-key', '--slot', '0', '--alg', 'ml-dsa-87') | Out-Null
        Prov 'V5b_genkey_occupied_slot' 'REJECT' @('gen-key', '--slot', '0', '--alg', 'ml-dsa-87') | Out-Null
        # V6: chain larger than the 15,360 B cert slot (root + 2 extra certs)
        $big = Prov 'V6_oversize_chain' 'REJECT' @('put-cert', '--slot', '1', '--key-slot', '0', '--spdm-chain', '--hash-alg', 'sha384',
             '-i', "$hostd\test\ca_mldsa87.crt", '-i', "$roots\rogue_mldsa87_root.der", '-i', "$roots\mldsa65_root.der")
        # V7: leaf whose public key is NOT the slot-0 key -> must fail at put-cert or at prove-cert
        $pc = Prov 'V7a_putcert_foreign_leaf' 'INFO' @('put-cert', '--slot', '1', '--key-slot', '0', '--spdm-chain', '--hash-alg', 'sha384',
             '-i', "$hostd\test\ca_mldsa87.crt", '-i', "$roots\rogue_mldsa87_root.der", '--save-blob', "$outDir\foreign_blob.bin")
        if ($pc.code -eq 0) {
            Prov 'V7b_prove_foreign_leaf' 'REJECT' @('prove-cert', '--slot', '1', '-i', "$outDir\foreign_blob.bin") | Out-Null
        } else {
            Record 'V7b_prove_foreign_leaf' 'REJECT' 1 $true 0 'rejected already at put-cert (V7a)'
        }
        # V8/V9: complete provisioning with RootHash = SHA-256 while the profile negotiates SHA-384
        Provision 'p4_sha256' 'ml-dsa-87' 'ca_mldsa87' 'sha256'
        Flash $resp @('SPDM_PROFILE=P4', 'SPDM_FAULT_INJECT=0') 'resp_p4'
        Req 'V9_roothash_sha256_vs_sha384' 'REJECT' @('get-cert', '--root', $good) | Out-Null
    }
    'P2FI' {
        $p2root = "$outDir\ca_ec384.der"; DerOf "$hostd\test\ca_ec384.crt" $p2root
        Flash $prov @('PROV_SIG_SUITES=ecdsa-p384') 'prov_ec384' -Clean
        Provision 'p2' 'ecdsa-p384' 'ca_ec384' 'sha384'
        $first = $true
        foreach ($fi in 1,2,3,5,6) {
            if ($first) { Flash $resp @('SPDM_PROFILE=P2', "SPDM_FAULT_INJECT=$fi") "resp_p2_fi$fi" -Clean; $first = $false }
            else        { Flash $resp @('SPDM_PROFILE=P2', "SPDM_FAULT_INJECT=$fi") "resp_p2_fi$fi" }
            switch ($fi) {
                1 { Req 'P2_B1_modified_cert'       'REJECT' @('get-cert', '--root', $p2root) | Out-Null }
                2 { Req 'P2_B2_invalid_signature'   'REJECT' @('challenge', '--root', $p2root) | Out-Null }
                3 { Req 'P2_B3_modified_transcript' 'REJECT' @('session', '--root', $p2root) | Out-Null }
                5 { Req 'P2_B5_mitm_finish_rsp'     'REJECT' @('session', '--root', $p2root) | Out-Null }
                6 { Req 'P2_B6_mitm_app_data'       'REJECT' @('exchange', '--root', $p2root, '--message', 'p2 tamper') | Out-Null }
            }
        }
        Flash $resp @('SPDM_PROFILE=P2', 'SPDM_FAULT_INJECT=0') 'resp_p2_clean'
        Req 'P2_R1_clean_exchange' 'PASS' @('exchange', '--root', $p2root, '--message', 'p2 clean') | Out-Null
        Req 'P2_R2_pqc_root_on_ecc_device' 'REJECT' @('get-cert', '--root', $good) | Out-Null
    }
    'RESTORE' {
        Flash $prov @('PROV_SIG_SUITES=ml-dsa-87') 'prov_mldsa87' -Clean
        Provision 'p4' 'ml-dsa-87' 'ca_mldsa87' 'sha384'
        # new identity artifacts stay in test/_work/edge_p4 (Task 3 evidence keeps spdm_p4 / task3_cred_p4_original)
        Flash $resp @('SPDM_PROFILE=P4', 'SPDM_FAULT_INJECT=0') 'resp_p4_clean' -Clean
        Req 'R_restore_exchange' 'PASS' @('exchange', '--root', $good, '--message', 'restored P4') | Out-Null
        Req 'R_repeat_exchange_2' 'PASS' @('exchange', '--root', $good, '--message', 'repeat 2') | Out-Null
        Req 'R_repeat_exchange_3' 'PASS' @('exchange', '--root', $good, '--message', 'repeat 3') | Out-Null
        Req 'R_restore_get_cert' 'PASS' @('get-cert', '--root', $good, '--out', "$outDir\chain.der") | Out-Null
    }
    }
} finally {
    Start-Sleep -Seconds 1
    Stop-Job $uart; Remove-Job $uart
    if (Test-Path $csvPath) { Import-Csv $csvPath | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object "$outDir\summary.txt" }
    Write-Host "Results in $outDir"
}
