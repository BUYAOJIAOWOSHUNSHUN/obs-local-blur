param(
 [Parameter(Mandatory=$true)][string]$Dependencies,
 [Parameter(Mandatory=$true)][string]$ObsRoot,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$PackageDirectory,
 [Parameter(Mandatory=$true)][string]$ReviewDirectory
)
$ErrorActionPreference='Stop'
$MsvcRoot=Join-Path $Dependencies '10.msvc-bootstrap/msvc';$QtRoot=Join-Path $Dependencies 'obs-qt6-20260715';$ObsSource=Join-Path $Dependencies 'obs-studio'
$vc=(Get-ChildItem -LiteralPath "$MsvcRoot/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk=Join-Path $MsvcRoot 'Windows Kits/10';$sdkVersion=(Get-ChildItem -LiteralPath "$sdk/Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$saved=@{};foreach($name in @('PATH','INCLUDE','LIB','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_SCALE_FACTOR')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
New-Item -ItemType Directory -Path $ReviewDirectory -Force | Out-Null
try{
 $env:PATH="$ObsRoot/bin/64bit;$vc/bin/Hostx64/x64;"+$env:PATH
 $env:INCLUDE="$vc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um"
 $env:LIB="$vc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
 $exe="$BuildDirectory/visual-crop-smoke.exe"
 & "$vc/bin/Hostx64/x64/cl.exe" /nologo /O2 /W4 /WX /MD /utf-8 /std:c++17 /EHsc /Zc:__cplusplus /permissive- /DQT_NO_DEBUG /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB "/I$QtRoot/include" "/I$QtRoot/include/QtCore" "/I$QtRoot/include/QtGui" "/I$QtRoot/include/QtWidgets" "/I$BuildDirectory/generated" "/I$ObsSource/libobs" "/I$ObsSource/deps" "$PSScriptRoot/visual-crop-smoke.cpp" "/Fo$BuildDirectory/visual-crop-smoke.obj" /link "/OUT:$exe" "$BuildDirectory/obs.lib" "/LIBPATH:$QtRoot/lib" Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib
 if($LASTEXITCODE -ne 0){throw 'Test compilation failed'}
 $env:QT_QPA_PLATFORM='windows';$env:QT_QPA_PLATFORM_PLUGIN_PATH="$ObsRoot/bin/64bit/platforms"
 & $exe "$ObsRoot/data/libobs/" "$PackageDirectory/obs-plugins/64bit/obs-composite-blur.dll" "$PackageDirectory/data/obs-plugins/obs-composite-blur/" $ReviewDirectory *> "$ReviewDirectory/test.log"
 $code=$LASTEXITCODE
 Get-Content -LiteralPath "$ReviewDirectory/test.log" | Where-Object {$_ -match '^(PASS|FAIL|RESULT)'}
 if($code -ne 0){Get-Content -LiteralPath "$ReviewDirectory/test.log" -Tail 30;throw "Tests failed: $code"}
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
