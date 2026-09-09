param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-ragdoll-weight-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src/peds/VrRagdoll.cpp'
if (-not (Test-Path -LiteralPath $path)) {$path=Join-Path $StageRoot 'overlay/src/peds/VrRagdoll.cpp'}
$source=[IO.File]::ReadAllText($path)
$parts=[regex]::Match($source,'(?ms)^\s*static const unsigned char parts\[\]\[6\] = \{.*?^\s*\};')
$call=[regex]::Match($source,'(?s)VrRagdollImpulse::ApplyPartImpulse\(hitSlot->pos,.*?;')
if (-not $parts.Success -or -not $call.Success) {throw 'Cannot extract actual bullet part map and impulse call.'}
$api=@([regex]::Matches($source,'(?m)^(?:void SetWeightPercent|int GetWeightPercent)\([^\r\n]*\}') | ForEach-Object {$_.Value})
if ($api.Count -ne 2) {throw 'Cannot extract actual mass getter/setter.'}
$generated='namespace VrRagdoll {'+"`n"+($api -join "`n")+"`n}`n"+
 'static void ProductionBullet(P::State *hitSlot,int part,const CVector &hitPos,const CVector &direction,float impulse) {'+"`n"+
 'using namespace P; const VrRagdollSupport::Transform hitPresentation;'+"`n"+$parts.Value+"`n"+$call.Value+"`n}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-weight-production.inc'),$generated,[Text.UTF8Encoding]::new($false))
Write-Host ('VrRagdoll.cpp SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-weight.cpp')
