param(
    [string]$StageRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$SourceRoot = '',
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-driveby-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
if (-not $SourceRoot) {
    $SourceRoot = Join-Path $StageRoot 'src'
    if (-not (Test-Path -LiteralPath $SourceRoot)) { $SourceRoot = Join-Path $StageRoot 'overlay/src' }
}
function Read-Source([string]$Relative) {
    $path = Join-Path $SourceRoot $Relative
    Write-Host ((Get-FileHash -LiteralPath $path).Hash + ' ' + $Relative)
    return [IO.File]::ReadAllText($path)
}
function Method([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, '(?ms)^(?:static )?(?:void|bool|uint32|int|CVehicle \*|CBike \*|int16 \*)\s*\r?\n?' + [regex]::Escape($Name) + '\(.*?^\}')
    if (-not $match.Success) { throw "Missing production function: $Name" }
    return $match.Value
}
function Balanced([string]$Text, [string]$Pattern) {
    $match=[regex]::Match($Text,$Pattern)
    if (-not $match.Success) { throw "Missing production block: $Pattern" }
    $end=$match.Index+$match.Length; $depth=1
    while ($end -lt $Text.Length -and $depth -gt 0) {
        if ($Text[$end] -eq '{') { $depth++ } elseif ($Text[$end] -eq '}') { $depth-- }
        $end++
    }
    if ($depth -ne 0) { throw 'Unbalanced production block.' }
    return $Text.Substring($match.Index,$end-$match.Index)
}
$driving = Read-Source 'vr/QuestDrivingVR.cpp'
$weapons = Read-Source 'vr/QuestWeaponVR.cpp'
$android = Read-Source 'skel/android/android.cpp'
$debugMenu = Read-Source 'skel/android/vrdebug.cpp'
$androidHeader = Read-Source 'skel/android/android.h'
$pad = Read-Source 'core/Pad.cpp'
$cam = Read-Source 'core/Cam.cpp'
$automobile = Read-Source 'vehicles/Automobile.cpp'
$bike = Read-Source 'vehicles/Bike.cpp'
$weapon = Read-Source 'weapons/Weapon.cpp'
$ped = Read-Source 'peds/Ped.cpp'
$code = ''
foreach ($name in @('GetCarGunFired','GetLookLeft','GetLookRight','GetLookBehindForCar')) {
    $code += (Method $pad ('CPad::' + $name)) + "`n"
}
$enums = [regex]::Match($androidHeader, '(?ms)enum eVrPadSource.*?^\};.*?enum eVrPadTarget.*?^\};').Value
if (-not $enums) { throw 'Missing production binding enums.' }
$defaults = [regex]::Match($debugMenu, '(?ms)^static const int kVrPadBindingDefault\[.*?^\};').Value
if (-not $defaults) { throw 'Missing production default button bindings.' }
$code += 'namespace androidgame {' + "`n" + $enums + "`n}`n" + $defaults + "`n"
$code += 'namespace androidgame {' + "`n" + (Method $android 'PadTargetField') + "`n"
$code += (Method $android 'VrApplyPadBindings') + "`n}`n"
$code += "namespace OculusVR {`n"
if ($driving.Contains('DefaultVehicleForwardFire')) {
    $load = [regex]::Match($driving, '(?s)gVehicleForwardFireEnabled = GetPrivateProfileIntA\("VR",\s*"DefaultVehicleForwardFire"[^;]+;')
    if (-not $load.Success) { throw 'Missing forward-fire setting loader.' }
    $code += 'void LoadForwardFireSetting() {' + $load.Value + "}`n"
    foreach ($name in @('IsQuestVehicleForwardFireEnabled','SetQuestVehicleForwardFireEnabled')) { $code += (Method $driving $name) + "`n" }
} else {
    $code += "void LoadForwardFireSetting() {} bool IsQuestVehicleForwardFireEnabled() { return gVehicleForwardFireEnabled; } void SetQuestVehicleForwardFireEnabled(bool on) { gVehicleForwardFireEnabled=on; }`n"
}
foreach ($name in @('GetActivePlayerBike','GetActivePlayerCar','GetDrivingTypeForVehicle','IsDrivingEnvironmentActive','IsVrBikeActive','IsVrCarActive','IsVrDrivingActive','MapHornToPad','MapHandbrakeToPad',$(if($driving.Contains('MapButtonDriveBy')){'MapButtonDriveBy'}else{'MapDefaultDriveBy'}),'UpdateQuestDrivingInput','IsImmersiveVehicleSidearm')) {
    $code += (Method $driving $name) + "`n"
}
if ($driving.Contains('ApplyQuestVehicleButtonInput')) { $code += (Method $driving 'ApplyQuestVehicleButtonInput') + "`n" }
$code += 'bool IsImmersiveDrivingActive() { return IsVrDrivingActive(); }' + "`n"
$code += 'bool IsVrCarDrivingActive() { return IsVrCarActive(); }' + "`n"
$code += 'bool IsVrBikeDrivingActive() { return IsVrBikeActive(); } bool IsImmersiveBikeSidearm(int type) { return IsImmersiveVehicleSidearm(type); }' + "`n"
if ($driving.Contains('GetQuestVehicleButtonFireDirection')) { $code += (Method $driving 'GetQuestVehicleButtonFireDirection') + "`n" }
else { $code += "int GetQuestVehicleButtonFireDirection(CVehicle*) { return -1; }`n" }
foreach ($name in @('IsGameplayAvailable','UpdateWeaponTriggerEdges','ApplyTouchInput')) { $code += (Method $weapons $name) + "`n" }
foreach ($name in @('IsTrackedWeaponTriggerPressed','IsTrackedWeaponTriggerJustPressed')) {
    $match = [regex]::Match($weapons, '(?ms)^bool ' + $name + '\(.*?^\}')
    if (-not $match.Success) { throw "Missing $name" }
    $code += $match.Value + "`n"
}
$code += "}`n"
$code += (Method $automobile 'CAutomobile::DoDriveByShootings') + "`n"
$code += (Method $bike 'CBike::DoDriveByShootings') + "`n"
$code += (Method $weapon 'CWeapon::FireFromCar') + "`n"
$rayMethod = Method $weapon 'CWeapon::FireInstantHitFromCar'
$rayEnd = $rayMethod.IndexOf("`tCWorld::bIncludeBikers = false;")
if ($rayEnd -lt 0) { throw 'Missing actual projectile LOS dispatch.' }
$code += $rayMethod.Substring(0, $rayEnd + "`tCWorld::bIncludeBikers = false;".Length) + "`nreturn true;`n}`n"
$code += (Method $ped 'CPed::RemoveWeaponWhenEnteringVehicle') + "`n"
$capture = [regex]::Match($android, '(?ms)(?<=CapturePad\(RwInt32 padID\)\r?\n\{\r?\n).*?(?=\t// Thumbstick Y)').Value
if (-not $capture) { throw 'Missing CapturePad menu gate.' }
$code += 'void CaptureButtons(int padID) {' + "`n" + $capture + 'androidgame::VrApplyPadBindings(&state, in, true);' + "`n}`n"
$camera = [regex]::Match($cam, '(?ms)\tLookingBehind = false;\r?\n\tLookingLeft = false;\r?\n\tLookingRight = false;\r?\n\tSourceBeforeLookBehind = Source;.*?(?=\t\tif\(Mode == MODE_FOLLOWPED)').Value
if (-not $camera) { throw 'Missing native camera direction dispatch.' }
$code += 'void CCam::UpdateDirection() {' + "`n" + $camera + "}`n}`n"
if ($debugMenu.Contains('VR_VEHICLE_FORWARD_FIRE')) {
    $code += "#define HAS_FORWARD_FIRE_MENU 1`n"
    $code += [regex]::Match($debugMenu,'(?ms)^enum eVrVehicleMenuItem \{.*?^\};').Value + "`n"
    $visible=Balanced $debugMenu 'if\(page == VR_MENU_PAGE_VEHICLE\)\{'
    $code += 'bool IsMenuItemVisible(int page,int item) {'+$visible+"return false;}`n"
    $repeat=[regex]::Match($debugMenu,'(?s)if\(gVrMenuPage == VR_MENU_PAGE_VEHICLE\)\s*(return[^;]+;)').Groups[1].Value
    $code += 'bool ForwardValueRepeats() {'+$repeat+"}`n"
    $locked=[regex]::Match($debugMenu,'(?s)const bool cockpitLocked =.*?;').Value
    $toggle=[regex]::Match($debugMenu,'(?s)case VR_VEHICLE_FORWARD_FIRE:\s*OculusVR::SetQuestVehicleForwardFireEnabled\(.*?break;').Value
    if (-not $repeat -or -not $locked -or -not $toggle) { throw 'Missing production forward option dispatch.' }
    $code += 'void DispatchForward(bool positivePulse,bool decreasePulse) { if(positivePulse || decreasePulse){'+$locked+'if(!cockpitLocked) switch(gVrVehicleSelection){'+$toggle+"}}}`n"
    $page=Balanced $debugMenu '(?m)^static void\s+DrawQuestVehiclePage\(void\)\s*\{'
    $row=[regex]::Match($page,'(?s)snprintf\(rows\[VR_VEHICLE_FORWARD_FIRE\].*?;').Value
    $loop=[regex]::Match($page,'(?s)int visibleRow = 0;.*?(?=\tDrawVrMenuText\("VALUES ARE SAVED)').Value
    if (-not $row -or -not $loop) { throw 'Missing actual vehicle row painting loop.' }
    $code += 'void DrawForwardMenu() { char rows[VR_VEHICLE_ITEM_COUNT][112]; for(auto &row:rows) std::strcpy(row,"EXISTING VEHICLE ROW");std::strcpy(rows[VR_VEHICLE_BACK],"BACK TO SETTINGS");'+$row+$loop+"}`n"
}
if ($android -notmatch 'VrMenuConsumesInput\(\)\)\s*return;' -or $debugMenu -notmatch 'cheatShortcut[^\r\n]*!FindPlayerVehicle\(\)') {
    throw 'Vehicle menu/input ownership guard changed; review this fixture.'
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-driveby-production.inc'), $code, [Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-driveby.cpp')
if ($LASTEXITCODE -ne 0) { throw 'Drive-by host regression failed.' }
