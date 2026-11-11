# Dot-source this in PowerShell before running ModusToolbox make:  . C:\Users\kpato\KP\Hackathon\tools\mtbenv.ps1
# SPDM workspace (renamed from "PSOC_C3_SPDM (2)" so the path has no spaces/parentheses).
$global:SPDM = 'C:/Users/kpato/KP/Hackathon/PSOC_C3_SPDM_v2/PSOC_C3_SPDM/SPDM'
$env:CY_TOOLS_PATHS = 'C:/Users/kpato/ModusToolbox/tools_3.9'
$env:HOME = $env:USERPROFILE.Replace('\', '/')
$env:PATH = 'C:/Program Files/CMake/bin;C:/Users/kpato/ModusToolbox/tools_3.9/modus-shell/bin;C:/Users/kpato/Infineon/Tools/ModusToolbox-Edge-Protect-Security-Suite-2.4.0/tools/edgeprotecttools/bin;' + $env:PATH
$global:make = 'C:/Users/kpato/ModusToolbox/tools_3.9/modus-shell/bin/make.exe'
$global:dfuh = 'C:\Users\kpato\ModusToolbox\tools_3.9\dfuh-tool\dfuh-cli.exe'
