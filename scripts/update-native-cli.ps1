#requires -Version 7.0
param([string]$Root=(Get-Location).Path,[string]$Tag,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'

function Initialize-NativePathInspection {
 if('JadeBuildTools.NativePathInfo' -as [type]){return}
 Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace JadeBuildTools {
 public static class NativePathInfo {
  [StructLayout(LayoutKind.Sequential)] struct AttributeTag { public uint Attributes; public uint Tag; }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandleEx(SafeFileHandle file,int kind,out AttributeTag information,uint size);
  public static uint ReadTag(string path) {
   using(var file=CreateFileW(path,0,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
    if(file.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
    AttributeTag info;if(!GetFileInformationByHandleEx(file,9,out info,8))throw new Win32Exception(Marshal.GetLastWin32Error());
    return info.Tag;
   }
  }
  public static bool IsCloudDataTag(uint tag) { return (tag & 0xFFFF0FFFu)==0x9000001Au; }
 }
}
'@
}
function Assert-NativeDataPath($Item){
 if(!($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)){return}
 if($Item.LinkTarget){throw 'Native release paths must not contain links.'}
 Initialize-NativePathInspection
 if(![JadeBuildTools.NativePathInfo]::IsCloudDataTag([JadeBuildTools.NativePathInfo]::ReadTag($Item.FullName))){throw 'Native release path contains an unsupported reparse point.'}
}

function Copy-NativeBoundedStream([IO.Stream]$Source,[IO.Stream]$Destination,[long]$MaxBytes,[Threading.CancellationToken]$CancellationToken=[Threading.CancellationToken]::None){
 $buffer=[byte[]]::new(65536);[long]$total=0
 while(($count=$Source.ReadAsync($buffer,0,$buffer.Length,$CancellationToken).GetAwaiter().GetResult()) -gt 0){$total+=$count;if($total -gt $MaxBytes){throw 'Native release stream size exceeds its limit.'};$Destination.Write($buffer,0,$count)}
 return $total
}
function Save-NativeDownload([string]$Url,[string]$File,[long]$MaxBytes){
 $client=[Net.Http.HttpClient]::new();$client.Timeout=[TimeSpan]::FromSeconds(60);$cancel=[Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(60))
 try{
  $response=$client.GetAsync($Url,[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
  try{
   $response.EnsureSuccessStatusCode()|Out-Null
   if($response.Content.Headers.ContentLength -gt $MaxBytes){throw 'Native release download size exceeds its limit.'}
   $source=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult();$target=[IO.File]::Open($File,[IO.FileMode]::CreateNew)
   try{Copy-NativeBoundedStream -Source $source -Destination $target -MaxBytes $MaxBytes -CancellationToken $cancel.Token|Out-Null}finally{$target.Dispose();$source.Dispose()}
  }finally{$response.Dispose()}
 }finally{$client.Dispose();$cancel.Dispose()}
}

function Assert-NativePath([string]$Relative){
 if(!$Relative -or $Relative.Contains('\') -or $Relative.StartsWith('/') -or $Relative -match '[:*?"<>|\x00-\x1f]' -or ($Relative.Split('/')|Where-Object{!$_ -or $_ -eq '.' -or $_ -eq '..' -or $_ -match '[. ]$' -or $_ -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)'})){throw "Invalid native runtime path: $Relative"}
}
function Assert-NativeContained([string]$Base,[string]$Candidate){
 $parent=[IO.Path]::GetFullPath($Base).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 $absolute=[IO.Path]::GetFullPath($Candidate)
 if(!$absolute.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Native release path escapes its package directory.'}
 for($check=$absolute; $check.Length -ge $parent.TrimEnd('\','/').Length; $check=[IO.Path]::GetDirectoryName($check)){
  if(Test-Path -LiteralPath $check){Assert-NativeDataPath (Get-Item -LiteralPath $check -Force)}
  if(!$check -or $check -eq [IO.Path]::GetPathRoot($check)){break}
 }
}
function Test-NativeDistribution([string]$Directory){
 $manifestFile=Join-Path $Directory 'distribution.json'
 if(!(Test-Path -LiteralPath $manifestFile)){throw 'Native CLI distribution.json is missing.'}
 $manifest=Get-Content -LiteralPath $manifestFile -Raw|ConvertFrom-Json -AsHashtable
 if($manifest.formatVersion -ne 2 -or $manifest.runtime -ne 'native-cpp' -or $manifest.platform -ne 'win32' -or $manifest.arch -ne 'x64' -or $manifest.executable -ne 'jade-cli.exe' -or $manifest.packageVersion -isnot [string] -or $manifest.packageVersion -notmatch '^\d+\.\d+\.\d+$' -or $manifest.runtimeFiles -isnot [Collections.IDictionary]){throw 'Invalid native C++ CLI distribution identity.'}
 $expected=@{};foreach($entry in $manifest.runtimeFiles.GetEnumerator()){$expected[$entry.Key]=$entry.Value};$expected['jade-cli.exe']=$manifest.executableSha256
 foreach($relative in $expected.Keys){
  Assert-NativePath $relative;$file=Join-Path $Directory $relative;Assert-NativeContained $Directory $file
  if($expected[$relative] -isnot [string] -or $expected[$relative] -notmatch '^[a-fA-F0-9]{64}$' -or !(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected[$relative]){throw "Native CLI checksum mismatch: $relative"}
 }
 foreach($file in Get-ChildItem -LiteralPath $Directory -Force -Recurse){Assert-NativeDataPath $file;if(!$file.PSIsContainer){$relative=[IO.Path]::GetRelativePath($Directory,$file.FullName).Replace('\','/');if($relative -ne 'distribution.json' -and !$expected.ContainsKey($relative)){throw "Unlisted native runtime file: $relative"}}}
 return $manifest
}
function Expand-NativeRelease([string]$Archive,[string]$Destination,[string]$ExpectedHash,[long]$MaxExpandedBytes=268435456,[long]$MaxMemberBytes=134217728){
 if($ExpectedHash -notmatch '^[a-fA-F0-9]{64}$' -or (Get-Item -LiteralPath $Archive).Length -gt 268435456 -or (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $ExpectedHash){throw 'Native release archive checksum or download size is invalid.'}
 $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
 try{
  $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$files=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);[long]$total=0
  if($zip.Entries.Count -eq 0){throw 'Native release archive is empty.'};if($zip.Entries.Count -gt 4096){throw 'Native release has too many archive members.'}
  foreach($entry in $zip.Entries){
   $relative=$entry.FullName.TrimEnd('/');Assert-NativePath $relative
   if(!$names.Add($relative)){throw "Duplicate native archive path: $relative"}
   if(!$entry.FullName.EndsWith('/')){$files.Add($relative)|Out-Null}
   $mode=($entry.ExternalAttributes -shr 16) -band 0xF000
   if($mode -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400)){throw 'Native release archive contains a link.'}
   $total+=$entry.Length;if($entry.Length -gt $MaxMemberBytes -or $total -gt $MaxExpandedBytes){throw 'Native archive expanded size exceeds its limit.'}
  }
  foreach($relative in $names){$parent=$relative;while($parent.Contains('/')){$parent=$parent.Substring(0,$parent.LastIndexOf('/'));if($files.Contains($parent)){throw 'Native archive file/directory collision.'}}}
  if(Test-Path -LiteralPath $Destination){throw 'Native archive destination must be a new directory.'}
  [IO.Directory]::CreateDirectory($Destination)|Out-Null
  foreach($entry in $zip.Entries){
   $file=Join-Path $Destination $entry.FullName
   if($entry.FullName.EndsWith('/')){[IO.Directory]::CreateDirectory($file)|Out-Null;continue}
   [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))|Out-Null
   $source=$entry.Open();$target=[IO.File]::Open($file,[IO.FileMode]::CreateNew)
   try{$written=Copy-NativeBoundedStream -Source $source -Destination $target -MaxBytes $entry.Length;if($written -ne $entry.Length){throw 'Native ZIP member size is inconsistent.'}}finally{$target.Dispose();$source.Dispose()}
  }
 }finally{$zip.Dispose()}
 Test-NativeDistribution $Destination|Out-Null
}
function Install-NativeRelease([string]$Root,[string]$Archive,[string]$ExpectedHash,[string]$Version){
 if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid native CLI release version.'}
 $rootPath=[IO.Path]::GetFullPath($Root);$packages=Join-Path $rootPath 'packages';[IO.Directory]::CreateDirectory($packages)|Out-Null
 Assert-NativeContained $rootPath $packages
 $destination=Join-Path $packages 'jade-cli-win-x64';Assert-NativeContained $packages $destination
 $pin=Join-Path $packages 'jade-cli-pin.json';$old=$null
 if(Test-Path -LiteralPath $destination){$old=Test-NativeDistribution $destination;if([version]$Version -lt [version]$old.packageVersion){throw 'Native CLI downgrade is forbidden.'}}
 if(Test-Path -LiteralPath $pin){$current=Get-Content -LiteralPath $pin -Raw|ConvertFrom-Json;if($current.version -eq $Version -and $current.sha256 -eq $ExpectedHash){return @{updated=$false;version=$Version;sha=$ExpectedHash}}}
 $suffix=[guid]::NewGuid().ToString('N');$stage=Join-Path $packages ('.jade-native-'+$suffix+'.stage');$backup=Join-Path $packages ('.jade-native-'+$suffix+'.backup');$pinTemp=Join-Path $packages ('.jade-pin-'+$suffix+'.json');$installed=$false;$committed=$false
 try{
  Expand-NativeRelease -Archive $Archive -Destination $stage -ExpectedHash $ExpectedHash
  $new=Test-NativeDistribution $stage;if($new.packageVersion -ne $Version){throw 'Native CLI manifest version differs from the release tag.'}
  if(Test-Path -LiteralPath $destination){Move-Item -LiteralPath $destination -Destination $backup}
  Move-Item -LiteralPath $stage -Destination $destination;$installed=$true
  [IO.File]::WriteAllText($pinTemp,(@{version=$Version;sha256=$ExpectedHash;tag='v'+$Version}|ConvertTo-Json));Move-Item -LiteralPath $pinTemp -Destination $pin -Force;$committed=$true
  return @{updated=$true;version=$Version;sha=$ExpectedHash;tag='v'+$Version;branch='codex/update-jade-cli-'+$Version}
 }finally{
  if(!$committed){
   if($installed -and (Test-Path -LiteralPath $destination)){Assert-NativeContained $packages $destination;Remove-Item -LiteralPath $destination -Recurse -Force}
   if(Test-Path -LiteralPath $backup){Move-Item -LiteralPath $backup -Destination $destination}
  }
  foreach($directory in @($stage,$backup)){if(Test-Path -LiteralPath $directory){Assert-NativeContained $packages $directory;Remove-Item -LiteralPath $directory -Recurse -Force}}
  if(Test-Path -LiteralPath $pinTemp){Assert-NativeContained $packages $pinTemp;Remove-Item -LiteralPath $pinTemp -Force}
 }
}
function Get-NativeRelease([string]$Tag){
 if($Tag -and $Tag -notmatch '^v\d+\.\d+\.\d+$'){throw 'Native CLI tag must be a stable semantic version.'}
 $endpoint=if($Tag){'tags/'+$Tag}else{'latest'}
 $release=Invoke-RestMethod -Uri ('https://api.github.com/repos/darrenhull/Jade-CLI/releases/'+$endpoint) -Headers @{'User-Agent'='Jade-Native-CLI-Updater'}
 if($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v\d+\.\d+\.\d+$'){throw 'Expected a stable official native CLI release.'}
 if($Tag -and $release.tag_name -ne $Tag){throw 'Native CLI release tag mismatch.'}
 $asset=@($release.assets|Where-Object name -eq 'jade-cli-win-x64.zip');$checks=@($release.assets|Where-Object name -eq 'SHA256SUMS')
 if($asset.Count -ne 1 -or $checks.Count -ne 1 -or $asset[0].size -gt 268435456 -or $checks[0].size -gt 65536){throw 'Native CLI release assets are missing, duplicated or oversized.'}
 foreach($item in @($asset[0],$checks[0])){$expected='https://github.com/darrenhull/Jade-CLI/releases/download/'+$release.tag_name+'/'+$item.name;if($item.browser_download_url -cne $expected){throw 'Untrusted native CLI asset URL.'}}
 return @{release=$release;asset=$asset[0];checks=$checks[0]}
}
function Update-NativeRelease([string]$Root,[string]$Tag){
 $metadata=Get-NativeRelease $Tag;$temporary=Join-Path ([IO.Path]::GetTempPath()) ('jade-native-download-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($temporary)|Out-Null
 try{
  $checks=Join-Path $temporary 'SHA256SUMS';Save-NativeDownload -Url $metadata.checks.browser_download_url -File $checks -MaxBytes 65536
  $matches=@([regex]::Matches([IO.File]::ReadAllText($checks),'(?m)^([a-fA-F0-9]{64})\s+\*?jade-cli-win-x64\.zip\r?$'))
  if($matches.Count -ne 1){throw 'Native release ZIP checksum is missing or duplicated.'}
  $archive=Join-Path $temporary 'native.zip';Save-NativeDownload -Url $metadata.asset.browser_download_url -File $archive -MaxBytes 268435456
  return Install-NativeRelease -Root $Root -Archive $archive -ExpectedHash $matches[0].Groups[1].Value -Version $metadata.release.tag_name.Substring(1)
 }finally{Remove-Item -LiteralPath $temporary -Recurse -Force}
}
if($MyInvocation.InvocationName -ne '.'){
 try{
  if($VerifyOnly){Test-NativeDistribution (Join-Path $Root 'packages/jade-cli-win-x64')|Out-Null;Write-Output 'Native CLI distribution integrity verified.'}
  else{$result=Update-NativeRelease -Root $Root -Tag $Tag;if($env:GITHUB_OUTPUT){foreach($key in @('updated','version','sha','tag','branch')){if($result.ContainsKey($key)){Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value ($key+'='+$result[$key].ToString().ToLowerInvariant())}}};$result|ConvertTo-Json -Compress}
 }catch{Write-Error $_;exit 1}
}
