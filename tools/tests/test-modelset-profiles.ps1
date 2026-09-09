param(
 [string]$KitRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-modelsets-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$core=Join-Path $KitRoot 'src/core'
if(-not(Test-Path (Join-Path $core 'ModelSets.cpp'))){$core=Join-Path $KitRoot 'overlay/src/core'}
$source=[IO.File]::ReadAllText((Join-Path $core 'ModelSets.cpp'))
$header=[IO.File]::ReadAllText((Join-Path $core 'ModelSets.h'))
$source=[regex]::Replace($source,'(?m)^#include .*\r?\n','')
$source=$source.Replace('_WIN32','MODELSETS_TEST_WINDOWS')
$streamPath=Join-Path $KitRoot 'src/core/Streaming.cpp'
if(-not(Test-Path $streamPath)){throw 'Run this test against the assembled source tree to exercise the streaming loader.'}
$stream=[IO.File]::ReadAllText($streamPath)
$start=$stream.IndexOf('static ModelSets::eModelCategory')
$end=$stream.IndexOf('static char*',$start)
if($start -lt 0 -or $end -lt 0){throw 'Streaming model-set region missing.'}
$cd=[IO.File]::ReadAllText((Join-Path $KitRoot 'src/core/CdStreamPosix.cpp'))
$cdStart=$cd.IndexOf("bool`nCdStreamAddModelSetImages")
if($cdStart -lt 0){$cd=$cd.Replace("`r`n","`n");$cdStart=$cd.IndexOf("bool`nCdStreamAddModelSetImages")}
$cdEnd=$cd.IndexOf("char *`nCdStreamGetImageName",$cdStart)
if($cdStart -lt 0 -or $cdEnd -lt 0){throw 'Image registration region missing.'}
$output=$header+"`n"+$source+"`n"+$stream.Substring($start,$end-$start)+"`n"+$cd.Substring($cdStart,$cdEnd-$cdStart)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'modelsets-production.inc'),$output,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $KitRoot -OutDir $OutDir -Sources @('test-modelset-profiles.cpp')
if($LASTEXITCODE -ne 0){throw 'Model-set routing regression failed.'}
