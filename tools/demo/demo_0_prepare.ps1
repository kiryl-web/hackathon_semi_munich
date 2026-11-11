# Demo step 0 (run BEFORE the talk, ~1 min): known state = EPBL (ML-DSA-87) + responder v1.0.0, empty upgrade slot.
# The SPDM identity in the PAS is not touched.
. "$PSScriptRoot\demo_common.ps1"
Banner 'PREPARE: EPBL + SPDM responder v1.0.0 (signed ML-DSA-87), empty upgrade slot'
Invoke-BoardBoot @('epbl_full.hex', 'resp_v1_0_0_boot.hex', 'zero_upgrade.hex') 8
