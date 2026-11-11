# Task 4: lifecycle tests with the SPDM responder BEHIND the Edge Protect Bootloader.
#
# Flash map (512 KB): EPBL 0x32000000-0x3201B000 | responder slot 0x3201B000-0x32048000 |
#                     PAS (SPDM identity, untouched) 0x32048000-0x3204E000 | upgrade slot 0x3204E000-0x3207B000
#
#  -Phase PREP   (no board) sign all responder images with ML-DSA-87 / SHA-512 and build the tamper variants
#                from the resp_epbl build (TARGET=APP_KIT_PSC3M6_EVAL_EPBL) and the EPBL spdm-map build.
#  -Phase BOARD  T0 boot v1.0.0 + SPDM | T1 valid update v1.1.0 + SPDM | T2 rollback | T3 corrupt payload |
#                T4 modified header | T5 rogue key | T6 SPDM still v1.1.0 | T7 tampered primary (no boot) |
#                T8 recovery by signed update v2.0.0 + SPDM reconnect. Leaves EPBL + responder v2.0.0.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\task4_epbl_spdm_tests.ps1 -Phase PREP|BOARD [-Only T0,T1]
param(
    [Parameter(Mandatory)][ValidateSet('PREP','BOARD')][string]$Phase,
    [string]$Hwid = '',
    [string]$Com = 'COM4',
    [string[]]$Only = @(),
    [switch]$NoSpdm            # boot checks only (skip the ~5 min SPDM operations)
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"

$hack  = 'C:\Users\kpato\KP\Hackathon'
$img   = "$hack\results\task4_boot"
$resp  = "$SPDM\psoc-spdm-responder"   # PREP: build it first with TARGET=APP_KIT_PSC3M6_EVAL_EPBL SPDM_PROFILE=P4
$req   = "$SPDM\psoc-spdm-requester\spdm-requester.exe"
$good  = "$hack\results\task3_neg_roots\good_root.der"
$keys  = "$hack\mtb-example-ce243041-secure-boot-and-update-with-epb-master\keys"
$key   = "$keys\ml_dsa_87_img_sign_key_priv.der"
$rogue = "$keys\rogue_ml_dsa_87_priv.der"
$jlink = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
New-Item -ItemType Directory -Force $img | Out-Null

function Sign([string]$kind, [string]$ver, [string]$k, [string]$out) {
    py -3.13 -I "$PSScriptRoot\resp_epbl_sign.py" --combined "$resp\build\app_combined.hex" --kind $kind --version $ver --key $k --out "$img\$out" 2>&1 | Out-String | Write-Host
    if (-not (Test-Path "$img\$out")) { throw "signing $out failed" }
}
function Flip([string]$in, [string]$out, [string]$addr) {
    py -3.13 -I "$PSScriptRoot\hex_flip.py" "$img\$in" "$img\$out" $addr | Write-Host
}

if ($Phase -eq 'PREP') {
    # build/app_combined.hex is TARGET-independent -> always (re)build the EPBL variant right before signing
    & $make -C $resp build -j8 TARGET=APP_KIT_PSC3M6_EVAL_EPBL SPDM_PROFILE=P4 SPDM_FAULT_INJECT=0 2>&1 | Out-File -Encoding utf8 "$img\resp_epbl_build.log"
    if ($LASTEXITCODE -ne 0) { throw "responder EPBL build failed, see $img\resp_epbl_build.log" }
    Copy-Item "$hack\epbl\bl\build\app_combined.hex" "$img\epbl_spdm_map.hex" -Force
    py -3.13 -I "$PSScriptRoot\hex_pad.py" "$img\epbl_spdm_map.hex" "$img\epbl_full.hex" 0x32000000 0x3201B000
    py -3.13 -I "$PSScriptRoot\hex_pad.py" - "$img\zero_upgrade.hex" 0x3204E000 0x3207B000
    Sign boot   1.0.0 $key   'resp_v1_0_0_boot.hex'
    Sign boot   1.1.0 $key   'resp_v1_1_0_boot.hex'
    Sign update 1.1.0 $key   'resp_v1_1_0_update.hex'
    Sign update 1.0.0 $key   'resp_v1_0_0_update.hex'          # rollback candidate
    Sign update 1.2.0 $key   'resp_v1_2_0_update.hex'
    Sign update 1.2.0 $rogue 'resp_v1_2_0_rogue_update.hex'    # valid format, unknown ML-DSA-87 key
    Sign update 2.0.0 $key   'resp_v2_0_0_update.hex'
    Flip 'resp_v1_2_0_update.hex' 'resp_v1_2_0_corrupt_update.hex' 0x3204F000   # payload byte (secure code)
    Flip 'resp_v1_2_0_update.hex' 'resp_v1_2_0_header_update.hex'  0x3204E016   # header: revision 0 -> 1
    Flip 'resp_v1_1_0_boot.hex'   'resp_v1_1_0_tampered_boot.hex'  0x3201C000   # primary slot payload byte
    foreach ($f in 'resp_v1_0_0_boot.hex','resp_v1_2_0_update.hex') { py -3.13 -I "$PSScriptRoot\mcuboot_info.py" "$img\$f" }
    Get-ChildItem $img | Format-Table Name, Length | Out-String | Write-Host
    return
}

# ------------------------------------------------------------------ BOARD
if (-not $Hwid) {
    $m = [regex]::Match((& $dfuh --display-hw 2>&1 | Out-String), 'I2C:\s*(\S+)')
    if (-not $m.Success) { throw 'No I2C bridge found.' }
    $Hwid = $m.Groups[1].Value
}
$stamp  = Get-Date -Format 'yyyyMMdd_HHmm'
$outDir = "$hack\results\task4_EPBL_SPDM_$stamp"
New-Item -ItemType Directory -Force $outDir | Out-Null
$csvPath = "$outDir\summary.csv"
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
Start-Sleep -Seconds 2

function Want([string]$id) { return ($Only.Count -eq 0) -or ($Only -contains $id) }
function Mark([string]$t) { Add-Content "$outDir\timeline.txt" ('{0:HH:mm:ss.fff} {1}' -f (Get-Date), $t) }
function Record([string]$name, [string]$expect, [string]$result, [bool]$ok, [double]$wall, [string]$ev) {
    $row = [pscustomobject]@{ test = $name; expect = $expect; result = $result; ok = $ok; wall_s = [math]::Round($wall,1); evidence = $ev }
    $row | Export-Csv -NoTypeInformation -Append -Encoding UTF8 $csvPath
    Write-Host ("{0,-34} expect={1,-8} result={2,-9} {3} {4,6:N1}s  {5}" -f $name, $expect, $result, $(if ($ok) {'OK'} else {'UNEXPECTED'}), $wall, $ev)
}
function UartLines() { if (Test-Path $uartLog) { return @(Get-Content $uartLog) } else { return @() } }

# Load hex files with J-Link (sector erase + program + verify of the touched range only), reset, and
# classify the boot from the EPBL log: BOOTED (app started) / REJECTED (EPBL refused the candidate image
# but booted the old one) / NOBOOT (no valid image -> EPBL stops).
function Boot([string]$name, [string]$expect, [string[]]$hexes, [int]$wait = 10) {
    Mark "START $name"
    $t = [Diagnostics.Stopwatch]::StartNew()
    $n0 = (UartLines).Count
    $cmds = "connect`n" + (($hexes | ForEach-Object { "loadfile $img\$_`n" }) -join '') + "r`ng`nexit`n"
    Set-Content "$env:TEMP\t4_load.jlink" $cmds -Encoding ASCII
    & $jlink -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile "$env:TEMP\t4_load.jlink" 2>&1 |
        Out-File -Encoding utf8 "$outDir\${name}_jlink.txt"
    Start-Sleep -Seconds $wait
    $log = @(UartLines | Select-Object -Skip $n0)
    $log | Set-Content "$outDir\${name}_boot.log"
    $t.Stop()
    $txt = $log -join "`n"
    $started  = $txt -match 'Starting User Application'
    $upgraded = $txt -match 'Image upgrade secondary slot -> primary slot'
    $rej = ($log | Where-Object { $_ -match 'insufficient version|is invalid|returned = 0x(?!742e)|\[ERR\]|not valid' } | Select-Object -First 1)
    $result = if (-not $started) { 'NOBOOT' } elseif ($upgraded) { 'UPDATED' } elseif ($rej) { 'REJECTED' } else { 'BOOTED' }
    $ver = ($log | Where-Object { $_ -match 'Image Version|SPDM|fw \d' } | Select-Object -First 1)
    $ev = if ($rej) { $rej.Trim() } elseif ($ver) { $ver.Trim() } else { ($log | Select-Object -Last 1) }
    Record $name $expect $result ($result -eq $expect) $t.Elapsed.TotalSeconds $ev
}
function Req([string]$name, [string]$expect, [string[]]$a) {
    if ($NoSpdm) { return }
    Mark "START $name"
    $t = [Diagnostics.Stopwatch]::StartNew()
    $o = & $req @a --root $good --hwid $Hwid --dfuh-cli $dfuh --work-dir "$outDir\work_$name" 2>&1 | Out-String
    $t.Stop(); $code = $LASTEXITCODE
    $o | Set-Content "$outDir\$name.txt"
    $res = if ($code -eq 0) { 'PASS' } else { 'REJECT' }
    $ev = ($o -split "`n" | Where-Object { $_ -match 'block 3|error:|exchange: OK' } | Select-Object -First 1)
    $b4 = ($o -split "`n" | Where-Object { $_ -match 'block 4' } | Select-Object -First 1)
    if ($b4 -and $b4 -match '\)\s+([0-9a-f]+)\s*$') {
        $s = -join ($Matches[1] -split '(..)' | Where-Object { $_ } | ForEach-Object { [char][Convert]::ToInt32($_, 16) })
        $ev = "$($ev.Trim()) | block4='$s'"
    }
    Record $name $expect $res ($res -eq $expect) $t.Elapsed.TotalSeconds $(if ($ev) { $ev.Trim() } else { '' })
}

try {
    if (Want 'T0') {
        Boot 'T0_boot_v1_0_0' 'BOOTED' @('epbl_full.hex', 'resp_v1_0_0_boot.hex', 'zero_upgrade.hex') 8
        Req 'T0_measure_v1_0_0' 'PASS' @('measure')
        Req 'T0_exchange_v1_0_0' 'PASS' @('exchange', '--message', 'boot v1.0.0 via EPBL')
    }
    if (Want 'T1') {
        Boot 'T1_valid_update_v1_1_0' 'UPDATED' @('resp_v1_1_0_update.hex') 15
        Req 'T1_measure_v1_1_0' 'PASS' @('measure')
        Req 'T1_exchange_after_update' 'PASS' @('exchange', '--message', 'after update v1.1.0')
    }
    if (Want 'T2') { Boot 'T2_rollback_v1_0_0'      'REJECTED' @('resp_v1_0_0_update.hex') 10 }
    if (Want 'T3') { Boot 'T3_corrupt_payload'      'REJECTED' @('resp_v1_2_0_corrupt_update.hex') 10 }
    if (Want 'T4') { Boot 'T4_modified_header'      'REJECTED' @('resp_v1_2_0_header_update.hex') 10 }
    if (Want 'T5') { Boot 'T5_rogue_key'            'REJECTED' @('resp_v1_2_0_rogue_update.hex') 10 }
    if (Want 'T6') { Req  'T6_measure_still_v1_1_0' 'PASS' @('measure') }
    if (Want 'T7') {
        Boot 'T7_tampered_primary' 'NOBOOT' @('resp_v1_1_0_tampered_boot.hex') 8
        Req  'T7_connect_no_app'   'REJECT' @('connect')
    }
    if (Want 'T8') {
        Boot 'T8_recovery_update_v2_0_0' 'UPDATED' @('resp_v2_0_0_update.hex') 15
        Req  'T8_measure_v2_0_0'         'PASS' @('measure')
        Req  'T8_exchange_after_recovery' 'PASS' @('exchange', '--message', 'recovered v2.0.0')
    }
} finally {
    Start-Sleep -Seconds 1
    Stop-Job $uart; Remove-Job $uart
    if (Test-Path $csvPath) { Import-Csv $csvPath | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object "$outDir\summary.txt" }
    Write-Host "Results in $outDir"
}
