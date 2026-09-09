param(
 [string]$KitRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-wheels-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $KitRoot 'src/core/FileLoader.cpp'
if(-not(Test-Path -LiteralPath $path)){throw 'Run this test against assembled source (src/core/FileLoader.cpp).'}
$source=[IO.File]::ReadAllText($path).Replace("`r`n","`n")
$marker='}else if(strncmp(line, "MODELFILE", 9) == 0){'
$start=$source.IndexOf($marker)
$end=$source.IndexOf('}else if(strncmp(line, "HIERFILE", 8) == 0){',$start)
if($start -lt 0 -or $end -lt 0){throw 'Production MODELFILE block missing.'}
$body=$source.Substring($start+$marker.Length,$end-$start-$marker.Length)
$moves=$source.IndexOf('static RwTexture*'+"`n"+'MoveTexturesCB')
$movesEnd=$source.IndexOf('#define isLine3',$moves)
if($moves -lt 0 -or $movesEnd -lt 0){throw 'Production texture transfer functions missing.'}
$output=$source.Substring($moves,$movesEnd-$moves)+"`nvoid CFileLoader::ExecuteModelFile(const char *line,RwTexDictionary *savedTxd){`n"+$body+"`n}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'modelset-wheels-production.inc'),$output,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $KitRoot -OutDir $OutDir -Sources @('test-modelset-wheels.cpp')
if($LASTEXITCODE -ne 0){throw 'Production wheel binding regression failed.'}
Write-Host ('FileLoader SHA256: '+(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)
