param(
 [Parameter(Mandatory=$true)][string]$GameDataRoot,
 [string[]]$Models = @('sentinel.dff','admiral.dff','stinger.dff','oceanic.dff'),
 [switch]$IncludeModern,
 [string]$OutJson = ''
)
# Read-only metadata inspector for the user's local GTA IMG/DIR archives.
# It emits counts, frame/material metadata and source offsets, never DFF/TXD
# files or vertex/index buffers. No game data is copied into a public kit.
$ErrorActionPreference='Stop'
function Get-RwChildren([byte[]]$Data,[int]$Start,[int]$End) {
 $cursor=$Start
 while($cursor+12 -le $End){
  $kind=[BitConverter]::ToUInt32($Data,$cursor)
  $length=[BitConverter]::ToUInt32($Data,$cursor+4)
  $body=$cursor+12
  $finish=$body+[long]$length
  if($finish -gt $End -or $finish -le $cursor){ throw "Invalid RenderWare chunk at$cursor" }
  [pscustomobject]@{Id=$kind;Start=$cursor;Body=$body;End=[int]$finish;Size=$length}
  $cursor=[int]$finish
 }
}
function Get-RwString([byte[]]$Data,$Chunk) {
 $value=[Text.Encoding]::ASCII.GetString($Data,$Chunk.Body,$Chunk.Size)
 $nul=$value.IndexOf([char]0)
 if($nul -ge 0){$value.Substring(0,$nul)}else{$value}
}
function Get-Material([byte[]]$Data,$Chunk) {
 $children=@(Get-RwChildren $Data $Chunk.Body $Chunk.End)
 $structure=@($children | Where-Object Id -eq 1)[0]
 if($structure.Size -lt 16){ throw 'Short material structure.' }
 $color=@($Data[($structure.Body+4)..($structure.Body+7)] | ForEach-Object {[int]$_})
 $textureName='';$maskName=''
 $texture=@($children | Where-Object Id -eq 6)
 if($texture.Count){
  $textureChildren=@(Get-RwChildren $Data $texture[0].Body $texture[0].End)
  $names=@($textureChildren | Where-Object Id -eq 2)
  if($names.Count -gt 0){$textureName=Get-RwString $Data $names[0]}
  if($names.Count -gt 1){$maskName=Get-RwString $Data $names[1]}
 }
 [pscustomobject]@{rgba=$color;texture=$textureName;mask=$maskName}
}
function Get-Geometry([byte[]]$Data,$Chunk,[int]$Index) {
 $children=@(Get-RwChildren $Data $Chunk.Body $Chunk.End)
 $structure=@($children | Where-Object Id -eq 1)[0]
 if($structure.Size -lt 16){ throw 'Short geometry structure.' }
 $materials=[Collections.Generic.List[object]]::new()
 $materialList=@($children | Where-Object Id -eq 8)
 if($materialList.Count){
  $materialChunks=@(Get-RwChildren $Data $materialList[0].Body $materialList[0].End)
  $materialHeader=@($materialChunks | Where-Object Id -eq 1)[0]
  $count=[BitConverter]::ToInt32($Data,$materialHeader.Body)
  if($count -lt 0 -or $count -gt 4096 -or $materialHeader.Size -lt 4+4*$count){throw 'Invalid material list.'}
  $unique=@($materialChunks | Where-Object Id -eq 7);$next=0
  for($m=0;$m -lt $count;++$m){
   $reference=[BitConverter]::ToInt32($Data,$materialHeader.Body+4+4*$m)
   if($reference -eq -1){$materials.Add((Get-Material $Data $unique[$next]));++$next}
   elseif($reference -ge 0 -and $reference -lt $materials.Count){$materials.Add($materials[$reference])}
   else{throw 'Invalid material reference.'}
  }
 }
 [pscustomobject]@{
  index=$Index;flags=[BitConverter]::ToUInt32($Data,$structure.Body)
  triangles=[BitConverter]::ToUInt32($Data,$structure.Body+4)
  vertices=[BitConverter]::ToUInt32($Data,$structure.Body+8)
  morphTargets=[BitConverter]::ToUInt32($Data,$structure.Body+12)
  materials=@($materials.ToArray())
 }
}
function Get-DffMetadata([byte[]]$Data) {
 if([BitConverter]::ToUInt32($Data,0) -ne 16){throw 'Expected a RenderWare clump.'}
 $end=12+[BitConverter]::ToUInt32($Data,4)
 if($end -gt $Data.Length){throw 'Clump exceeds IMG record.'}
 $children=@(Get-RwChildren $Data 12 $end)
 $frameChunk=@($children | Where-Object Id -eq 14)[0]
 $frameChildren=@(Get-RwChildren $Data $frameChunk.Body $frameChunk.End)
 $frameHeader=@($frameChildren | Where-Object Id -eq 1)[0]
 $frameCount=[BitConverter]::ToInt32($Data,$frameHeader.Body)
 if($frameCount -lt 0 -or $frameCount -gt 4096 -or $frameHeader.Size -lt 4+56*$frameCount){throw 'Invalid frame list.'}
 $extensions=@($frameChildren | Where-Object Id -eq 3)
 $frames=[Collections.Generic.List[object]]::new()
 for($f=0;$f -lt $frameCount;++$f){
  $base=$frameHeader.Body+4+56*$f;$name=''
  if($f -lt $extensions.Count){
   $plugins=@(Get-RwChildren $Data $extensions[$f].Body $extensions[$f].End)
   $nodeName=@($plugins | Where-Object Id -eq 0x0253F2FE)
   if($nodeName.Count){$name=Get-RwString $Data $nodeName[0]}
  }
  $transform=@(0..11 | ForEach-Object {[BitConverter]::ToSingle($Data,$base+4*$_)})
  $frames.Add([pscustomobject]@{index=$f;name=$name;parent=[BitConverter]::ToInt32($Data,$base+48);localTransform=$transform})
 }
 $geometryList=@($children | Where-Object Id -eq 26)[0]
 $geometryChildren=@(Get-RwChildren $Data $geometryList.Body $geometryList.End)
 $geometryChunks=@($geometryChildren | Where-Object Id -eq 15)
 $geometries=[Collections.Generic.List[object]]::new()
 for($g=0;$g -lt $geometryChunks.Count;++$g){$geometries.Add((Get-Geometry $Data $geometryChunks[$g] $g))}
 $atomics=[Collections.Generic.List[object]]::new()
 foreach($atomic in @($children | Where-Object Id -eq 20)){
  $atomicChildren=@(Get-RwChildren $Data $atomic.Body $atomic.End)
  $atomicHeader=@($atomicChildren | Where-Object Id -eq 1)[0]
  $frame=[BitConverter]::ToInt32($Data,$atomicHeader.Body)
  $geometry=[BitConverter]::ToInt32($Data,$atomicHeader.Body+4)
  if($frame -lt 0 -or $frame -ge $frames.Count -or $geometry -lt 0 -or $geometry -ge $geometries.Count){throw 'Invalid atomic frame/geometry reference.'}
  $atomics.Add([pscustomobject]@{index=$atomics.Count;frame=$frame;frameName=$frames[$frame].name;geometry=$geometry;flags=[BitConverter]::ToUInt32($Data,$atomicHeader.Body+8)})
 }
 [pscustomobject]@{
  clumpBytes=$end;totalVertices=($geometries | Measure-Object -Property vertices -Sum).Sum
  totalTriangles=($geometries | Measure-Object -Property triangles -Sum).Sum
  frames=@($frames.ToArray());geometries=@($geometries.ToArray());atomics=@($atomics.ToArray())
 }
}
$sets=[Collections.Generic.List[string]]::new();$sets.Add('models')
if($IncludeModern){$sets.Add('modelsets/modern/models')}
$results=[Collections.Generic.List[object]]::new()
foreach($set in $sets){
 $folder=Join-Path $GameDataRoot $set
 $archive=Join-Path $folder 'gta3.img'
 $directory=[IO.File]::ReadAllBytes((Join-Path $folder 'gta3.dir'))
 $stream=[IO.File]::OpenRead($archive);$reader=[IO.BinaryReader]::new($stream)
 try {
  for($entry=0;$entry+32 -le $directory.Length;$entry+=32){
   $name=[Text.Encoding]::ASCII.GetString($directory,$entry+8,24).TrimEnd([char]0)
   if($name -notin $Models){continue}
   $offset=[BitConverter]::ToUInt32($directory,$entry);$sectors=[BitConverter]::ToUInt32($directory,$entry+4)
   if($sectors -eq 0 -or $sectors -gt 32768){throw 'Unexpected DFF record size.'}
   [void]$stream.Seek([long]$offset*2048,[IO.SeekOrigin]::Begin)
   $data=$reader.ReadBytes($sectors*2048)
   $metadata=Get-DffMetadata $data
   $results.Add([pscustomobject]@{set=$set;archive=$archive;name=$name;sectorOffset=$offset;sectorCount=$sectors;mesh=$metadata})
  }
 } finally {$reader.Dispose();$stream.Dispose()}
}
if($results.Count -ne $Models.Count*$sets.Count){throw 'Not every requested local DFF was found.'}
$json=ConvertTo-Json -InputObject @($results.ToArray()) -Depth 12
if($OutJson){
 [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutJson),$json,[Text.UTF8Encoding]::new($false))
 Write-Host ('Metadata-only JSON: '+[IO.Path]::GetFullPath($OutJson))
}else{$json}
