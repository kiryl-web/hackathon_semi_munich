# Demo step 2: write a candidate image into the upgrade slot and reset; EPBL decides.
#   -What rogue    update signed with an UNKNOWN ML-DSA-87 key          -> rejected
#   -What corrupt  genuine signature, one payload byte changed          -> rejected
#   -What header   genuine signature, version field in header changed   -> rejected
#   -What valid    genuine v1.1.0                                       -> installed
#   -What rollback genuine but OLDER v1.0.0 (run after 'valid')         -> rejected
#   -What v2       genuine v2.0.0                                       -> installed
param([Parameter(Mandatory)][ValidateSet('rogue','corrupt','header','valid','rollback','v2')][string]$What)
. "$PSScriptRoot\demo_common.ps1"
$map = @{
    rogue    = @('resp_v1_2_0_rogue_update.hex',   'TAMPER: update v1.2.0 signed with a rogue ML-DSA-87 key');
    corrupt  = @('resp_v1_2_0_corrupt_update.hex', 'TAMPER: genuine v1.2.0 update with one byte of code changed');
    header   = @('resp_v1_2_0_header_update.hex',  'TAMPER: genuine v1.2.0 update with the header version edited');
    valid    = @('resp_v1_1_0_update.hex',         'VALID UPDATE: genuine signed v1.1.0');
    rollback = @('resp_v1_0_0_update.hex',         'ROLLBACK: genuine signed but OLDER v1.0.0');
    v2       = @('resp_v2_0_0_update.hex',         'VALID UPDATE: genuine signed v2.0.0')
}
$hex, $title = $map[$What]
Banner "2  $title"
Show-ImageVersion $hex
Invoke-BoardBoot @($hex) $(if ($What -in 'valid','v2') { 12 } else { 8 })
