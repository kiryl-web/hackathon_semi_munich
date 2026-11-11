# Reset the board via J-Link and capture the boot log from the J-Link VCOM (COM4).
# Usage: powershell -ExecutionPolicy Bypass -File boot_capture.ps1 -Out <logfile> [-Seconds 8]
# -JLinkCmds lets the caller run its own J-Link script while the serial port is already open
# (e.g. "connect / loadfile <old image> / mem32 readback / r / g") so the very first boot after a
# load is captured completely; -JLinkOut saves that J-Link session's output (readback evidence).
param([Parameter(Mandatory=$true)][string]$Out, [int]$Seconds = 8, [string]$Com = 'COM4',
      [string]$JLinkCmds = "connect`nr`ng`nexit`n", [string]$JLinkOut = '')
$jlink = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$cmd = "$env:TEMP\jl_reset.jlink"
Set-Content $cmd $JLinkCmds -Encoding ASCII
$p = New-Object System.IO.Ports.SerialPort $Com, 115200, 'None', 8, 'One'
$p.ReadTimeout = 200
$p.ReadBufferSize = 1048576   # verbose boot logs arrive while J-Link is still busy
$p.Open()
$p.DiscardInBuffer()
# Run J-Link asynchronously so the VCOM is drained continuously from reset on
# (the J-Link OB VCOM FIFO is small and drops data if the host is not reading).
$jp = Start-Process -FilePath $jlink -NoNewWindow -PassThru -RedirectStandardOutput "$env:TEMP\jl_reset.out" `
      -ArgumentList '-device','PSC3M6GES3AH','-if','SWD','-speed','4000','-autoconnect','1','-NoGui','1','-ExitOnError','1','-CommandFile',$cmd
$sw = [Diagnostics.Stopwatch]::StartNew()
$lines = New-Object System.Collections.Generic.List[string]
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    try { $lines.Add(('{0,7:N3}s {1}' -f $sw.Elapsed.TotalSeconds, $p.ReadLine().TrimEnd())) } catch [System.TimeoutException] {}
}
$p.Close()
$lines | Set-Content $Out
if ($JLinkOut) { $jp.WaitForExit(30000) | Out-Null; Copy-Item "$env:TEMP\jl_reset.out" $JLinkOut -Force }
$lines
