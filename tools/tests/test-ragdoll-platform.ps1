param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [switch]$UseBaseline,
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-platform-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src/peds/VrRagdoll.cpp'
if($UseBaseline){$path=Join-Path $StageRoot 'baseline-qbuild/src/peds/VrRagdoll.cpp'}
$text=[IO.File]::ReadAllText($path)
$parts=[Collections.Generic.List[string]]::new()
$parts.Add([regex]::Match($text,'(?ms)^struct StepContacts \{.*?^\};').Value)
foreach($name in @('ProjectContacts','Update')){
 $match=[regex]::Match($text,'(?m)^(?:static\s+)?void\s+'+$name+'\([^)]*\)\s*\{')
 if(-not$match.Success){throw "Missing production $name"}
 $end=$match.Index+$match.Length;$depth=1
 while($end-lt$text.Length-and$depth-gt0){if($text[$end]-eq'{'){$depth++}elseif($text[$end]-eq'}'){$depth--};$end++}
 $parts.Add($text.Substring($match.Index,$end-$match.Index))
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-platform-production.inc'),($parts-join"`n"),[Text.UTF8Encoding]::new($false))
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-ragdoll-schedule.cpp'))
$fixture=$fixture.Substring(0,$fixture.IndexOf('static P::State Body('))
$fixture=$fixture.Replace('ragdoll-schedule-production.inc','ragdoll-platform-production.inc')
$fixture=$fixture.Replace('static int gathers;','static int gathers; static Batch platform;')
$fixture=$fixture.Replace('++gathers;batch=Batch();','++gathers;batch=platform;')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-platform-fixture.inc'),$fixture,[Text.UTF8Encoding]::new($false))
Write-Host ('Full production Update SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-platform.cpp')
