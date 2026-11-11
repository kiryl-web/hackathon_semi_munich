# Demo backup: show the recorded hardware evidence (instant) - SPDM after the update + the SPDM tamper campaign.
. "$PSScriptRoot\demo_common.ps1"
$run = Get-ChildItem "$Hack\results" -Directory -Filter 'task4_EPBL_SPDM_*' | Where-Object { Test-Path "$($_.FullName)\summary.csv" } | Select-Object -Last 1
Banner "RECORDED: responder behind EPBL, $($run.Name)"
Import-Csv "$($run.FullName)\summary.csv" | ForEach-Object {
    $c = if ($_.ok -ne 'True') { 'Red' } elseif ($_.result -in 'REJECTED','NOBOOT','REJECT') { 'Yellow' } else { 'Green' }
    Write-Host ("    {0,-30} {1,-9} {2}" -f $_.test, $_.result, ($_.evidence -replace '^\d\d:\d\d:\d\d\.\d+ ', '').Substring(0, [Math]::Min(70, $_.evidence.Length))) -ForegroundColor $c
}
Write-Host ''
Write-Host '  measurements after the update to v1.1.0 vs release file:' -ForegroundColor Cyan
py -3.13 -I "$Hack\tools\task4_verify_meas.py" "$Img\epbl_full.hex" "$Img\resp_v1_1_0_update.hex" "$($run.FullName)\T1_measure_v1_1_0.txt" |
    ForEach-Object { Write-Host "    $_" -ForegroundColor $(if ($_ -match 'MISMATCH') {'Red'} elseif ($_ -match 'MATCH') {'Green'} else {'White'}) }
$neg = "$Hack\results\task3_NEG_P4_20261111_1533\summary.txt"
Banner 'RECORDED: SPDM tamper campaign (Task 3), 12/12 rejected'
Get-Content $neg | Where-Object { $_ -match 'expect=' } | ForEach-Object {
    Write-Host "    $_" -ForegroundColor $(if ($_ -match 'ok=True') { if ($_ -match 'REJECT') {'Yellow'} else {'Green'} } else {'Red'})
}
