param(
    [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir=(Join-Path $StageRoot 'checks-never-wanted'),
    [Parameter(Mandatory=$true)][string]$EngineRoot
)
$ErrorActionPreference='Stop'
if(-not(Test-Path -LiteralPath (Join-Path $StageRoot 'src/core/Wanted.cpp'))){throw 'StageRoot must be an assembled engine tree containing Wanted.cpp; the source kit distributes that file as patches.'}
if(-not(Test-Path -LiteralPath (Join-Path $EngineRoot 'src/core/Crime.h'))){throw 'EngineRoot must be an assembled engine tree containing src/core/Crime.h.'}
function Get-Method([string]$Text,[string]$Name){
    $match=[regex]::Match($Text,'(?m)^(?:void|bool|int32)\s*\r?\n'+[regex]::Escape($Name)+'\(')
    if(-not $match.Success){throw "Production method not found: $Name"}
    $open=$Text.IndexOf('{',$match.Index);$depth=0
    for($i=$open;$i -lt $Text.Length;$i++){
        if($Text[$i] -eq '{'){$depth++}
        if($Text[$i] -eq '}'){$depth--;if($depth -eq 0){return $Text.Substring($match.Index,$i-$match.Index+1)}}
    }
    throw "Unterminated production method: $Name"
}
$wantedPath=Join-Path $StageRoot 'src/core/Wanted.cpp'
$wanted=[IO.File]::ReadAllText($wantedPath)
$globals=@([regex]::Matches($wanted,'(?m)^(?:bool|int32) CWanted::(?:bNoWantedCheat|MaximumWantedLevel|nMaximumWantedLevel)\s*=.*?;')|ForEach-Object{$_.Value})
if($globals.Count -ne 3){throw 'Expected the three production wanted globals.'}
$definitions=($globals -join "`n")+"`n"
foreach($name in @('Initialise','NumOfHelisRequired','SetNoWantedCheat','SetWantedLevel','SetWantedLevelNoDrop','CheatWantedLevel','SetMaximumWantedLevel','RegisterCrime','RegisterCrime_Immediately','ClearQdCrimes','AddCrimeToQ','ReportCrimeNow','UpdateWantedLevel','Update','ResetPolicePursuit','Reset','UpdateCrimesQ','Suspend')){
    $definitions+=(Get-Method $wanted ('CWanted::'+$name))+"`n"
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'never-wanted-production.inc'),$definitions,[Text.UTF8Encoding]::new($false))
$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path -LiteralPath $OutDir){throw 'Choose a new output directory.'}
[void][IO.Directory]::CreateDirectory($OutDir)
if(-not (Test-Path -LiteralPath (Join-Path $StageRoot 'src/core/Crime.h'))){
    Copy-Item -LiteralPath (Join-Path $EngineRoot 'src/core/Crime.h') -Destination (Join-Path $OutDir 'Crime.h')
}
$pf=[Environment]::GetFolderPath('ProgramFilesX86')
$vswhere=Join-Path $pf 'Microsoft Visual Studio/Installer/vswhere.exe'
$install=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if($LASTEXITCODE -ne 0 -or $install.Count -ne 1){throw 'MSVC tools not found.'}
$vc=@(Get-ChildItem -LiteralPath (Join-Path $install[0] 'VC/Tools/MSVC') -Directory|Sort-Object {[version]$_.Name} -Descending)[0].FullName
$sdk=Join-Path $pf 'Windows Kits/10'
$registry=Get-ItemProperty -LiteralPath 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' -ErrorAction SilentlyContinue
if($registry -and $registry.PSObject.Properties.Name -contains 'KitsRoot10'){$sdk=$registry.KitsRoot10.TrimEnd('\')}
$sdkVersion=@(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory|Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stdlib.h')}|Sort-Object {[version]$_.Name} -Descending)[0].Name
$compiler=Join-Path $vc 'bin/Hostx64/x64/cl.exe'
$exe=Join-Path $OutDir 'test-never-wanted.exe'
& $compiler /nologo /EHsc /W4 /WX /std:c++17 /MT /O2 `
    ('/I'+$PSScriptRoot) ('/I'+$OutDir) ('/I'+(Join-Path $vc 'include')) `
    ('/I'+(Join-Path $sdk ('Include/'+$sdkVersion+'/ucrt'))) `
    (Join-Path $PSScriptRoot 'test-never-wanted.cpp') ('/Fo'+(Join-Path $OutDir 'test-never-wanted.obj')) ('/Fe'+$exe) /link `
    ('/LIBPATH:'+(Join-Path $vc 'lib/x64')) `
    ('/LIBPATH:'+(Join-Path $sdk ('Lib/'+$sdkVersion+'/ucrt/x64'))) `
    ('/LIBPATH:'+(Join-Path $sdk ('Lib/'+$sdkVersion+'/um/x64')))
if($LASTEXITCODE -ne 0){throw 'Never-wanted fixture compilation failed.'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Never-wanted fixture failed.'}
Write-Host ('Wanted.cpp SHA256: '+(Get-FileHash -LiteralPath $wantedPath).Hash)
Write-Host ('Wanted.h SHA256: '+(Get-FileHash -LiteralPath (Join-Path $StageRoot 'src/core/Wanted.h')).Hash)
