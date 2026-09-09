param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-vehicle-hook-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$fight = [IO.File]::ReadAllText((Join-Path $StageRoot 'src/peds/PedFight.cpp'))
$physical = [IO.File]::ReadAllText((Join-Path $StageRoot 'src/entities/Physical.cpp'))
$gate = [regex]::Match($fight,'(?ms)^static bool\r?\nIsVrVehicleKnockdown\(.*?^\}')
if(-not $gate.Success){throw 'Missing production low-speed vehicle gate.'}
$start=$physical.IndexOf('// B can move')
$start=$physical.IndexOf('float maxImpulseA = 0.0f;', $start)
$end=$physical.IndexOf('else if(B->IsObject() && B->bUsesCollision && A->IsVehicle())', $start)
if($start -lt 0 -or $end -le $start){throw 'Cannot extract production movable-pair impulse loop.'}
$loop=$physical.Substring($start,$end-$start)
if($loop.IndexOf('preCollisionMoveB = B->m_vecMoveSpeed') -gt $loop.IndexOf('A->ApplyCollision(')){
    throw 'Velocity capture moved after collision response.'
}
$definitions=$gate.Value+"`nstatic void RunPair(CPhysical *A,CPhysical *B) {`nCPed *Aped=(CPed*)A,*Bped=(CPed*)B;`nint i;float impulseA=0,impulseB=0;`n"+$loop+"`n}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-vehicle-hook-production.inc'),$definitions,[Text.UTF8Encoding]::new($false))
foreach($file in @('src/entities/Physical.cpp','src/peds/PedFight.cpp','src/peds/Ped.h')){
    Write-Host ($file+' SHA256: '+(Get-FileHash -LiteralPath (Join-Path $StageRoot $file)).Hash)
}
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-vehicle-hook.cpp')
