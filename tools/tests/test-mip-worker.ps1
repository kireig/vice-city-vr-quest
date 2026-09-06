[CmdletBinding()]
param(
    [string]$Compiler = 'cl.exe',
    [string]$OutDir = (Join-Path $env:TEMP ('vcvr-mip-tests-' + [Guid]::NewGuid().ToString('N')))
)

$ErrorActionPreference = 'Stop'
$kitRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = Get-Content -LiteralPath (Join-Path $kitRoot 'librw/src/vulkan/vkraster.cpp') -Raw
$start = $source.IndexOf('struct GeneratedMipJob')
$end = $source.IndexOf('// Creates a texture directly from DXT blocks', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Mip worker extraction markers changed.' }
if (Test-Path -LiteralPath $OutDir) { throw "Test output already exists: $OutDir" }
New-Item -ItemType Directory -Path $OutDir | Out-Null
$worker = $source.Substring($start, $end - $start)
[IO.File]::WriteAllText((Join-Path $OutDir 'mip-worker-extracted.inc'), $worker)
$outputExe = Join-Path $OutDir 'mip-worker-test.exe'
$outputObj = Join-Path $OutDir 'mip-worker-test.obj'
& $Compiler /nologo /std:c++17 /EHsc /W4 /WX /O2 "/I$OutDir" "/Fe$outputExe" "/Fo$outputObj" (Join-Path $PSScriptRoot 'mip-worker-test.cpp')
if ($LASTEXITCODE -ne 0) { throw "Mip worker test compilation failed: $LASTEXITCODE" }
& $outputExe
if ($LASTEXITCODE -ne 0) { throw "Mip worker regression test failed: $LASTEXITCODE" }
