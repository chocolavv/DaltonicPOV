# Builds DaltonicPOV.exe into -Out (bin by default) and every plugin (plugins\<name>\build.ps1) into
# its plugins folder.
# Needs Visual Studio or its Build Tools with the C++ workload.
param([string] $Out = "$PSScriptRoot\bin")
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
$out = $Out
New-Item -ItemType Directory -Force $out, "$out\plugins" | Out-Null
Push-Location $out
try {
  rc /nologo /i "$n" /fo app.res "$n\app.rc"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  # uiAccess puts the topmost overlay in z-band 2, which draws above the taskbar and its thumbnails
  # (band 6 while the cursor rests on a thumbnail); without it the overlay stays in band 1, under them.
  cl /nologo /std:c++17 /EHsc /O2 /MT /W3 "$n\app_windows.cpp" "$n\filter_windows.cpp" "$n\icon_windows.cpp" app.res `
    /link d3d11.lib dxgi.lib dwmapi.lib d2d1.lib windowscodecs.lib shlwapi.lib ole32.lib user32.lib gdi32.lib advapi32.lib shell32.lib /SUBSYSTEM:WINDOWS `
    /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='true'" /OUT:DaltonicPOV.exe
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
} finally { Pop-Location }

foreach ($p in Get-ChildItem "$PSScriptRoot\plugins\*\build.ps1") {
  & $p.FullName -Core $n -Out "$out\plugins"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
