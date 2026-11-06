$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/package-native-cli.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-native-package-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $root 'input/native') -Force|Out-Null
try {
 $inputDir=Join-Path $root 'input';$outputDir=Join-Path $root 'output'
 [IO.File]::WriteAllText((Join-Path $inputDir 'jade-cli.exe'),'native fixture')
 $manifest=@{formatVersion=2;runtime='native-cpp';platform='win32';arch='x64';packageVersion='0.2.0';executable='jade-cli.exe';executableSha256=(Get-FileHash (Join-Path $inputDir 'jade-cli.exe')).Hash;runtimeFiles=@{}}
 foreach($tool in @('jade-test-helper.exe','jade-debug-helper.exe','jade-native-helper.exe','jade-inspect-helper.exe')){$file=Join-Path $inputDir ('native/'+$tool);[IO.File]::WriteAllText($file,'helper');$manifest.runtimeFiles['native/'+$tool]=(Get-FileHash $file).Hash}
 $manifest|ConvertTo-Json -Depth 5|Set-Content (Join-Path $inputDir 'distribution.json') -Encoding utf8
 $result=New-NativePackage -Directory $inputDir -OutputDirectory $outputDir -Tag 'v0.2.0'
 if(!(Test-Path (Join-Path $outputDir 'jade-cli-win-x64.zip')) -or !(Test-Path (Join-Path $outputDir 'SHA256SUMS'))){throw 'Release assets missing'}
 $rejected=$false;try{New-NativePackage -Directory $inputDir -OutputDirectory $outputDir -Tag 'v9.9.9'|Out-Null}catch{$rejected=$true};if(!$rejected){throw 'Mismatched release tag accepted'}
 [IO.File]::WriteAllText((Join-Path $inputDir 'native/jade-test-helper.exe'),'tampered')
 $rejected=$false;try{New-NativePackage -Directory $inputDir -OutputDirectory $outputDir|Out-Null}catch{$rejected=$true};if(!$rejected){throw 'Tampered helper accepted'}
 Write-Output 'NativeDistribution package: PASS'
}finally{if($root.StartsWith([IO.Path]::GetTempPath(),[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $root -Recurse -Force}}
