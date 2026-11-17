# Builds bin\DaltonicPOV.exe and every plugin (plugins\<name>\build.ps1) into bin\plugins.
# Needs Visual Studio or its Build Tools with the C++ workload.
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with C++ tools found' }
# Take the x64 build environment of vcvars64.bat into this process; it looks for vswhere on PATH.
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

$n = "$PSScriptRoot\native"
$out = "$PSScriptRoot\bin"
New-Item -ItemType Directory -Force $out, "$out\plugins" | Out-Null
Push-Location $out
try {
  rc /nologo /fo app.res "$n\app.rc"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  cl /nologo /std:c++17 /EHsc /O2 /MT /W3 "$n\app_windows.cpp" "$n\filter_windows.cpp" app.res `
    /link d3d11.lib dxgi.lib user32.lib advapi32.lib shell32.lib /SUBSYSTEM:WINDOWS /OUT:DaltonicPOV.exe
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
} finally { Pop-Location }

foreach ($p in Get-ChildItem "$PSScriptRoot\plugins\*\build.ps1") {
  & $p.FullName -Core $n -Out "$out\plugins"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
