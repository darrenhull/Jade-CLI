param([string]$Directory=(Join-Path $PSScriptRoot '../dist/jade-cli-win-x64'),[string]$OutputDirectory=(Join-Path $PSScriptRoot '../dist'),[string]$Tag)
$ErrorActionPreference='Stop'
$releaseTag=$Tag
. (Join-Path $PSScriptRoot 'NativeDistribution.ps1')
function New-NativePackage([string]$Directory,[string]$OutputDirectory,[string]$Tag){
 $manifest=Test-NativeDistribution $Directory
 if($Tag -and $Tag -ne ('v'+$manifest.packageVersion)){throw 'Release tag must exactly match the native CLI version.'}
 foreach($name in @('jade-test-helper.exe','jade-debug-helper.exe','jade-native-helper.exe','jade-inspect-helper.exe')){if(!$manifest.runtimeFiles.ContainsKey('native/'+$name)){throw "Required native helper missing from distribution: $name"}}
 [IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
 $archive=Join-Path $OutputDirectory 'jade-cli-win-x64.zip'
 Compress-Archive -Path (Join-Path $Directory '*') -DestinationPath $archive -Force
 Copy-Item -LiteralPath (Join-Path $Directory 'jade-cli.exe') -Destination (Join-Path $OutputDirectory 'jade-cli.exe') -Force
 $lines=foreach($name in @('jade-cli-win-x64.zip','jade-cli.exe')){(Get-FileHash -LiteralPath (Join-Path $OutputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$name}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'SHA256SUMS'),($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
 return @{version=$manifest.packageVersion;archive=$archive;runtime='native-cpp'}
}
if($MyInvocation.InvocationName -ne '.'){
 try{New-NativePackage -Directory $Directory -OutputDirectory $OutputDirectory -Tag $releaseTag|ConvertTo-Json -Compress}catch{Write-Error $_;exit 1}
}
