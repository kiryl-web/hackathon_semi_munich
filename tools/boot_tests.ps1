# Build/flash helper for the EPBL boot tests (Task 1 ECC baseline / Task 2 PQC).
# Usage examples:
#   boot_tests.ps1 -Kind boot   -Major 1 -Minor 0 -Tag v1_0         # good primary image
#   boot_tests.ps1 -Kind update -Major 0 -Minor 9 -Tag v0_9         # rollback attempt (upgrade slot)
#   boot_tests.ps1 -Kind update -Major 2 -Minor 0 -Tag v2_0         # valid update
param(
    [ValidateSet('boot','update')][string]$Kind = 'boot',
    [int]$Major = 1, [int]$Minor = 0, [int]$Build = 0,
    [string]$Tag = 'img',
    [string]$KeyType = 'ECDSA-384',
    [string]$Key = '../keys/ecdsa_p384_img_sign_key_priv.der',
    [string]$Sha = '',
    [int]$Seconds = 8,
    [switch]$NoFlash
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\mtbenv.ps1"
$app  = 'C:/Users/kpato/KP/Hackathon/mtb-example-ce243041-secure-boot-and-update-with-epb-master'
$res  = 'C:\Users\kpato\KP\Hackathon\results'
$imgType = if ($Kind -eq 'boot') { 'BOOT' } else { 'UPDATE' }
$suffix  = if ($Kind -eq 'boot') { '_boot' } else { '_update' }

& $make -C $app clean *> $null
& $make -C $app build -j8 "IMG_TYPE=$imgType" "IMG_VER_MAJOR=$Major" "IMG_VER_MINOR=$Minor" "IMG_REVISION=0" "IMG_BUILD_NO=$Build" `
    "IMAGE_SIGNING_KEY_TYPE=$KeyType" "IMAGE_SIGNING_KEY=$Key" "IMAGE_SHA=$Sha" *> "$res\epbapp_build_$Tag.log"
if ($LASTEXITCODE -ne 0) { throw "build failed, see $res\epbapp_build_$Tag.log" }
$hex = "$app/build/last_config/mtb-example-ce243041-secure-boot-and-update-with-epb$suffix.hex"
$keep = "$res\img_${KeyType}_$Tag$suffix.hex"
Copy-Item $hex $keep -Force
Write-Host "built $Kind v$Major.$Minor.0+$Build ($KeyType $Sha) -> $keep"
if ($NoFlash) { return }

$jl = "$env:TEMP\jl_load.jlink"
Set-Content $jl "connect`nloadfile $hex`nexit`n" -Encoding ASCII
& 'C:\Program Files\SEGGER\JLink_V976a\JLink.exe' -device PSC3M6GES3AH -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile $jl |
    Select-String 'Flash download: Bank|ERROR' | ForEach-Object { $_.Line }

& "$PSScriptRoot\boot_capture.ps1" -Out "$res\boot_${KeyType}_$Tag.log" -Seconds $Seconds |
    Select-String 'verif|valid|version|downgrade|Swap type|swap_type|Secondary|copy|erase|ERR|Starting|Running|handle|App version|Image version' |
    ForEach-Object { $_.Line }
