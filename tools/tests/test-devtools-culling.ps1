param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-cull-off-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$sourceRoot=Join-Path $StageRoot 'src'
if(-not(Test-Path -LiteralPath (Join-Path $sourceRoot 'render/VrCullViz.cpp'))){$sourceRoot=Join-Path $StageRoot 'overlay/src'}
$header=[IO.File]::ReadAllText((Join-Path $sourceRoot 'render/VrCullViz.h'))
$source=[IO.File]::ReadAllText((Join-Path $sourceRoot 'render/VrCullViz.cpp'))
# Compile the entire actual module, including its engine imports. OFF must
# eliminate those imports, storage and definitions before host compilation.
$source=$source.Replace('#include "VrCullViz.h"',$header)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'devtools-cull-production.inc'),$source,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-devtools-culling.cpp','test-devtools-culling-explicit-off.cpp')
if($LASTEXITCODE -ne 0){throw 'Shipping cull fixture failed.'}
Write-Host ('Production VrCullViz SHA256: '+(Get-FileHash -LiteralPath (Join-Path $sourceRoot 'render/VrCullViz.cpp')).Hash)
