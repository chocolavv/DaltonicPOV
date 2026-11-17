# Builds dichromat.dll into -Out. Run by the host's build.ps1, which gives the MSVC environment.
param([string] $Core = "$PSScriptRoot\..\..\native", [Parameter(Mandatory)] [string] $Out)
$ErrorActionPreference = 'Stop'
Push-Location $Out
try {
  rc /nologo /fo dichromat.res "$PSScriptRoot\dichromat.rc"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /LD /I "$Core" "$PSScriptRoot\dichromat.cpp" dichromat.res `
    /link d3dcompiler.lib user32.lib /OUT:dichromat.dll
  exit $LASTEXITCODE
} finally { Pop-Location }
