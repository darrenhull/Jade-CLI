param([string]$Distribution=(Join-Path $PSScriptRoot '../../dist/jade-cli-win-x64'))
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-init-smoke-'+[guid]::NewGuid().ToString('N'))
$exe=(Resolve-Path (Join-Path $Distribution 'jade-cli.exe')).Path
$oldProfiles=$env:JADE_TOOLS_CONNECTIONS_FILE;$oldState=$env:JADE_TOOLS_STATE_DIRECTORY;$oldInstall=$env:JADE_INSTALL;$oldHome=$env:JADE_HOME;$previous=(Get-Location).Path
function Assert-Result($value,$message){if(!$value){throw $message}}
. (Join-Path $PSScriptRoot '../../scripts/ProjectSetupCleanup.ps1')
$operationFailure=$null
try{
 New-Item -ItemType Directory -Path $root|Out-Null
 $env:JADE_TOOLS_CONNECTIONS_FILE=Join-Path $root 'private/connections.json'
 $env:JADE_TOOLS_STATE_DIRECTORY=Join-Path $root 'broker'
 $env:JADE_INSTALL='';$env:JADE_HOME=''
 $runtime=Join-Path $root 'NotAnInstalledRuntime'
 $first=Join-Path $root 'first';New-Item -ItemType Directory -Path $first|Out-Null;Set-Location -LiteralPath $first
 $unconfigured='' | & $exe build --json | ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -eq 2 -and !$unconfigured.passed -and $unconfigured.report.error -match 'setup is required' -and !(Test-Path jade.json)) 'Noninteractive missing setup must fail without creating a project'
 $setup=& $exe init --profile demo --install $runtime --json|ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -eq 0 -and $setup.created -and $setup.connection -eq 'demo') 'Explicit init failed'
 $project=Get-Content jade.json -Raw|ConvertFrom-Json
 Assert-Result (@($project.psobject.Properties).Count -eq 1 -and $project.connection -eq 'demo') 'Project configuration should contain only connection reference'
 $bytes=[IO.File]::ReadAllBytes((Join-Path $first 'jade.json'))
 $again=& $exe init --json|ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -eq 0 -and !$again.created -and [Convert]::ToBase64String($bytes) -eq [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $first 'jade.json')))) 'Init overwrote existing project'
 $doctor=& $exe doctor --json|ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -eq 0 -and $doctor.runtime -eq $runtime -and $doctor.sdkVersion -eq '0.3.0') 'Minimal profile doctor failed'
 $second=Join-Path $root 'second';New-Item -ItemType Directory -Path $second|Out-Null;Set-Location -LiteralPath $second
 @'
schemaDefinition
Demo subschemaOf RootSchema partialDefinition;
typeHeaders
Pet subclassOf Object transient;
typeDefinitions
Pet completeDefinition
(
)
typeSources
'@ | Set-Content Pet.cls
 # The fake runtime intentionally cannot compile. Auto-setup precedes native validation
 # without opening or writing any real Jade database.
 $failed=& $exe build Pet.cls --json 2> (Join-Path $root 'build-progress.txt')|ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -ne 0 -and !$failed.passed -and (Test-Path jade.json)) 'Missing project did not auto-initialize before native validation'
 $request=@{id='prepare';command='prepare';operation='build';argv=@('Pet.cls');cwd=$second}|ConvertTo-Json -Compress
 $prepared=@($request|& $exe service --stdio|ForEach-Object{$_|ConvertFrom-Json}|Where-Object event -eq result)[0].result
 Assert-Result ($prepared.passed -and $prepared.prepared.config.databasePath -eq (Join-Path $runtime 'system') -and $prepared.prepared.config.workspaceRoots[0] -eq $second) 'Service did not share inferred configuration'
 $third=Join-Path $root 'test-auto';New-Item -ItemType Directory -Path $third|Out-Null
 Copy-Item -LiteralPath (Join-Path $second 'Pet.cls') -Destination $third
 Set-Location -LiteralPath $third
 $testFailed=& $exe test Pet.cls --json 2> (Join-Path $root 'test-progress.txt')|ConvertFrom-Json
 Assert-Result ($LASTEXITCODE -ne 0 -and !$testFailed.passed -and (Test-Path jade.json)) 'Missing test project did not auto-initialize before native validation'
 Write-Output 'ProjectSetup CLI explicit init, auto-build setup, tiny JSON, unchanged file, doctor and service defaults: PASS'
}catch{
 $operationFailure=$_
 throw
}finally{
 $cleanupFailure=$null
 try{
  if(Test-Path -LiteralPath $exe){
   $owned=& $exe status --json|ConvertFrom-Json
   Assert-Result ($LASTEXITCODE -eq 0 -and $owned.brokerPID -gt 0) 'Cannot identify owned smoke-test broker'
   $stopped=& $exe shutdown --json|ConvertFrom-Json
   Assert-Result ($LASTEXITCODE -eq 0 -and $stopped.passed) 'Smoke-test broker shutdown failed'
   Wait-ProjectSetupExit -ProcessId $owned.brokerPID
  }
 }catch{$cleanupFailure=$_}
 finally{
  Set-Location -LiteralPath $previous
  $env:JADE_TOOLS_CONNECTIONS_FILE=$oldProfiles;$env:JADE_TOOLS_STATE_DIRECTORY=$oldState;$env:JADE_INSTALL=$oldInstall;$env:JADE_HOME=$oldHome
 }
 if(!$cleanupFailure){
  $resolved=[IO.Path]::GetFullPath($root);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
  try{if($resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}}
  catch{$cleanupFailure=$_}
 }
 if($cleanupFailure){
  if($operationFailure){Write-Warning ('Cleanup failed; preserving original test failure and temporary evidence: '+$cleanupFailure.Exception.Message)}
  else{throw $cleanupFailure}
 }
}
