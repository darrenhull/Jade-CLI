$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/update-native-cli.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-native-updater-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root)|Out-Null
function Assert($Condition,$Message){if(!$Condition){throw $Message}}
function Reject($Action,$Pattern){try{& $Action;throw 'Expected rejection was missing'}catch{if($_.Exception.Message -notmatch $Pattern){throw}}}
function ChangeManifest($Archive,$Change){$entry=$Archive.GetEntry('distribution.json');$reader=[IO.StreamReader]::new($entry.Open());try{$manifest=$reader.ReadToEnd()|ConvertFrom-Json -AsHashtable}finally{$reader.Dispose()};$entry.Delete();& $Change $manifest;$entry=$Archive.CreateEntry('distribution.json');$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write(($manifest|ConvertTo-Json -Depth 8))}finally{$writer.Dispose()}}
function Archive($Name,$Extra=$null){
 $directory=Join-Path $root $Name;[IO.Directory]::CreateDirectory((Join-Path $directory 'native'))|Out-Null
 [IO.File]::WriteAllText((Join-Path $directory 'jade-cli.exe'),'native '+$Name)
 [IO.File]::WriteAllText((Join-Path $directory 'native/helper.exe'),'helper')
 $manifest=@{formatVersion=2;runtime='native-cpp';platform='win32';arch='x64';packageVersion='0.2.0';executable='jade-cli.exe';executableSha256=(Get-FileHash (Join-Path $directory 'jade-cli.exe')).Hash.ToLowerInvariant();runtimeFiles=@{'native/helper.exe'=(Get-FileHash (Join-Path $directory 'native/helper.exe')).Hash.ToLowerInvariant()}}
 [IO.File]::WriteAllText((Join-Path $directory 'distribution.json'),($manifest|ConvertTo-Json -Depth 6))
 $zip=Join-Path $root ($Name+'.zip');Compress-Archive -Path (Join-Path $directory '*') -DestinationPath $zip
 if($Extra){$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update);try{& $Extra $archive}finally{$archive.Dispose()}}
 return $zip
}
try{
 $repository=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
 if(Test-Path (Join-Path $repository 'dist/jade-cli-win-x64')){Assert-NativeContained $repository (Join-Path $repository 'dist/jade-cli-win-x64')}
 Initialize-NativePathInspection
 foreach($tag in @('9000001A','9000101A','9000F01A')){Assert ([JadeBuildTools.NativePathInfo]::IsCloudDataTag([Convert]::ToUInt32($tag,16))) 'Known Cloud data tag was rejected'}
 foreach($tag in @('A0000003','A000000C','80000042')){Assert (![JadeBuildTools.NativePathInfo]::IsCloudDataTag([Convert]::ToUInt32($tag,16))) 'Redirecting or unknown reparse tag was allowed'}
 $bytes=[IO.MemoryStream]::new([byte[]](1..16));$output=[IO.MemoryStream]::new();try{Reject {Copy-NativeBoundedStream -Source $bytes -Destination $output -MaxBytes 8} 'size';Assert ($output.Length -le 8) 'Bounded download wrote more than its budget'}finally{$bytes.Dispose();$output.Dispose()}
 $cancel=[Threading.CancellationTokenSource]::new();$cancel.Cancel();$bytes=[IO.MemoryStream]::new([byte[]](1..16));$output=[IO.MemoryStream]::new();try{Reject {Copy-NativeBoundedStream -Source $bytes -Destination $output -MaxBytes 32 -CancellationToken $cancel.Token} 'cancel';Assert ($output.Length -eq 0) 'Cancelled download wrote bytes'}finally{$bytes.Dispose();$output.Dispose();$cancel.Dispose()}
 $zip=Archive 'valid';$target=Join-Path $root 'target'
 Expand-NativeRelease -Archive $zip -Destination $target -ExpectedHash (Get-FileHash $zip).Hash
 Assert (Test-Path (Join-Path $target 'native/helper.exe')) 'Valid native helper was not extracted'
 Reject {Expand-NativeRelease -Archive $zip -Destination (Join-Path $root 'bad-hash') -ExpectedHash ('0'*64)} 'checksum'
 $traversal=Archive 'traversal' {param($z)$e=$z.CreateEntry('../escape');$s=[IO.StreamWriter]::new($e.Open());$s.Write('bad');$s.Dispose()}
 Reject {Expand-NativeRelease -Archive $traversal -Destination (Join-Path $root 'traversal-out') -ExpectedHash (Get-FileHash $traversal).Hash} 'path'
 $duplicate=Archive 'duplicate' {param($z)$z.CreateEntry('JADE-CLI.EXE')|Out-Null}
 Reject {Expand-NativeRelease -Archive $duplicate -Destination (Join-Path $root 'duplicate-out') -ExpectedHash (Get-FileHash $duplicate).Hash} 'duplicate'
 $link=Archive 'link' {param($z)$e=$z.CreateEntry('link');$e.ExternalAttributes=[int]0xA1FF0000}
 Reject {Expand-NativeRelease -Archive $link -Destination (Join-Path $root 'link-out') -ExpectedHash (Get-FileHash $link).Hash} 'link'
 Reject {Expand-NativeRelease -Archive $zip -Destination (Join-Path $root 'large-out') -ExpectedHash (Get-FileHash $zip).Hash -MaxExpandedBytes 8} 'size'
 Reject {Expand-NativeRelease -Archive $zip -Destination (Join-Path $root 'invalid-hash') -ExpectedHash 'not-a-hash'} 'checksum'
 $empty=Join-Path $root 'empty.zip';$emptyZip=[IO.Compression.ZipFile]::Open($empty,[IO.Compression.ZipArchiveMode]::Create);$emptyZip.Dispose()
 Reject {Expand-NativeRelease -Archive $empty -Destination (Join-Path $root 'empty-out') -ExpectedHash (Get-FileHash $empty).Hash} 'missing|empty'
 $truncated=Join-Path $root 'truncated.zip';[IO.File]::WriteAllBytes($truncated,[byte[]](80,75,3,4))
 Reject {Expand-NativeRelease -Archive $truncated -Destination (Join-Path $root 'truncated-out') -ExpectedHash (Get-FileHash $truncated).Hash} 'central directory|archive|End of'
 $reserved=Archive 'reserved' {param($z)$z.CreateEntry('native/CON.txt')|Out-Null}
 Reject {Expand-NativeRelease -Archive $reserved -Destination (Join-Path $root 'reserved-out') -ExpectedHash (Get-FileHash $reserved).Hash} 'path'
 $extra=Archive 'extra' {param($z)$z.CreateEntry('undeclared.exe')|Out-Null}
 Reject {Expand-NativeRelease -Archive $extra -Destination (Join-Path $root 'extra-out') -ExpectedHash (Get-FileHash $extra).Hash} 'Unlisted'
 $badType=Archive 'hash-type' {param($z)ChangeManifest $z {param($m)$m.executableSha256=$true}}
 Reject {Expand-NativeRelease -Archive $badType -Destination (Join-Path $root 'hash-type-out') -ExpectedHash (Get-FileHash $badType).Hash} 'checksum'
 $badManifest=Archive 'manifest-shape' {param($z)ChangeManifest $z {param($m)$m.runtimeFiles=@()}}
 Reject {Expand-NativeRelease -Archive $badManifest -Destination (Join-Path $root 'manifest-shape-out') -ExpectedHash (Get-FileHash $badManifest).Hash} 'identity'
 $badIdentity=Archive 'manifest-version' {param($z)ChangeManifest $z {param($m)$m.packageVersion='0.3.0'}}
 Reject {Install-NativeRelease -Root (Join-Path $root 'bad-version-consumer') -Archive $badIdentity -ExpectedHash (Get-FileHash $badIdentity).Hash -Version '0.2.0'} 'version differs'
 $collision=Archive 'collision' {param($z)$z.CreateEntry('native/helper.exe/child')|Out-Null}
 $collisionOut=Join-Path $root 'collision-out'
 Reject {Expand-NativeRelease -Archive $collision -Destination $collisionOut -ExpectedHash (Get-FileHash $collision).Hash} 'collision'
 Assert (!(Test-Path $collisionOut)) 'File/directory collisions must be rejected before extraction'
 $consumer=Join-Path $root 'consumer';[IO.Directory]::CreateDirectory($consumer)|Out-Null
 $first=Install-NativeRelease -Root $consumer -Archive $zip -ExpectedHash (Get-FileHash $zip).Hash -Version '0.2.0'
 Assert $first.updated 'First native installation was skipped'
 $again=Install-NativeRelease -Root $consumer -Archive $zip -ExpectedHash (Get-FileHash $zip).Hash -Version '0.2.0'
 Assert (!$again.updated) 'Unchanged native payload was not reused'
 $new=Archive 'replacement';$updated=Install-NativeRelease -Root $consumer -Archive $new -ExpectedHash (Get-FileHash $new).Hash -Version '0.2.0'
 Assert $updated.updated 'Same version with new digest should update'
 Reject {Install-NativeRelease -Root $consumer -Archive $zip -ExpectedHash (Get-FileHash $zip).Hash -Version '0.1.0'} 'downgrade'
 Assert ([IO.File]::ReadAllText((Join-Path $consumer 'packages/jade-cli-win-x64/jade-cli.exe')) -eq 'native replacement') 'Failed update changed the installed runtime'
 function Move-Item {param($LiteralPath,$Destination,[switch]$Force) if($Destination -eq (Join-Path $consumer 'packages/jade-cli-pin.json')){throw 'injected pin write failure'};Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force}
 try{Reject {Install-NativeRelease -Root $consumer -Archive $zip -ExpectedHash (Get-FileHash $zip).Hash -Version '0.2.0'} 'injected pin write failure'}finally{Remove-Item Function:\Move-Item}
 Assert ([IO.File]::ReadAllText((Join-Path $consumer 'packages/jade-cli-win-x64/jade-cli.exe')) -eq 'native replacement') 'Pin write failure did not restore the prior runtime'
 Assert (@(Get-ChildItem (Join-Path $consumer 'packages') -Force|Where-Object Name -match '^\.jade-').Count -eq 0) 'Failed update left stale stage, backup or pin files'
 $junctionRoot=Join-Path $root 'junction-consumer';$junctionTarget=Join-Path $root 'junction-target';[IO.Directory]::CreateDirectory($junctionRoot)|Out-Null;[IO.Directory]::CreateDirectory($junctionTarget)|Out-Null
 New-Item -ItemType Junction -Path (Join-Path $junctionRoot 'packages') -Target $junctionTarget|Out-Null
 Reject {Install-NativeRelease -Root $junctionRoot -Archive $zip -ExpectedHash (Get-FileHash $zip).Hash -Version '0.2.0'} 'links'
 Write-Output 'PASS NativeReleaseUpdater: valid, checksum, traversal, duplicate, symlink, expanded limits, unchanged, same-version update, downgrade and preservation'
}finally{Remove-Item -LiteralPath $root -Recurse -Force}
