# Shared helpers for the Task 5 live demo (dot-sourced by the demo_*.ps1 scripts).
$ErrorActionPreference = 'Continue'
$Hack  = 'C:\Users\kpato\KP\Hackathon'
$Img   = "$Hack\results\task4_boot"
$JLink = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$Com   = 'COM4'
. "$Hack\tools\mtbenv.ps1" *> $null

function Banner([string]$t) {
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host "  $t" -ForegroundColor Cyan
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
}

# Load hex files with J-Link and reset, while the board console (COM4) is captured from the first byte.
# Prints only the lines that tell the story, coloured.
function Invoke-BoardBoot([string[]]$Hexes, [int]$Seconds = 8) {
    $cmds = "connect`n" + (($Hexes | ForEach-Object { "loadfile $Img\$_`n" }) -join '') + "r`ng`nexit`n"
    Set-Content "$env:TEMP\demo_load.jlink" $cmds -Encoding ASCII
    $p = New-Object System.IO.Ports.SerialPort $Com, 115200, 'None', 8, 'One'
    $p.ReadTimeout = 200; $p.ReadBufferSize = 1048576
    $p.Open(); $p.DiscardInBuffer()
    if ($Hexes.Count) { Write-Host "  programming: $($Hexes -join ', ')" -ForegroundColor DarkGray }
    $jp = Start-Process -FilePath $JLink -NoNewWindow -PassThru -RedirectStandardOutput "$env:TEMP\demo_jlink.out" `
          -ArgumentList '-device','PSC3M6GES3AH','-if','SWD','-speed','4000','-autoconnect','1','-NoGui','1','-ExitOnError','1','-CommandFile',"$env:TEMP\demo_load.jlink"
    $jp.WaitForExit(120000) | Out-Null
    Write-Host '  reset -> board console:' -ForegroundColor DarkGray
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $all = New-Object System.Collections.Generic.List[string]
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        try { $l = $p.ReadLine().TrimEnd() } catch [System.TimeoutException] { continue }
        $all.Add($l)
        $c = $null
        if ($l -match 'not valid|insufficient version|\[ERR\]|handle_error') { $c = 'Red' }
        elseif ($l -match 'validated successfully|Starting User Application|Image upgrade secondary|Copying the secondary') { $c = 'Green' }
        elseif ($l -match 'bootutil_img_validate|MCUBoot Bootloader Started|Erasing the primary') { $c = 'Gray' }
        elseif ($l -match 'SPDM Responder|cert chain loaded|SPDM responder initialized|waiting for host') { $c = 'White' }
        if ($c) { Write-Host "    $($l -replace '^\s*\[[^\]]*\]', '')" -ForegroundColor $c }
    }
    $p.Close()
    $txt = $all -join "`n"
    if ($txt -notmatch 'Starting User Application') {
        Write-Host '  RESULT: EPBL refused to start any image (no valid signed image) - device stays silent' -ForegroundColor Red
    } elseif ($txt -match 'Image upgrade secondary slot -> primary slot') {
        Write-Host '  RESULT: signed update verified (ML-DSA-87) and INSTALLED, responder started' -ForegroundColor Green
    } elseif ($txt -match 'not valid|insufficient version') {
        Write-Host '  RESULT: candidate update REJECTED by EPBL - previous verified image keeps running' -ForegroundColor Yellow
    } else {
        Write-Host '  RESULT: image verified with ML-DSA-87, responder started' -ForegroundColor Green
    }
    $all | Set-Content "$Hack\results\task5_demo_last_boot.log"
}

function Show-ImageVersion([string]$hex) {
    $info = (py -3.13 -I "$Hack\tools\mcuboot_info.py" "$Img\$hex" 2>&1 | Select-String 'version').ToString()
    $ver = [regex]::Match($info, '\d+\.\d+\.\d+\+\d+').Value
    Write-Host "  image: $hex  (version $ver, signed ML-DSA-87 / SHA-512)" -ForegroundColor DarkGray
}
