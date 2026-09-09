param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-ragdoll-schedule-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src\peds\VrRagdoll.cpp'
$text=[IO.File]::ReadAllText($path)
$parts=[Collections.Generic.List[string]]::new()
$structure=[regex]::Match($text,'(?ms)^struct StepContacts \{.*?^\};')
if(-not$structure.Success){throw 'Missing combined contact context.'}
$parts.Add($structure.Value)
foreach($name in @('ProjectContacts','Update')){
 $signature=[regex]::Match($text,'(?m)^(?:static\s+)?void\s+'+$name+'\([^)]*\)\s*\{')
 if(-not$signature.Success){throw "Missing production $name"}
 $end=$signature.Index+$signature.Length;$depth=1
 while($end-lt$text.Length-and$depth-gt0){if($text[$end]-eq'{'){$depth++}elseif($text[$end]-eq'}'){$depth--};$end++}
 if($depth-ne0){throw "Unterminated production $name"}
 $parts.Add($text.Substring($signature.Index,$end-$signature.Index))
}
if(-not[regex]::IsMatch($text,'MAX_POSES\s*=\s*NUMPEDS')){throw 'Retention no longer matches native ped capacity.'}
if(-not[regex]::IsMatch($text,'candidate.pending = slot->waitTime;')){throw 'Fairness must use uncapped waiting time, not capped physics debt.'}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-schedule-production.inc'),($parts-join"`r`n"),[Text.UTF8Encoding]::new($false))
Write-Host ('VrRagdoll.cpp SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-schedule.cpp')
