param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-presentation-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src/peds/VrRagdoll.cpp';$source=[IO.File]::ReadAllText($path)
$parts=[Collections.Generic.List[string]]::new()
$parts.Add([regex]::Match($source,'(?ms)^struct StepContacts \{.*?^\};').Value)
foreach($name in @('ProjectContacts','Update','FindSlot','Presentation','ApplyPresentation','GetBounds','GetClumpBounds','BulletHit')){
 $match=[regex]::Match($source,'(?m)^(?:static\s+)?(?:void|bool|Slot\s*\*|CPed\s*\*|VrRagdollSupport::Transform)\s*'+$name+'\([^)]*\)\s*\{')
 if(-not$match.Success){throw "Missing production $name"}
 $end=$match.Index+$match.Length;$depth=1
 while($depth-gt0-and$end-lt$source.Length){if($source[$end]-eq'{'){$depth++}elseif($source[$end]-eq'}'){$depth--};$end++}
 $parts.Add($source.Substring($match.Index,$end-$match.Index))
}
foreach($mode in @('Replay','Write')){
 $pattern=if($mode-eq'Replay'){'if\(slot->asleep && slot->poseCached\)\s*\{'}else{'if\(slot->asleep\)\s*\{\s*for\(int i = 0; i < hier->numNodes; i\+\+\)\s*slot->cachedPose'}
 $match=[regex]::Match($source,$pattern)
 if(-not$match.Success){throw "Missing production pose cache $mode"}
 $start=$source.IndexOf('{',$match.Index);$end=$start+1;$depth=1
 while($depth-gt0-and$end-lt$source.Length){if($source[$end]-eq'{'){$depth++}elseif($source[$end]-eq'}'){$depth--};$end++}
 if($mode-eq'Write'){
  $tail=[regex]::Match($source.Substring($end),'^\s*ApplyPresentation\(mats,hier->numNodes,presentation\);')
  if(-not$tail.Success){throw 'Raw pose cache must be written before presentation'}
  $end+=$tail.Length
 }
 $parts.Add('static void ProductionCache'+$mode+'(Slot*slot,RwMatrix*mats,int count) {'+"`n"+'struct {int numNodes;} hierarchy={count}; const auto*hier=&hierarchy; const auto presentation=Presentation(slot);'+"`n"+$source.Substring($match.Index,$end-$match.Index)+"`n}")
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-presentation-production.inc'),($parts-join"`n"),[Text.UTF8Encoding]::new($false))
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-ragdoll-schedule.cpp'))
$fixture=$fixture.Substring(0,$fixture.IndexOf('static P::State Body('))
$fixture=$fixture.Replace('struct CPed {','struct CPed : CEntity {').Replace('bool DyingOrDead()const{return dead;}','void *GetClump(){return this;} bool DyingOrDead()const{return dead;}')
$fixture=$fixture.Replace('CPed*ped=nullptr;bool live=false,poseCached=false;','RwMatrix cachedPose[27]; CPed*ped=nullptr;bool live=false,poseCached=false;')
$fixture=$fixture.Replace('CAR_BRAKES,BRAKE_POINTS','CAR_BRAKES,BRAKE_POINTS,HITS,WAKEUPS,BULLET')
$helpers='static int shotHits,shotMisses,shotThaws,shotBusy; static int GetShotPercent(){return 100;} static float BulletImpulse(int){return 8;} static Slot *Thaw(Slot*s){return s;}'
$fixture=$fixture.Replace('#include "ragdoll-schedule-production.inc"',$helpers+"`n"+'#pragma warning(push)'+"`n"+'#pragma warning(disable:4459)'+"`n"+'#include "ragdoll-presentation-production.inc"'+"`n"+'#pragma warning(pop)')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-presentation-fixture.inc'),$fixture,[Text.UTF8Encoding]::new($false))
Write-Host ('Presentation/BulletHit production SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-presentation.cpp')
