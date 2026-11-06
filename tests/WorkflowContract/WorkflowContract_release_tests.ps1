$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workflow = [IO.File]::ReadAllText((Join-Path $root '.github/workflows/build-release.yml')).Replace("`r`n", "`n")
if($workflow -match '(?m)^    tags:'){throw 'Tag pushes can race main publication; publish from main pushes only.'}
foreach ($job in @('release-guard','release')) {
    $condition = [regex]::Match($workflow, "(?m)^  ${job}:`n    if: (.+)").Groups[1].Value
    if ($condition -notmatch 'github.event_name == ''push''' -or $condition -notmatch 'github.ref == ''refs/heads/main''') { throw 'A successful protected-main push must publish the native version.' }
}
$match = [regex]::Match($workflow, '(?s)      - name: Publish tested native GitHub release.*?        run: \|\n(.*)$')
if (!$match.Success) { throw 'Expected the version-aware native release publisher.' }
$scriptText = $match.Groups[1].Value -replace '(?m)^          ', ''
$scriptText=$scriptText.Replace('./scripts/PublishedNativeRelease.ps1',("'"+(Join-Path $root 'scripts/PublishedNativeRelease.ps1').Replace("'","''")+"'"))
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('jade-release-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $temporary 'release'),(Join-Path $temporary 'payload') | Out-Null
[IO.File]::WriteAllText((Join-Path $temporary 'payload/distribution.json'), '{"runtime":"native-cpp","packageVersion":"0.2.0"}')
[IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $temporary 'payload'),(Join-Path $temporary 'release/jade-cli-win-x64.zip'))
foreach ($name in @('jade-cli.exe','SHA256SUMS')) { [IO.File]::WriteAllText((Join-Path $temporary ('release/'+$name)),'fixture') }
$published=Join-Path $temporary 'published';$publishedPayload=Join-Path $temporary 'published-payload'
New-Item -ItemType Directory -Path $published,(Join-Path $publishedPayload 'native') | Out-Null
[IO.File]::WriteAllText((Join-Path $publishedPayload 'jade-cli.exe'),'published native executable')
$nativeFiles=@{}
foreach($helper in @('jade-test-helper.exe','jade-debug-helper.exe','jade-native-helper.exe','jade-inspect-helper.exe')){$file=Join-Path $publishedPayload ('native/'+$helper);[IO.File]::WriteAllText($file,$helper);$nativeFiles['native/'+$helper]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
$publishedManifest=@{formatVersion=2;runtime='native-cpp';platform='win32';arch='x64';packageVersion='0.2.0';executable='jade-cli.exe';executableSha256=(Get-FileHash (Join-Path $publishedPayload 'jade-cli.exe') -Algorithm SHA256).Hash;runtimeFiles=$nativeFiles}
[IO.File]::WriteAllText((Join-Path $publishedPayload 'distribution.json'),($publishedManifest|ConvertTo-Json -Depth 5))
[IO.Compression.ZipFile]::CreateFromDirectory($publishedPayload,(Join-Path $published 'jade-cli-win-x64.zip'))
Copy-Item -LiteralPath (Join-Path $publishedPayload 'jade-cli.exe') -Destination $published
function Write-PublishedChecksums([string]$Directory){$lines=foreach($name in @('jade-cli-win-x64.zip','jade-cli.exe')){(Get-FileHash -LiteralPath (Join-Path $Directory $name) -Algorithm SHA256).Hash+'  '+$name};[IO.File]::WriteAllText((Join-Path $Directory 'SHA256SUMS'),($lines -join "`n")+"`n")}
Write-PublishedChecksums $published
$script:publishedMode='valid'
$names = @('GITHUB_SHA','GITHUB_REF_TYPE','GITHUB_REF_NAME','GITHUB_STEP_SUMMARY','GH_REPO')
$before = @{}; foreach ($name in $names) { $before[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$previous = (Get-Location).Path
function gh {
    param([Parameter(ValueFromRemainingArguments=$true)][object[]]$Arguments)
    $global:LASTEXITCODE = 0
    $values = @($Arguments | ForEach-Object { [string]$_ })
    if ($values[0] -ne 'release') { throw 'Unexpected GitHub command.' }
    switch ($values[1]) {
        'view' {
            if (!$script:exists) { $global:LASTEXITCODE = 1;return }
            return (@{isDraft=$script:draft;targetCommitish=$script:tagTarget;url='https://github.com/darrenhull/Jade-CLI/releases/tag/v0.2.0'} | ConvertTo-Json -Compress)
        }
        'download' {
            $directory=$values[[array]::IndexOf($values,'--dir')+1]
            Copy-Item -Path (Join-Path $published '*') -Destination $directory
            switch($script:publishedMode){
              'missing' {Remove-Item -LiteralPath (Join-Path $directory 'SHA256SUMS')}
              'checksum' {[IO.File]::AppendAllText((Join-Path $directory 'jade-cli.exe'),'tampered')}
              'splitExe' {[IO.File]::WriteAllText((Join-Path $directory 'jade-cli.exe'),'different native exe');Write-PublishedChecksums $directory}
              'version' {
                $archive=[IO.Compression.ZipFile]::Open((Join-Path $directory 'jade-cli-win-x64.zip'),[IO.Compression.ZipArchiveMode]::Update)
                try{$archive.GetEntry('distribution.json').Delete();$entry=$archive.CreateEntry('distribution.json');$writer=[IO.StreamWriter]::new($entry.Open());try{$copy=$publishedManifest.Clone();$copy.packageVersion='0.9.0';$writer.Write(($copy|ConvertTo-Json -Depth 5))}finally{$writer.Dispose()}}finally{$archive.Dispose()}
                Write-PublishedChecksums $directory
              }
              'unsafe' {
                $archive=[IO.Compression.ZipFile]::Open((Join-Path $directory 'jade-cli-win-x64.zip'),[IO.Compression.ZipArchiveMode]::Update)
                try{$entry=$archive.CreateEntry('../escape');$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write('unsafe')}finally{$writer.Dispose()}}finally{$archive.Dispose()}
                Write-PublishedChecksums $directory
              }
            }
            return
        }
        'create' {
            if ($values -contains '--verify-tag' -or $values -notcontains '--target' -or $values[[array]::IndexOf($values,'--target')+1] -ne $env:GITHUB_SHA) { throw 'New release must target the approved main SHA without requiring a pre-created tag.' }
            if ($values -notcontains '--draft') { throw 'A new release must remain a draft until all assets are uploaded.' }
            $script:created++;$script:exists=$true;$script:draft=$true;$script:tagTarget=$env:GITHUB_SHA
            if ($script:failInitialUpload) { $global:LASTEXITCODE=1 }
            return
        }
        'upload' { if (!$script:draft -or $values -notcontains '--clobber') { throw 'Only an unpublished matching draft may resume asset upload.' };$script:uploaded++;return }
        'edit' { if (!$script:draft -or $values -notcontains '--draft=false') { throw 'Only the matching draft may be published.' };$script:edited++;$script:draft=$false;return }
        default { throw "Unexpected release operation: $($values[1])" }
    }
}
function git {
    param([Parameter(ValueFromRemainingArguments=$true)][object[]]$Arguments)
    if($Arguments[0] -eq 'merge-base') { $global:LASTEXITCODE=if($script:offMain){1}else{0};return }
    $script:tagQueries++
    if ($script:tagTarget) { $global:LASTEXITCODE=0;return $script:tagTarget }
    $global:LASTEXITCODE=1
}
try {
    Set-Location -LiteralPath $temporary
    $env:GITHUB_SHA='b'*40;$env:GITHUB_REF_TYPE='branch';$env:GITHUB_REF_NAME='main';$env:GH_REPO='darrenhull/Jade-CLI';$env:GITHUB_STEP_SUMMARY=Join-Path $temporary 'summary'
    $script:exists=$false;$script:draft=$false;$script:tagTarget='';$script:created=0;$script:uploaded=0;$script:edited=0;$script:tagQueries=0
    & ([scriptblock]::Create($scriptText))
    if ($script:created -ne 1 -or $script:draft -or $script:edited -ne 1) { throw 'A new native version was not fully uploaded before publication.' }
    & ([scriptblock]::Create($scriptText))
    $env:GITHUB_SHA='c'*40
    & ([scriptblock]::Create($scriptText))
    if ($script:created -ne 1 -or $script:uploaded -ne 0 -or $script:edited -ne 1 -or $script:tagTarget -ne ('b'*40)) { throw 'Published native versions must remain immutable on retries and later main commits.' }
    $script:offMain=$true
    $rejected=$false;try { & ([scriptblock]::Create($scriptText)) } catch { $rejected=$true }
    if(!$rejected){throw 'A published tag outside approved main was accepted without verification.'}
    $script:offMain=$false
    foreach($mode in @('missing','checksum','splitExe','version','unsafe')){
      $script:publishedMode=$mode
      $rejected=$false;try { & ([scriptblock]::Create($scriptText)) } catch { $rejected=$true }
      if(!$rejected){throw "Invalid published release accepted: $mode"}
    }
    $script:publishedMode='valid'
    $script:exists=$false;$script:tagTarget='d'*40
    $rejected=$false;try { & ([scriptblock]::Create($scriptText)) } catch { $rejected=$true };if(!$rejected){throw 'A tag at another commit was accepted.'}
    $env:GITHUB_SHA='b'*40;$script:exists=$true;$script:draft=$true;$script:tagTarget=$env:GITHUB_SHA
    & ([scriptblock]::Create($scriptText))
    if ($script:uploaded -ne 1 -or $script:edited -ne 2 -or $script:draft) { throw 'An interrupted matching draft did not resume idempotently.' }
    $script:exists=$false;$script:tagTarget='';$script:failInitialUpload=$true
    $rejected=$false;try { & ([scriptblock]::Create($scriptText)) } catch { $rejected=$true }
    if (!$rejected -or !$script:draft -or $script:edited -ne 2) { throw 'An interrupted first upload published an incomplete release.' }
    $script:failInitialUpload=$false
    & ([scriptblock]::Create($scriptText))
    if ($script:draft -or $script:edited -ne 3) { throw 'A first-upload interruption was not safely resumable.' }
    Write-Output 'CLI main-release targeting, immutable published versions, retry and draft-resume contracts passed.'
} finally {
    Set-Location -LiteralPath $previous
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$before[$name],'Process') }
    Remove-Item Function:\gh,Function:\git
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
