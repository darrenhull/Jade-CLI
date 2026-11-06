param([string]$Distribution=(Join-Path $PSScriptRoot '../../dist/jade-cli-win-x64'))
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-init-smoke-'+[guid]::NewGuid().ToString('N'))
$exe=(Resolve-Path (Join-Path $Distribution 'jade-cli.exe')).Path
$oldProfiles=$env:JADE_TOOLS_CONNECTIONS_FILE;$oldState=$env:JADE_TOOLS_STATE_DIRECTORY;$oldInstall=$env:JADE_INSTALL;$oldHome=$env:JADE_HOME;$previous=(Get-Location).Path
function Assert-Result($value,$message){if(!$value){throw $message}}
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
 Write-Output 'ProjectSetup CLI explicit init, auto-build setup, tiny JSON, unchanged file, doctor and service defaults: PASS'
}finally{
 if(Test-Path -LiteralPath $exe){& $exe shutdown --json|Out-Null}
 Set-Location -LiteralPath $previous
 $env:JADE_TOOLS_CONNECTIONS_FILE=$oldProfiles;$env:JADE_TOOLS_STATE_DIRECTORY=$oldState;$env:JADE_INSTALL=$oldInstall;$env:JADE_HOME=$oldHome
 $resolved=[IO.Path]::GetFullPath($root);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
 if($resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
