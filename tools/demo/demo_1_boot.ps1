# Demo step 1: verified boot. Reset only - EPBL verifies the responder with ML-DSA-87 and starts it.
. "$PSScriptRoot\demo_common.ps1"
Banner '1  VERIFIED BOOT: reset -> EPBL checks the ML-DSA-87 signature -> SPDM responder starts'
Invoke-BoardBoot @() 6
