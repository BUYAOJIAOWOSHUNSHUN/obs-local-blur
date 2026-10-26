param(
    [Parameter(Mandatory=$true)][string]$Dependencies,
    [Parameter(Mandatory=$true)][string]$ObsBin,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$PackageDirectory
)
$ErrorActionPreference='Stop'
$MsvcRoot=Join-Path $Dependencies '10.msvc-bootstrap/msvc'
$QtRoot=Join-Path $Dependencies 'obs-qt6-20260715'
$ObsSource=Join-Path $Dependencies 'obs-studio'
$Toolchain=Join-Path $Dependencies 'llvm-mingw-20261006-ucrt-x86_64'
$vc=(Get-ChildItem -LiteralPath (Join-Path $MsvcRoot 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk=Join-Path $MsvcRoot 'Windows Kits/10'
$sdkVersion=(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$savedPath=$env:PATH;$savedInclude=$env:INCLUDE;$savedLib=$env:LIB
New-Item -ItemType Directory -Path $BuildDirectory,"$BuildDirectory/generated","$PackageDirectory/obs-plugins/64bit","$PackageDirectory/data/obs-plugins/obs-composite-blur" -Force | Out-Null
@'
#pragma once
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins"
#define OBS_INSTALL_PREFIX ""
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
'@ | Set-Content -LiteralPath "$BuildDirectory/generated/obsconfig.h" -Encoding utf8
$exports=& "$Toolchain/bin/llvm-readobj.exe" --coff-exports "$ObsBin/obs.dll"
if($LASTEXITCODE -ne 0){throw 'Read OBS exports failed'}
$names=@($exports|ForEach-Object {if($_ -match '^\s+Name: (\S+)$'){$Matches[1]}})
@('LIBRARY obs.dll','EXPORTS')+$names | Set-Content -LiteralPath "$BuildDirectory/obs.def" -Encoding ascii
try {
    $env:PATH="$vc/bin/Hostx64/x64;"+$env:PATH
    $env:INCLUDE="$vc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um"
    $env:LIB="$vc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    & "$vc/bin/Hostx64/x64/lib.exe" /nologo "/DEF:$BuildDirectory/obs.def" /MACHINE:X64 "/OUT:$BuildDirectory/obs.lib"
    if($LASTEXITCODE -ne 0){throw 'OBS import library failed'}
    $common=@('/nologo','/O2','/W3','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS',"/I$BuildDirectory/generated","/I$ObsSource/libobs","/I$ObsSource/deps")
    $cFiles=@('obs-composite-blur-plugin.c','obs-composite-blur-filter.c','obs-utils.c','blur/gaussian-kernel.c','blur/gaussian.c','blur/box.c','blur/pixelate.c','blur/dual_kawase.c','blur/temporal.c')
    $objects=@()
    foreach($name in $cFiles){
        $obj=Join-Path $BuildDirectory (($name -replace '[/.]','_')+'.obj')
        & "$vc/bin/Hostx64/x64/cl.exe" @common /std:c17 /c "$PSScriptRoot/src/$name" "/Fo$obj"
        if($LASTEXITCODE -ne 0){throw "C compilation failed: $name"};$objects+=$obj
    }
    $cppObj="$BuildDirectory/crop-ui.obj"
    & "$vc/bin/Hostx64/x64/cl.exe" @common /std:c++17 /EHsc /Zc:__cplusplus /permissive- /DQT_NO_DEBUG /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB "/I$QtRoot/include" "/I$QtRoot/include/QtCore" "/I$QtRoot/include/QtGui" "/I$QtRoot/include/QtWidgets" /c "$PSScriptRoot/src/crop-ui.cpp" "/Fo$cppObj"
    if($LASTEXITCODE -ne 0){throw 'Crop UI compilation failed'};$objects+=$cppObj
    @'
#include <windows.h>
1 VERSIONINFO
 FILEVERSION 1,6,1,0
 PRODUCTVERSION 1,6,1,0
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_DLL
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileDescription", "Composite Blur 1.6.1 - Local Visual Crop Modification\0"
   VALUE "FileVersion", "1.6.1\0"
   VALUE "ProductName", "Composite Blur - Local Visual Crop\0"
   VALUE "ProductVersion", "1.6.1\0"
   VALUE "OriginalFilename", "obs-composite-blur.dll\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END
'@ | Set-Content -LiteralPath "$BuildDirectory/version.rc" -Encoding ascii
    & "$sdk/bin/$sdkVersion/x64/rc.exe" /nologo "/fo$BuildDirectory/version.res" "$BuildDirectory/version.rc"
    if($LASTEXITCODE -ne 0){throw 'Version resource failed'}
    & "$vc/bin/Hostx64/x64/link.exe" /nologo /DLL /DYNAMICBASE /NXCOMPAT @objects "$BuildDirectory/version.res" "$BuildDirectory/obs.lib" "/LIBPATH:$QtRoot/lib" Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib "/OUT:$PackageDirectory/obs-plugins/64bit/obs-composite-blur.dll" "/IMPLIB:$BuildDirectory/composite-blur.lib" /EXPORT:cb_crop_ui_start /EXPORT:cb_crop_ui_stop
    if($LASTEXITCODE -ne 0){throw 'Plugin link failed'}
    Copy-Item -Path "$PSScriptRoot/data/*" -Destination "$PackageDirectory/data/obs-plugins/obs-composite-blur" -Recurse -Force
    Get-FileHash -LiteralPath "$PackageDirectory/obs-plugins/64bit/obs-composite-blur.dll"
}finally{$env:PATH=$savedPath;$env:INCLUDE=$savedInclude;$env:LIB=$savedLib}
