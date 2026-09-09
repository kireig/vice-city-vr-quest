param(
    [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-explosion-'+[Guid]::NewGuid().ToString('N'))),
    [switch]$Baseline,
    [switch]$DevTools
)
$ErrorActionPreference='Stop'
$sourceRoot=Join-Path $StageRoot 'src'
if($Baseline){
    $sourceRoot=Join-Path $StageRoot 'baseline/src'
    if(-not(Test-Path (Join-Path $sourceRoot 'peds/VrRagdoll.cpp'))){throw 'Baseline reproduction requires the preserved baseline/src tree.'}
}
if(-not(Test-Path (Join-Path $sourceRoot 'peds/VrRagdoll.cpp'))){$sourceRoot=Join-Path $StageRoot 'overlay/src'}
$source=[IO.File]::ReadAllText((Join-Path $sourceRoot 'peds/VrRagdoll.cpp'))
$world=[IO.File]::ReadAllText((Join-Path $sourceRoot 'core/World.cpp'))
$ped=[IO.File]::ReadAllText((Join-Path $sourceRoot 'peds/Ped.cpp'))
function Extract([string]$text,[string]$name){
    $m=[regex]::Match($text,'(?m)^(?:static\s+)?(?:const\s+)?(?:VrRagdollSupport::Transform|Slot\s*\*|bool|void)\s*'+[regex]::Escape($name)+'\([^;]*?\)\s*\{')
    if(-not $m.Success){throw "Missing production function $name"}
    $end=$m.Index+$m.Length;$depth=1
    while($depth -gt 0 -and $end -lt $text.Length){if($text[$end] -eq '{'){$depth++};if($text[$end] -eq '}'){$depth--};$end++}
    if($depth -ne 0){throw "Unterminated $name"}
    return $text.Substring($m.Index,$end-$m.Index)
}
$declStart=$source.IndexOf('enum {')
$declEnd=$source.IndexOf('static bool enabled;')
$code=$source.Substring($declStart,$declEnd-$declStart)
$code+="`nstatic bool enabled; static int active, taken;`nenum { DENY_OFF,DENY_KIND,DENY_RANGE,DENY_SLOTS,DENY_BONES,DENY_COUNT };`nstatic int denied[DENY_COUNT];`n"
$start=$source.IndexOf('static const float TAKE_RANGE_SQ')
$end=$source.IndexOf("void`nSetEnabled",$start)
if($end -lt 0){$end=$source.IndexOf("void`r`nSetEnabled",$start)}
$code+=$source.Substring($start,$end-$start)
$code+="`nstatic void SeedVelocity(Slot*,CPed*);`n"
foreach($name in @('SetEnabled','IsEnabled','FindSlot','Presentation','ApplyPresentation','RefreshBounds','AcquireSlot','Thaw','StartRagdoll','ObserveAnimatedPose','ForgetAnimatedPose','InheritAnimatedMotion','SeedVelocity','Begin','BeginFall')){$code+="`n"+(Extract $source $name)}
if(-not $Baseline){foreach($name in @('TryLateAdmission','ExplosionImpulse')){$code+="`n"+(Extract $source $name)}}
foreach($name in @('Apply','Release')){$code+="`n"+(Extract $source $name)}
$code=('#define MIAMIVR_DEV_TOOLS '+[int]$DevTools.IsPresent)+"`n"+('#define OLD_BASELINE '+[int]$Baseline.IsPresent)+"`nnamespace VrRagdoll {`n"+$code+"`n}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-explosion-production.inc'),$code)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-explosion-bones.inc'),[IO.File]::ReadAllText((Join-Path $sourceRoot 'animation/Bones.h')))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-explosion-world.inc'),(Extract $world 'CWorld::TriggerExplosion'))
$force=[regex]::Match($world,'float fDamageMultiplier = Min\(\(fRadius - fMagnitude\)[\s\S]*?pEntity->ApplyMoveForce\(vecForceDir\);')
if(-not $force.Success){throw 'Native explosion expression not found'}
$forceText=$force.Value
$lift=[regex]::Match($world,'pPed->ApplyMoveForce\(0\.0, 0\.0, 2\.0f\);').Value
if(-not $lift){throw 'Native ped explosion lift not found'}
$gate=[regex]::Match($ped,'SetPedState\(PED_DIE\);[\s\S]*?if \(VrRagdoll::Begin\(this\)\)\s*animId = ANIM_STD_NUM;').Value
if(-not $gate){throw 'Native SetDie gate not found'}
$gate=$gate.Replace('SetPedState(','ped.SetPedState(').Replace('Begin(this)','Begin(&ped)')
$native="static void NativeForce(CPed &ped,const CVector &vecDistance,float fMagnitude,float fRadius,float fPower){`nCPed*pEntity=&ped;CPed*pPed=&ped;`n"+$forceText+"`n"+$lift+"`n}`nstatic void NativeDie(CPed &ped){int animId=0;`n"+$gate+"`n(void)animId;}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-explosion-native.inc'),$native)
Write-Host ('Ragdoll source SHA256: '+(Get-FileHash (Join-Path $sourceRoot 'peds/VrRagdoll.cpp')).Hash)
Write-Host ('World source SHA256: '+(Get-FileHash (Join-Path $sourceRoot 'core/World.cpp')).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-explosion.cpp')
