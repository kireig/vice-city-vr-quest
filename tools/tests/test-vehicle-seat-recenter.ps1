param(
 [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$BackendRoot = '',
 [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-seat-center-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$src = Join-Path $StageRoot 'src'
if (-not (Test-Path -LiteralPath $src)) { $src = Join-Path $StageRoot 'overlay/src' }
$xr = Join-Path $StageRoot 'android/app/src/main/cpp/xr_vulkan_session.cpp'
if (-not (Test-Path -LiteralPath $xr)) { $xr = Join-Path $StageRoot 'overlay/android/app/src/main/cpp/xr_vulkan_session.cpp' }
if (-not $BackendRoot) {
 $BackendRoot = Join-Path $StageRoot 'librw'
 if (-not (Test-Path -LiteralPath $BackendRoot)) { $BackendRoot = Join-Path $StageRoot 'vendor/librw' }
}
$backendPath = Join-Path $BackendRoot 'src/vulkan/vkdevice.cpp'
$android = [IO.File]::ReadAllText((Join-Path $src 'skel/android/android.cpp'))
$debug = [IO.File]::ReadAllText((Join-Path $src 'skel/android/vrdebug.cpp'))
$driving = [IO.File]::ReadAllText((Join-Path $src 'vr/QuestDrivingVR.cpp'))
$backend = [IO.File]::ReadAllText($backendPath)
$session = [IO.File]::ReadAllText($xr)
function Method([string]$text, [string]$name) {
 $result = [regex]::Match($text, '(?ms)^(?:static )?(?:void|bool|bool32)\r?\n' + [regex]::Escape($name) + '\([^\n]*\).*?^\}').Value
 if (-not $result) { throw ('Production method not found: ' + $name) }
 return $result
}
$backendCode = 'namespace rw { namespace vulkan {' + "`n"
foreach ($name in @('getFirstPersonPlayBasis','setHeadPose','getFirstPersonViewFrame','setFirstPersonAnchor','setFirstPersonAnchorBasis')) {
 # RenderWare declarations can wrap arguments across several lines.
 $method = [regex]::Match($backend, '(?ms)^(?:static )?(?:void|bool32)\r?\n' + $name + '\(.*?^\}').Value
 if (-not $method) { throw ('Backend method missing: ' + $name) }
 $backendCode += $method + "`n"
}
$backendCode += "} }`n"
$prologue = [regex]::Match($android, '(?ms)(?<=void\r?\nVrUpdateFirstPersonAnchor\(bool postPhysics\)\r?\n\{\r?\n).*?\tUpdateQuestVehicleSeatRecenter\(player,.*?postPhysics\);').Value
if (-not $prologue) { throw 'Missing actual seat observer call in the camera path.' }
$tracking = [regex]::Match($session, '(?ms)\t\tconst XrViewStateFlags trackedHeadFlags =.*?== trackedHeadFlags;').Value
if (-not $tracking) { throw 'Missing fresh tracking predicate.' }
$code = 'namespace xrvk {' + "`n" + (Method $session 'hasTrackedGameplayHeadPose') + "`n"
$code += 'void UpdateTracking(int located, unsigned viewCount, ViewState viewState) {' + "`n" + $tracking + "`n} }`n"
$code += (Method $debug 'androidgame::VrRecenterView') + "`n"
$code += (Method $android 'UpdateQuestVehicleSeatRecenter') + "`n"
$code += 'void EvaluateSeat(bool postPhysics) {' + "`n" + $prologue + "`n}`n"
$code += 'namespace OculusVR {' + "`n" + (Method $driving 'ApplyQuestVehicleViewOffset') + "`n}`n"
# Verify actual pipeline placement and frame lifetime, not only an isolated latch.
if ($android.IndexOf('UpdateQuestVehicleSeatRecenter(player,') -gt $android.IndexOf('OculusVR::ApplyQuestVehicleViewOffset(&vehicleView)')) { throw 'Seat recenter occurs after calibrated anchor.' }
if ($android -notmatch '(?s)if\(FrontEndMenuManager\.m_bWantToRestart\)\{\s*gVehicleSeatRecenter\.Reset\(\);') { throw 'World restart does not reset seat identity.' }
if ($session -notmatch '(?s)renderFrame\(void\)\s*\{\s*g\.headPoseTrackedForFrame = false;' -or $session -notmatch '(?s)g\.headPoseTrackedForFrame = false;\s*// An empty frame') { throw 'Tracking predicate survives its current render frame.' }
$header = [IO.Path]::GetFullPath((Join-Path $src 'skel/android/VrVehicleSeatRecenter.h')).Replace('\','/')
$enums = @([regex]::Matches($prologue,'\b(?:PED_\w+|OBJECTIVE_\w+)\b') | ForEach-Object {$_.Value}) + @('PED_IDLE','OBJECTIVE_NONE') | Sort-Object -Unique
$code = 'enum { ' + ($enums -join ',') + " };`n" + $code
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-seat-recenter-helper.inc'), '#include "' + $header + '"', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-seat-recenter-backend.inc'), $backendCode, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-seat-recenter-production.inc'), $code, [Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-seat-recenter.cpp')
if ($LASTEXITCODE -ne 0) { throw 'Seat center regression failed.' }
foreach ($file in @((Join-Path $src 'skel/android/android.cpp'),$header,$xr,$backendPath)) {
 Write-Host ((Get-FileHash -LiteralPath $file).Hash + ' ' + $file)
}
