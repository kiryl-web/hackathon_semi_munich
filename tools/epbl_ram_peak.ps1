# Measure EPBL peak stack/heap use during a full signature verification (stack painting via J-Link).
# Flashes an image whose signature byte is flipped (verification runs to the end, then fails, so EPBL keeps
# control and the app never overwrites RAM), paints heap+stack with a pattern, lets EPBL run, dumps RAM.
# Usage: epbl_ram_peak.ps1 -Image <neg_sig.hex> -Tag <name>   (EPBL must already be programmed)
param([Parameter(Mandatory)][string]$Image, [Parameter(Mandatory)][string]$Tag,
      [string]$HeapBase = '0x34005DD8', [string]$StackLimit = '0x3400A800', [string]$StackTop = '0x3400E800')
$ErrorActionPreference = 'Continue'
$d = "$env:TEMP\t2paint"; New-Item -ItemType Directory -Force $d | Out-Null
$jl = 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
$hb = [Convert]::ToUInt32($HeapBase, 16); $sl = [Convert]::ToUInt32($StackLimit, 16); $stp = [Convert]::ToUInt32($StackTop, 16)
$len = $stp - $hb
py -3.13 -I -c "import struct; open(r'$d\pat.bin','wb').write(struct.pack('<I',0xA5C3E1F0)*($len//4))"
function JL($c) { Set-Content "$d\c.jlink" $c -Encoding ASCII; & $jl -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -CommandFile "$d\c.jlink" 2>&1 | Out-String }
$null = JL "connect`nloadfile $Image`nr`nh`nloadbin $d\pat.bin,$HeapBase`nexit`n"   # CPU resumes on exit -> EPBL runs
Start-Sleep -Seconds 2
$null = JL ("connect`nh`nsavebin $d\after_$Tag.bin,$HeapBase,0x{0:X}`ng`nexit`n" -f $len)
py -3.13 -I -c @"
import struct
b=open(r'$d\after_$Tag.bin','rb').read(); w=struct.unpack('<%dI'%(len(b)//4),b); P=0xA5C3E1F0
si=($sl-$hb)//4; st=w[si:]; low=next((i for i,x in enumerate(st) if x!=P),len(st))
hp=w[:si]; last=max((i for i,x in enumerate(hp) if x!=P),default=-1)
print('$Tag : peak stack %d B of %d B, heap touched %d B of %d B' % ((len(st)-low)*4, len(st)*4, (last+1)*4, len(hp)*4))
"@
