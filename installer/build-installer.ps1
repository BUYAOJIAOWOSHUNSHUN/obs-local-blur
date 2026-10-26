param(
 [Parameter(Mandatory=$true)][string]$PackageDirectory,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$PackageDirectory=(Resolve-Path -LiteralPath $PackageDirectory).Path
New-Item -ItemType Directory -Path $BuildDirectory,$OutputDirectory -Force | Out-Null
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).Path
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if(!(Test-Path -LiteralPath $compiler)){throw '.NET Framework 4.x compiler is required.'}
$zip=Join-Path $BuildDirectory 'payload.zip'
if(Test-Path -LiteralPath $zip){throw 'Use a new build directory to preserve previous payloads.'}
Compress-Archive -LiteralPath "$PackageDirectory/obs-plugins","$PackageDirectory/data" -DestinationPath $zip
$hashes=foreach($sub in @('obs-plugins','data')){foreach($file in Get-ChildItem -LiteralPath "$PackageDirectory/$sub" -Recurse -File){
 $relative=$file.FullName.Substring($PackageDirectory.Length+1).Replace('\','/')
 (Get-FileHash -LiteralPath $file.FullName).Hash.ToLower()+'  '+$relative
}}
$manifest=Join-Path $BuildDirectory 'payload.sha256'
$hashes | Sort-Object | Set-Content -LiteralPath $manifest -Encoding ascii
$license=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\LICENSE'))
$source=Join-Path $PSScriptRoot 'Setup.cs'
$testSource=Join-Path $PSScriptRoot 'InstallerTests.cs'
$applicationManifest=Join-Path $PSScriptRoot 'setup.manifest'
$common=@('/nologo','/optimize+','/debug-','/platform:x64','/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.IO.Compression.dll',"/resource:$zip,payload.zip","/resource:$manifest,payload.sha256","/resource:$license,LICENSE")
$setup=Join-Path $OutputDirectory 'obs-local-blur-1.6.1-local-r5-setup-x64.exe'
& $compiler @common /target:winexe "/win32manifest:$applicationManifest" "/out:$setup" $source
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed.'}
$tests=Join-Path $BuildDirectory 'installer-tests.exe'
& $compiler @common /target:exe /main:InstallerTests "/out:$tests" $source $testSource
if($LASTEXITCODE -ne 0){throw 'Installer test compilation failed.'}
& $tests (Join-Path $BuildDirectory 'dummy-install')
if($LASTEXITCODE -ne 0){throw 'Installer tests failed.'}
Get-FileHash -LiteralPath $setup
