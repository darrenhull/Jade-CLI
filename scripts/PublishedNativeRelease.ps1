# Verification of an immutable published release before treating a retry as success.
. (Join-Path $PSScriptRoot 'NativeDistribution.ps1')
function Test-PublishedNativeRelease([string]$Tag,[string]$ExpectedVersion){
 if($Tag -notmatch '^v\d+\.\d+\.\d+$'){throw 'Invalid published native tag.'}
 git fetch origin "refs/tags/${Tag}:refs/tags/$Tag" | Out-Null
 if($LASTEXITCODE -ne 0){throw 'Cannot fetch the published native tag.'}
 $commit=git rev-parse --verify --quiet "refs/tags/$Tag^{commit}"
 if($LASTEXITCODE -ne 0 -or ([string]$commit).Trim() -notmatch '^[a-fA-F0-9]{40}$'){throw 'Cannot resolve the published native tag commit.'}
 git merge-base --is-ancestor ([string]$commit).Trim() origin/main
 if($LASTEXITCODE -ne 0){throw 'Published native tag is outside approved main ancestry.'}
 $temporary=Join-Path ([IO.Path]::GetTempPath()) ('jade-published-release-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $temporary | Out-Null
 try{
  gh release download "$Tag" --pattern jade-cli-win-x64.zip --pattern jade-cli.exe --pattern SHA256SUMS --dir "$temporary"
  if($LASTEXITCODE -ne 0){throw 'Cannot download the complete published native release.'}
  foreach($name in @('jade-cli-win-x64.zip','jade-cli.exe','SHA256SUMS')){if(!(Test-Path -LiteralPath (Join-Path $temporary $name) -PathType Leaf)){throw "Published native release is missing $name."}}
  $checksums=@{}
  foreach($line in [IO.File]::ReadAllLines((Join-Path $temporary 'SHA256SUMS'))){
   if(!$line.Trim()){continue}
   if($line -notmatch '^([a-fA-F0-9]{64})  (jade-cli-win-x64\.zip|jade-cli\.exe)$' -or $checksums.ContainsKey($matches[2])){throw 'Invalid published native checksums.'}
   $checksums[$matches[2]]=$matches[1]
  }
  foreach($name in @('jade-cli-win-x64.zip','jade-cli.exe')){if(!$checksums.ContainsKey($name) -or (Get-FileHash -LiteralPath (Join-Path $temporary $name) -Algorithm SHA256).Hash -ne $checksums[$name]){throw "Published native checksum mismatch: $name"}}
  $zip=Join-Path $temporary 'jade-cli-win-x64.zip';$archive=[IO.Compression.ZipFile]::OpenRead($zip)
  try{
   [long]$expanded=0;$names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
   foreach($entry in $archive.Entries){
    $relative=$entry.FullName.TrimEnd('/');Assert-NativePath $relative
    if(!$names.Add($relative) -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000){throw 'Duplicate or linked published archive entry.'}
    $expanded+=$entry.Length;if($expanded -gt 512MB){throw 'Published native archive exceeds its expanded-size limit.'}
   }
  }finally{$archive.Dispose()}
  $expandedDirectory=Join-Path $temporary 'expanded';[IO.Compression.ZipFile]::ExtractToDirectory($zip,$expandedDirectory)
  $published=Test-NativeDistribution $expandedDirectory
  if($published.packageVersion -ne $ExpectedVersion){throw 'Published native version does not match its tag.'}
  foreach($name in @('jade-test-helper.exe','jade-debug-helper.exe','jade-native-helper.exe','jade-inspect-helper.exe')){if(!$published.runtimeFiles.ContainsKey('native/'+$name)){throw "Published native helper is missing: $name"}}
  if((Get-FileHash -LiteralPath (Join-Path $temporary 'jade-cli.exe') -Algorithm SHA256).Hash -ne $published.executableSha256){throw 'Published standalone executable differs from its complete distribution.'}
 }finally{
  Assert-NativeContained ([IO.Path]::GetTempPath()) $temporary
  Remove-Item -LiteralPath $temporary -Recurse -Force
 }
}
