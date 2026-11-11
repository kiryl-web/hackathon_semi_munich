# Task 1 gap-closing boot tests (ECDSA-P384 baseline, reusable for ML-DSA-87 with -Good/-Rollback):
#  N-HDR : flip image version byte in MCUboot header
#  N-PTLV: flip a byte in the protected TLV area
#  N-SIG : flip a byte inside the signature TLV
#  restore good image, then rollback test with flash readback saved before/after boot.
param(
    [string]$Good     = 'C:\Users\kpato\KP\Hackathon\results\img_ECDSA-384_v1_0_boot.hex',
    [string]$Rollback = 'C:\Users\kpato\KP\Hackathon\results\img_ECDSA-384_v0_9_rollback_update.hex',
    [string]$Prefix   = 'ECDSA-384'
)
$ErrorActionPreference = 'Continue'
$res   = 'C:\Users\kpato\KP\Hackathon\results\boot_evidence'
New-Item -ItemType Directory -Force $res | Out-Null
$jlink = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$tools = 'C:\Users\kpato\KP\Hackathon\tools'

function JL([string]$cmds, [string]$saveTo = '') {
    $f = "$env:TEMP\jl_cmd.jlink"
    Set-Content $f $cmds -Encoding ASCII
    $o = & $jlink -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile $f 2>&1 | Out-String
    if ($saveTo) { $o | Set-Content $saveTo }
    return $o
}
function Flash([string]$hex) { $null = JL "connect`nloadfile $hex`nexit`n" }
function Boot([string]$tag) {
    & "$tools\boot_capture.ps1" -Out "$res\boot_${Prefix}_$tag.log" -Seconds 8 | Out-Null
    $l = Get-Content "$res\boot_${Prefix}_$tag.log"
    $verdict = if ($l -match 'not valid|handle_error|Unable to find bootable') { 'REJECTED' }
               elseif ($l -match 'validated successfully') { 'BOOTED' } else { 'UNKNOWN' }
    $ver = ($l | Select-String 'Image Version : (\S+)' | Select-Object -Last 1)
    $why = ($l | Select-String 'TLV_SHA is invalid|expected = \S+, returned = \S+|not valid' | Select-Object -First 2 | ForEach-Object { ($_.Line -replace '^\s*\S+s\s*\[[^\]]*\]','').Trim() }) -join ' | '
    "{0,-22} {1,-9} {2} {3}" -f $tag, $verdict, $(if ($ver) { $ver.Matches[0].Groups[1].Value } else { '' }), $why
}

$summary = @()
if ($env:ROLLBACK_ONLY -ne '1') {
foreach ($t in @(@('N-HDR_version', '3201B014'), @('N-PTLV_meta', '3202546C'), @('N-SIG_signature', '32025585'))) {
    $out = "$res\neg_${Prefix}_$($t[0]).hex"
    py -3.13 -I "$tools\hex_flip.py" $Good $out $t[1] | Out-Null
    Flash $out
    $summary += Boot $t[0]
}

Flash $Good
$summary += Boot 'restore_good'
}   # set $env:ROLLBACK_ONLY=1 to run only the rollback part below

# Rollback with readback evidence
# load + readback in the SAME J-Link session (a separate session after load cannot read flash)
# Load, readback and reset run in ONE J-Link session while the serial port is already open, so the
# first boot that sees the old image is captured completely ('insufficient version in secondary slot').
$log = "$res\boot_${Prefix}_rollback_attempt.log"
& "$tools\boot_capture.ps1" -Out $log -Seconds 14 -JLinkOut "$res\rollback_${Prefix}_readback_BEFORE_boot.txt" `
    -JLinkCmds "connect`nloadfile $Rollback`nmem32 0x32040000 8`nmem32 0x3201B000 8`nr`ng`nexit`n" | Out-Null
$l = Get-Content $log
$verdict = if (($l -match 'insufficient version') -and ($l -match 'Image Version : 1\.0\.0')) { 'REJECTED (v1.0 retained)' } else { 'CHECK LOG' }
$summary += "{0,-22} {1} {2}" -f 'rollback_attempt', $verdict, (($l | Select-String 'insufficient version' | Select-Object -First 1).Line -replace '^\s*\S+s\s*','')
$null = JL "connect`nh`nmem32 0x32040000 8`nmem32 0x3201B000 8`ng`nexit`n" "$res\rollback_${Prefix}_readback_AFTER_boot.txt"
foreach ($w in 'BEFORE', 'AFTER') {
    $m = Get-Content "$res\rollback_${Prefix}_readback_${w}_boot.txt" | Select-String '^3204|^3201'
    $summary += "readback $w boot: " + (($m | ForEach-Object { $_.Line.Trim() }) -join '  ||  ')
}
$summary | Tee-Object "$res\summary_${Prefix}.txt"
