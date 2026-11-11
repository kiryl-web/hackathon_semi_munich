# Demo step 3: SPDM 1.4 against the running responder (ML-DSA-87 authentication, ML-KEM-1024 session).
# NOTE: over this 127-byte I2C/DFU tool one operation takes ~6-10 min -> start it BEFORE the talk.
#   -Op measure  -Image resp_v1_1_0_update.hex   signed measurements + reference-value check against that release file
#   -Op exchange                                  full session + encrypted application message
param([ValidateSet('measure','exchange')][string]$Op = 'measure', [string]$Image = 'resp_v1_1_0_update.hex',
      [string]$Message = 'hello from the PQC demo')
. "$PSScriptRoot\demo_common.ps1"
$req  = "$SPDM\psoc-spdm-requester\spdm-requester.exe"
$root = "$Hack\results\task3_neg_roots\good_root.der"
$m = [regex]::Match((& $dfuh --display-hw 2>&1 | Out-String), 'I2C:\s*(\S+)')
if (-not $m.Success) { Write-Host 'No I2C bridge (KitProg3) found - check the USB cable.' -ForegroundColor Red; exit 1 }
$stamp = Get-Date -Format HHmmss
$work = "$Hack\results\task5_demo\spdm_${Op}_$stamp"
New-Item -ItemType Directory -Force $work | Out-Null
Banner "3  SPDM 1.4 $Op  (ML-DSA-87 identity, pinned ML-DSA-87 root$(if ($Op -eq 'exchange') {', ML-KEM-1024 session, AES-256-GCM'}))"
Write-Host "  started $(Get-Date -Format HH:mm:ss) - takes ~6-10 min over the 127-byte I2C tunnel" -ForegroundColor DarkGray
$a = @($Op, '--root', $root, '--hwid', $m.Groups[1].Value, '--dfuh-cli', $dfuh, '--work-dir', "$work\work")
if ($Op -eq 'exchange') { $a += @('--message', $Message) }
$t = [Diagnostics.Stopwatch]::StartNew()
$out = & $req @a 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$out | Set-Content "$work\$Op.txt"
foreach ($l in $out) {
    if ($l -match '^\s*$|CategoryInfo|FullyQualified|At line|~~~' -or $l -cmatch '\bINFO\b') { continue }
    Write-Host "    $l" -ForegroundColor $(if ($l -match 'error|FAIL') {'Red'} elseif ($l -match 'OK|PASS') {'Green'} else {'White'})
}
Write-Host ("  {0} after {1:N0} s" -f $(if ($code -eq 0) {'PASS'} else {"FAILED (exit $code)"}), $t.Elapsed.TotalSeconds) -ForegroundColor $(if ($code -eq 0) {'Green'} else {'Red'})
if ($Op -eq 'measure' -and $code -eq 0) {
    Write-Host "  reference values recomputed from the release files (EPBL + $Image):" -ForegroundColor Cyan
    py -3.13 -I "$Hack\tools\task4_verify_meas.py" "$Img\epbl_full.hex" "$Img\$Image" "$work\$Op.txt" |
        ForEach-Object { Write-Host "    $_" -ForegroundColor $(if ($_ -match 'MISMATCH') {'Red'} elseif ($_ -match 'MATCH') {'Green'} else {'White'}) }
}
