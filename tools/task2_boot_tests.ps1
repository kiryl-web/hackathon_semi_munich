# Task 2 – PQC boot tests: EPBL (ML-DSA-87 / HashML-DSA SHA-512) validating CE243041 images.
# Prerequisite: EPBL built from feature_config_ML-DSA-87.json is programmed (make build + make qprogram),
# images built with boot_tests.ps1 -KeyType ML-DSA-87 -Sha sha512 -NoFlash (see TASK2_NOTES.md).
# Output: results\task2_boot\  (logs, tampered images, readbacks, summary_ML-DSA-87*.txt)
# -Only N10,N11 re-runs single tests (P2 restore always runs before N10/P3).
param(
    [string]$Prefix = 'ML-DSA-87',
    [int]$Seconds = 8,
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Continue'
$R     = 'C:\Users\kpato\KP\Hackathon\results'
$res   = "$R\task2_boot"
New-Item -ItemType Directory -Force $res | Out-Null
$jlink = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$tools = 'C:\Users\kpato\KP\Hackathon\tools'

$Good     = "$R\img_${Prefix}_v1_0_boot.hex"
$Rogue    = "$R\img_${Prefix}_rogue_key_boot.hex"
$Sha256   = "$R\img_${Prefix}_v1_0_sha256_boot.hex"
$Ecdsa    = "$R\img_ECDSA-384_v1_0_boot.hex"
$Mldsa65  = "$R\img_ML-DSA-65_v1_0_unsupported_boot.hex"   # parameter set not supported by this EPBL
$Rollback = "$R\img_${Prefix}_v0_9_rollback_update.hex"
$Update   = "$R\img_${Prefix}_v2_0_valid_update_update.hex"

function Run([string]$id) { ($Only.Count -eq 0) -or ($Only -contains $id) }
function JL([string]$cmds, [string]$saveTo = '') {
    $f = "$env:TEMP\jl_cmd.jlink"
    Set-Content $f $cmds -Encoding ASCII
    $o = & $jlink -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile $f 2>&1 | Out-String
    if ($saveTo) { $o | Set-Content $saveTo }
    return $o
}
function Flash([string]$hex) { $null = JL "connect`nloadfile $hex`nexit`n" }
function Boot([string]$id, [string]$tag, [string]$expect) {
    $log = "$res\boot_${Prefix}_${id}_$tag.log"
    & "$tools\boot_capture.ps1" -Out $log -Seconds $Seconds | Out-Null
    $l = Get-Content $log
    $verdict = if ($l -match 'not valid|handle_error|Unable to find bootable|insufficient version') { 'REJECTED' }
               elseif ($l -match 'validated successfully') { 'BOOTED' } else { 'UNKNOWN' }
    $ver = ($l | Select-String 'Image Version : (\S+)' | Select-Object -Last 1)
    $why = ($l | Select-String 'TLV_SHA is invalid|insufficient version|expected = \S+, returned = \S+|not valid|Copying' |
            Select-Object -First 2 | ForEach-Object { ($_.Line -replace '^\s*\S+s\s*\[[^\]]*\]','').Trim() }) -join ' | '
    $ok = if ($verdict -eq $expect) { 'OK  ' } else { 'FAIL' }
    "{0} {1,-4} {2,-22} expect={3,-8} got={4,-8} app={5,-8} {6}" -f $ok, $id, $tag, $expect, $verdict,
        $(if ($ver) { $ver.Matches[0].Groups[1].Value } else { '-' }), $why
}

$summary = @("Task 2 boot tests, EPBL validation = $Prefix (HashML-DSA, SHA-512), $(Get-Date -Format s)")

if (Run 'P1') { Flash $Good; $summary += Boot 'P1' 'authorized_v1_0' 'BOOTED' }

# Tampered copies of the authorized image (addresses from tools/mcuboot_info.py)
$tamper = @(
    @('N1', 'payload_byte',     '3201C000', '01'),   # code corruption
    @('N2', 'header_version',   '3201B014', '01'),   # MCUboot header metadata (version)
    @('N3', 'protected_tlv',    '3202546C', '01'),   # protected TLV area (boot record)
    @('N4', 'keyhash_tlv',      '32025559', '01'),   # key-hash TLV (which key signed)
    @('N5', 'signature_byte',   '32025985', '01'),   # byte inside ML-DSA-87 signature
    @('N6', 'sig_tlv_length',   '3202559B', '10')    # malformed TLV: signature length 4627 -> 4611
)
foreach ($t in $tamper) {
    if (-not (Run $t[0])) { continue }
    $out = "$res\neg_${Prefix}_$($t[0])_$($t[1]).hex"
    py -3.13 -I "$tools\hex_flip.py" $Good $out $t[2] $t[3] | Out-Null
    Flash $out
    $summary += Boot $t[0] $t[1] 'REJECTED'
}
# Whole-image negatives
if (Run 'N7')  { Flash $Rogue;   $summary += Boot 'N7'  'rogue_mldsa87_key'    'REJECTED' }
if (Run 'N8')  { Flash $Ecdsa;   $summary += Boot 'N8'  'ecdsa384_classical'   'REJECTED' }
if (Run 'N9')  { Flash $Sha256;  $summary += Boot 'N9'  'wrong_prehash_sha256' 'REJECTED' }
if (Run 'N11') { Flash $Mldsa65; $summary += Boot 'N11' 'unsupported_mldsa65'  'REJECTED' }

# Restore authorized image (known state for N10 / P3)
Flash $Good; $summary += Boot 'P2' 'restore_v1_0' 'BOOTED'

if (Run 'N10') {
    # Load + readback + reset in ONE J-Link session while the serial port is open, so the first boot that
    # sees v0.9 is captured completely. Expected: 'insufficient version in secondary slot', upgrade slot
    # erased, v1.0 keeps running.
    $log = "$res\boot_${Prefix}_N10_rollback_v0_9.log"
    & "$tools\boot_capture.ps1" -Out $log -Seconds 14 -JLinkOut "$res\N10_rollback_readback_BEFORE.txt" `
        -JLinkCmds "connect`nloadfile $Rollback`nmem32 0x32040000 8`nmem32 0x3201B000 8`nr`ng`nexit`n" | Out-Null
    $l = Get-Content $log
    $v = if (($l -match 'insufficient version') -and ($l -match 'Image Version : 1\.0\.0')) { 'OK   N10  rollback_v0_9           expect=REJECTED got=REJECTED (v1.0 retained)' } else { 'FAIL N10  rollback_v0_9           see log' }
    $summary += "$v  " + (($l | Select-String 'insufficient version' | Select-Object -First 1).Line -replace '^\s*\S+s\s*','')
    $null = JL "connect`nh`nmem32 0x32040000 8`nmem32 0x3201B000 8`ng`nexit`n" "$res\N10_rollback_readback_AFTER.txt"
}

if (Run 'P3') {
    $null = JL "connect`nloadfile $Update`nmem32 0x32040000 8`nmem32 0x3201B000 8`nexit`n" "$res\P3_update_readback_BEFORE.txt"
    $summary += Boot 'P3' 'valid_update_v2_0' 'BOOTED'
    $null = JL "connect`nh`nmem32 0x32040000 8`nmem32 0x3201B000 8`ng`nexit`n" "$res\P3_update_readback_AFTER.txt"
    $summary += Boot 'P4' 'reboot_after_update' 'BOOTED'
}

foreach ($f in 'N10_rollback_readback_BEFORE','N10_rollback_readback_AFTER','P3_update_readback_BEFORE','P3_update_readback_AFTER') {
    if (-not (Test-Path "$res\$f.txt")) { continue }
    $m = Get-Content "$res\$f.txt" | Select-String '^3204|^3201'
    $summary += "$f : " + (($m | ForEach-Object { $_.Line.Trim() }) -join '  ||  ')
}
$outFile = if ($Only.Count) { "$res\summary_${Prefix}_rerun_$($Only -join '_').txt" } else { "$res\summary_${Prefix}.txt" }
$summary | Tee-Object $outFile
