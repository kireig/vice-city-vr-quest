param(
    [Parameter(Mandatory = $true)][string]$ShaderTools,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$kitRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$shaderRoot = Join-Path $kitRoot 'librw/src/vulkan/shaders'
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw 'Choose a new test output directory.' }
New-Item -ItemType Directory -Path $outputRoot | Out-Null

function Invoke-ShaderTool([string]$Name, [string[]]$ToolArguments) {
    & (Join-Path $ShaderTools "$Name.exe") @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
}

function Compile-Shader([string]$Name, [string]$Source, [string[]]$Options = @()) {
    $output = Join-Path $outputRoot "$Name.spv"
    Invoke-ShaderTool 'glslc' (@('--target-env=vulkan1.1', '-O', '-I', $shaderRoot,
        '-o', $output, (Join-Path $shaderRoot $Source)) + $Options)
    Invoke-ShaderTool 'spirv-val' @('--target-env', 'vulkan1.1', $output)
    return $output
}

function Read-Spirv([string]$Path) {
    $result = & (Join-Path $ShaderTools 'spirv-dis.exe') $Path
    if ($LASTEXITCODE -ne 0) { throw 'spirv-dis failed' }
    return $result -join "`n"
}

foreach ($source in @('rw_world.vert', 'rw_skin.vert', 'rw_im3d.vert', 'rw_im2d.vert')) {
    Compile-Shader $source $source | Out-Null
}
$world = Compile-Shader 'world' 'rw_world.frag'
foreach ($alpha in @(0, 1)) {
    foreach ($effects in @(0, 1)) {
        $specialized = Join-Path $outputRoot "world-alpha$alpha-effects$effects.spv"
        Invoke-ShaderTool 'spirv-opt' @('--set-spec-const-default-value', "0:$alpha 1:$effects",
            '--freeze-spec-const', '--fold-spec-const-op-composite', '-O', '--remove-unused-interface-variables',
            '--eliminate-dead-variables', $world, '-o', $specialized)
        Invoke-ShaderTool 'spirv-val' @('--target-env', 'vulkan1.1', $specialized)
        $assembly = Read-Spirv $specialized
        if ($effects -eq 0) {
            if ($assembly -match 'DescriptorSet 3|Location [45]\b|OpLoopMerge|OpAtomic') {
                throw "Effects OFF retained effects resources/work (alpha=$alpha)."
            }
            if (($assembly | Select-String 'OpImageSample' -AllMatches).Matches.Count -ne 1) {
                throw 'Effects OFF must sample only the material texture.'
            }
        } elseif ($assembly -notmatch 'DescriptorSet 3') {
            throw 'Effects ON lost the reflection resources.'
        }
        if (($alpha -eq 0) -and ($assembly -match 'OpKill')) {
            throw 'Opaque world specialization retained discard.'
        }
    }
}

$coverageOn = Compile-Shader 'im2d-coverage' 'rw_im2d.frag'
$coverageOff = Compile-Shader 'im2d-no-coverage' 'rw_im2d.frag' @('-DRW_IM2D_COVERAGE=0')
if ((Read-Spirv $coverageOn) -notmatch 'OpAtomicOr') { throw 'Coverage ON lost its mask writes.' }
if ((Read-Spirv $coverageOff) -match 'OpAtomic|StorageBuffer|BufferBlock') {
    throw 'Coverage OFF retained storage-buffer writes.'
}

$state = [IO.File]::ReadAllText((Join-Path $kitRoot 'librw/src/vulkan/vkstate.cpp'))
if ($state -notmatch 'getPipelineForKey\(work\[i\]\.shader, work\[i\]\.topology, work\[i\]\.key\)') {
    throw 'Deferred pipeline compilation does not retain the requested effect variant.'
}
if ($state -notmatch 'if\(\(key & \(uint64\(1\) << 21\)\) != 0\)') {
    throw 'Deferred radar depth state does not use the captured key.'
}
if ($state -notmatch 'const uint64 baselineKey = key & ~effectBits;' -or
    $state -notmatch 'reflectionCoverageComplete = 0;') {
    throw 'Cold effects pipelines must preserve baseline geometry without accepting incomplete coverage history.'
}
Write-Host 'PASS: shaders compile and validate; all four world specializations checked; coverage OFF has no atomics.'
