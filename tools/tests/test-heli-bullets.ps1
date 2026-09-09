param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path $StageRoot 'checks-heli-bullets'),
    [Parameter(Mandatory=$true)][string]$OriginalRoot,
    [string]$VehiclesCol = ''
)
$ErrorActionPreference = 'Stop'
foreach($required in @('src/weapons/Weapon.cpp','src/vehicles/Heli.cpp','src/collision/Collision.cpp')){
    if(-not(Test-Path -LiteralPath (Join-Path $StageRoot $required))){throw 'StageRoot must be an assembled engine tree. This core-function fixture cannot run against the source kit overlay alone.'}
}
if(-not(Test-Path -LiteralPath (Join-Path $StageRoot 'baseline-qbuild/src/weapons/Weapon.cpp'))){throw 'The helicopter comparison fixture also requires its historical baseline under StageRoot/baseline-qbuild.'}
function Get-Block([string]$Text,[int]$Start) {
    $open=$Text.IndexOf('{',$Start)
    if($Start -lt 0 -or $open -lt 0){throw 'Production block not found.'}
    $depth=0
    for($i=$open;$i -lt $Text.Length;$i++){
        if($Text[$i] -eq '{'){$depth++}
        if($Text[$i] -eq '}'){$depth--;if($depth -eq 0){return $Text.Substring($Start,$i-$Start+1)}}
    }
    throw 'Unterminated production block.'
}
function Get-Method([string]$Text,[string]$Name) {
    $match=[regex]::Match($Text,'(?m)^(bool|float|void)\r?\n'+[regex]::Escape($Name)+'\(')
    if(-not $match.Success){throw "Missing production method: $Name"}
    return Get-Block $Text $match.Index
}
function Get-TrackedBranch([string]$Text) {
    $method=Get-Method $Text 'CWeapon::FireInstantHit'
    $start=$method.IndexOf('if (useVrAim)')
    return Get-Block $method $start
}
$weapon=[IO.File]::ReadAllText((Join-Path $StageRoot 'src/weapons/Weapon.cpp'))
$old=[IO.File]::ReadAllText((Join-Path $StageRoot 'baseline-qbuild/src/weapons/Weapon.cpp'))
$heli=[IO.File]::ReadAllText((Join-Path $StageRoot 'src/vehicles/Heli.cpp'))
$collision=[IO.File]::ReadAllText((Join-Path $StageRoot 'src/collision/Collision.cpp'))
$baseHeli=(& git -c ('safe.directory='+$OriginalRoot) -C $OriginalRoot show 026cd10:src/vehicles/Heli.cpp) -join "`n"
if($LASTEXITCODE -ne 0){throw 'Cannot read original helicopter implementation.'}
$baseWeapon=(& git -c ('safe.directory='+$OriginalRoot) -C $OriginalRoot show 026cd10:src/weapons/Weapon.cpp) -join "`n"
$methods=''
foreach($name in @('CHeli::TestBulletCollision','CHeli::TestSniperCollision','CHeli::TestRocketCollision')){
    $current=Get-Method $heli $name
    if($current.Replace("`r",'') -cne (Get-Method $baseHeli $name).Replace("`r",'')){
        throw "Native damage semantics changed: $name"
    }
    $methods+=$current+"`n"
}
$methods=(Get-Method $collision 'CCollision::DistToLine')+"`n"+
    (Get-Method $collision 'CCollision::ProcessLineSphere')+"`n"+
    (Get-Method $collision 'CCollision::ProcessLineTriangle')+"`n"+$methods
$triangle=[IO.File]::ReadAllText((Join-Path $StageRoot 'src/collision/ColTriangle.cpp'))
$setMatches=[regex]::Matches($triangle,'(?m)^void\r?\nCColTrianglePlane::Set\(')
$methods+=(Get-Block $triangle $setMatches[$setMatches.Count-1].Index)+"`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'heli-native-production.inc'),$methods,[Text.UTF8Encoding]::new($false))
$header=[IO.File]::ReadAllText((Join-Path $StageRoot 'src/vehicles/Vehicle.h'))
$emptyBlowup=[regex]::Match($header,'virtual void BlowUpCar\(CEntity \*ent\) \{\}').Value
if(-not $emptyBlowup){throw 'Re-audit special helicopter virtual explosion path.'}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'heli-empty-blowup-production.inc'),$emptyBlowup,[Text.UTF8Encoding]::new($false))
$wrappers=''
foreach($entry in @(@('Fixed',$weapon),@('OriginalTracked',$old))){
    $wrappers+="static void Run$($entry[0])(CEntity *shooter,CVector *fireSource,CWeaponInfo *info,eWeaponType m_eWeaponType) {`n"+
        "CVector source,target,vrTraceSource; CVector2D ahead; CColPoint point; CEntity *victim=nil;`n"+
        "CVector vrFireSource=gAimSource,vrFireDirection=gAimDirection; bool useVrAim=true;`n"+
        (Get-TrackedBranch $entry[1])+"`n gResultVictim=victim; gResultTarget=target; gResultTraceSource=vrTraceSource; }`n"
}
# The original first-person branch's special test is also compiled: its M60
# damage remains 20, while ordinary FireInstantHit intentionally remains 4.
$m16=Get-Method $weapon 'CWeapon::FireM16_1stPerson'
$start=$m16.IndexOf('CVector bulletPos;')
$block=Get-Block $m16 ($m16.IndexOf('if ( CHeli::TestBulletCollision',$start))
$wrappers+="static void RunM16Special(CVector source,CVector target,eWeaponType m_eWeaponType){ CVector bulletPos;`n"+$block+"`n}`n"
# Native shotgun and sniper methods must still match the pre-fix source: this
# regression fix must not silently add special damage once per pellet.
foreach($name in @('CWeapon::FireShotgun','CWeapon::FireSniper','CWeapon::FireM16_1stPerson')){
    if((Get-Method $weapon $name).Replace("`r",'') -cne (Get-Method $old $name).Replace("`r",'')){
        throw "Unrelated fire path changed: $name"
    }
}
if((Get-Method $baseWeapon 'CWeapon::FireShotgun').Contains('TestBulletCollision')){throw 'Original shotgun audit assumption changed.'}
$fire=Get-Method $weapon 'CWeapon::Fire'
$start=$fire.IndexOf('case WEAPONTYPE_SHOTGUN:')
$end=$fire.IndexOf('case WEAPONTYPE_ROCKETLAUNCHER:',$start)
$dispatch=$fire.Substring($start,$end-$start)
$wrappers+="bool CWeapon::Dispatch(CEntity *shooter,CVector *source){ bool fired=false,addFireRateAsDelay=false; switch(m_eWeaponType){`n"+
    $dispatch+"`n default:break; } (void)addFireRateAsDelay;return fired;}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'heli-weapon-production.inc'),$wrappers,[Text.UTF8Encoding]::new($false))
Write-Host ('Weapon SHA256: '+(Get-FileHash -LiteralPath (Join-Path $StageRoot 'src/weapons/Weapon.cpp')).Hash)
Write-Host 'Original CHeli bullet/sniper/rocket implementations match base 026cd10.'
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-heli-bullets.cpp')
if($VehiclesCol){
    Write-Host ('Local COL SHA256: '+(Get-FileHash -LiteralPath $VehiclesCol).Hash)
    & (Join-Path $OutDir 'test-heli-bullets.exe') $VehiclesCol
    if($LASTEXITCODE -ne 0){throw 'Actual local helicopter COL regression failed.'}
}
