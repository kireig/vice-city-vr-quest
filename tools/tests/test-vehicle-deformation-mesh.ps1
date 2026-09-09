param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path $StageRoot 'checks-vehicle-deformation-mesh')
)
$ErrorActionPreference='Stop'
$header=Join-Path $StageRoot 'src/vehicles/VehicleDeformationMesh.h'
if(-not(Test-Path -LiteralPath $header)){$header=Join-Path $StageRoot 'overlay/src/vehicles/VehicleDeformationMesh.h'}
$header=[IO.Path]::GetFullPath($header)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-mesh-production.inc'),('#include "'+$header.Replace('\','/')+'"'),[Text.UTF8Encoding]::new($false))
$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path -LiteralPath $OutDir){throw 'Choose a new output directory.'}
[void][IO.Directory]::CreateDirectory($OutDir)
$pf=[Environment]::GetFolderPath('ProgramFilesX86')
$vswhere=Join-Path $pf 'Microsoft Visual Studio/Installer/vswhere.exe'
$install=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if($LASTEXITCODE -ne 0 -or $install.Count -ne 1){throw 'MSVC tools not found.'}
$vc=@(Get-ChildItem -LiteralPath (Join-Path $install[0] 'VC/Tools/MSVC') -Directory|Sort-Object {[version]$_.Name} -Descending)[0].FullName
$sdk=Join-Path $pf 'Windows Kits/10'
$registry=Get-ItemProperty -LiteralPath 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' -ErrorAction SilentlyContinue
if($registry -and $registry.PSObject.Properties.Name -contains 'KitsRoot10'){$sdk=$registry.KitsRoot10.TrimEnd('\')}
$sdkVersion=@(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory|Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stdlib.h')}|Sort-Object {[version]$_.Name} -Descending)[0].Name
$compiler=Join-Path $vc 'bin/Hostx64/x64/cl.exe'
$exe=Join-Path $OutDir 'test-vehicle-deformation-mesh.exe'
& $compiler /nologo /EHsc /W4 /WX /std:c++17 /MT /O2 `
    ('/I'+$PSScriptRoot) ('/I'+$OutDir) ('/I'+(Join-Path $vc 'include')) `
    ('/I'+(Join-Path $sdk ('Include/'+$sdkVersion+'/ucrt'))) `
    (Join-Path $PSScriptRoot 'test-vehicle-deformation-mesh.cpp') ('/Fo'+(Join-Path $OutDir 'test-vehicle-deformation-mesh.obj')) ('/Fe'+$exe) /link `
    ('/LIBPATH:'+(Join-Path $vc 'lib/x64')) `
    ('/LIBPATH:'+(Join-Path $sdk ('Lib/'+$sdkVersion+'/ucrt/x64'))) `
    ('/LIBPATH:'+(Join-Path $sdk ('Lib/'+$sdkVersion+'/um/x64')))
if($LASTEXITCODE -ne 0){throw 'Vehicle deformation mesh fixture compilation failed.'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Vehicle deformation mesh fixture failed.'}
