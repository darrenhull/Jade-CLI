param([switch]$TestsOnly,[switch]$SkipTests,[string]$TestFilter='*')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root 'build/native-cli'
New-Item -ItemType Directory -Force -Path $build | Out-Null
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
Import-NativeToolchain
function Get-NativeInputStamp {
    $inputs=Get-ChildItem (Join-Path $root 'native-cli') -File -Recurse | Where-Object {$_.Extension -in @('.cpp','.hpp')}
    return (($inputs | Sort-Object FullName | ForEach-Object {$_.FullName+':'+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}) -join "`n")
}
$inputStamp=Get-NativeInputStamp
$common=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/UNDEBUG','/utf-8','/DUNICODE','/D_UNICODE','/DNOMINMAX',('/I'+(Join-Path $root 'native-cli')),('/I'+(Join-Path $root 'native-cli/engine')),('/I'+(Join-Path $root 'native-src/third_party')))
$sources=@(Get-ChildItem (Join-Path $root 'native-cli') -Filter *.cpp -Recurse | Where-Object { $_.FullName -notmatch '[\\/]tests[\\/]' -and $_.Name -ne 'main.cpp' } | ForEach-Object FullName)
$headerTime=(Get-ChildItem (Join-Path $root 'native-cli') -Filter '*.hpp' -Recurse | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
$changed=@($sources | Where-Object {$object=Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($_)+'.obj');-not (Test-Path -LiteralPath $object) -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt (Get-Item -LiteralPath $_).LastWriteTimeUtc -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $headerTime})
if($changed.Count){
    & cl.exe @common /MP4 /c @changed ('/Fo'+$build+'\')
    if($LASTEXITCODE -ne 0){throw 'Native library compilation failed'}
}
$objects=@($sources | ForEach-Object {Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($_)+'.obj')})
$library=Join-Path $build 'jade-native-cli.lib'
& lib.exe /nologo ('/OUT:'+$library) @objects
if($LASTEXITCODE -ne 0){throw 'Native library archive failed'}
if((Get-NativeInputStamp) -ne $inputStamp){throw 'Native source changed during compilation. Rerun after edits finish.'}
if(-not $SkipTests){
    $testRoots=@((Join-Path $root 'native-cli'), (Join-Path $root 'native-src/inspector/tests'))
    $tests=@($testRoots | Where-Object {Test-Path -LiteralPath $_} | ForEach-Object {Get-ChildItem -LiteralPath $_ -Filter '*_tests.cpp' -Recurse} | Where-Object {$_.FullName -match '[\\/]tests[\\/]' -and $_.BaseName -like $TestFilter})
    if(-not $tests.Count){throw 'No native tests found'}
    foreach($test in $tests){
        $name=$test.BaseName
        $target=Join-Path $build ($name+'.exe')
        & cl.exe @common $test.FullName ('/Fo'+$build+'\') ('/Fe'+$target) $library /link bcrypt.lib advapi32.lib shell32.lib xmllite.lib shlwapi.lib ole32.lib
        if($LASTEXITCODE -ne 0){throw "Compilation failed: $name"}
        & $target
        if($LASTEXITCODE -ne 0){throw "Test failed: $name"}
    }
}
if($TestsOnly){exit 0}
if((Get-NativeInputStamp) -ne $inputStamp){throw 'Native source changed during tests. Rerun before packaging.'}
$output=Join-Path $root 'dist/jade-cli-win-x64'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& cl.exe @common (Join-Path $root 'native-cli/main.cpp') ('/Fo'+$build+'\') ('/Fe'+(Join-Path $output 'jade-cli.exe')) $library /link bcrypt.lib advapi32.lib shell32.lib xmllite.lib shlwapi.lib ole32.lib
if($LASTEXITCODE -ne 0){throw 'Native CLI compilation failed'}
# Only declared native runtime assets belong in this distribution. Reject leftovers.
foreach($entry in Get-ChildItem $output -Force){if($entry.Name -notin @('jade-cli.exe','native','README.md','ThirdPartyNotices.txt','distribution.json')){throw "Unexpected legacy runtime file in native distribution: $($entry.Name). Build into an empty dist/jade-cli-win-x64 directory."}}
$native=Join-Path $output 'native'
New-Item -ItemType Directory -Force -Path $native | Out-Null
Get-ChildItem (Join-Path $root 'native-bin') -File | Where-Object {$_.Extension -in @('.exe','.json')} | Copy-Item -Destination $native -Force
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $output -Force
Copy-Item -LiteralPath (Join-Path $root 'ThirdPartyNotices.txt') -Destination $output -Force
$nativeVersion=[regex]::Match([IO.File]::ReadAllText((Join-Path $root 'native-cli/Version.hpp')),'cliVersion\[\]\s*=\s*"([^"]+)"').Groups[1].Value
$manifest=@{oidFormat='jade-native';formatVersion=2;runtime='native-cpp';platform='win32';arch='x64';packageVersion=$nativeVersion;executable='jade-cli.exe';executableSha256=(Get-FileHash -LiteralPath (Join-Path $output 'jade-cli.exe') -Algorithm SHA256).Hash.ToLowerInvariant();runtimeFiles=@{}}
foreach($file in Get-ChildItem $output -File -Recurse){if($file.Name -notin @('distribution.json','jade-cli.exe')){$relative=$file.FullName.Substring($output.Length+1).Replace('\','/');$manifest.runtimeFiles[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'distribution.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $output 'jade-cli.exe') -Destination (Join-Path $root 'jade-cli.exe') -Force
Write-Output "Native CLI: $output"
