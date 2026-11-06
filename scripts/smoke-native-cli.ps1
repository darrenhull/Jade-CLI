param([string]$Distribution=(Join-Path $PSScriptRoot '../dist/jade-cli-win-x64'))
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-native-portable-'+[guid]::NewGuid().ToString('N'))
$oldPath=$env:PATH;$oldState=$env:JADE_TOOLS_STATE_DIRECTORY;$previous=(Get-Location).Path
try{
 New-Item -ItemType Directory -Path $root|Out-Null
 Copy-Item -LiteralPath $Distribution -Destination (Join-Path $root 'runtime') -Recurse
 $exe=Join-Path $root 'runtime/jade-cli.exe'
 $workspace=Join-Path $root 'source';New-Item -ItemType Directory -Path $workspace|Out-Null
 @'
schemaDefinition
PortableSchema subschemaOf RootSchema partialDefinition;
typeHeaders
PortablePet subclassOf Object transient;
typeDefinitions
PortablePet completeDefinition
(
)
typeSources
'@ | Set-Content -LiteralPath (Join-Path $workspace 'DifferentFileName.cls') -Encoding utf8
 $env:PATH=Join-Path $env:SystemRoot 'System32'
 $env:JADE_TOOLS_STATE_DIRECTORY=Join-Path $root 'state'
 Set-Location -LiteralPath $root
 $help=& $exe --help;if($LASTEXITCODE -ne 0 -or ($help -join "`n") -notmatch 'native C\+\+'){throw 'Native portable help failed'}
 $doctor=& $exe doctor --json|ConvertFrom-Json;if($LASTEXITCODE -ne 0 -or $doctor.runtimeImplementation -ne 'native-cpp'){throw 'Native portable doctor failed'}
 $found=& $exe source search --workspace $workspace --query PortablePet --json|ConvertFrom-Json
 if($LASTEXITCODE -ne 0 -or $found.total -ne 1 -or $found.rows[0].schema -ne 'PortableSchema'){throw 'Native portable discovery failed'}
 $status=& $exe status --json|ConvertFrom-Json;if($LASTEXITCODE -ne 0 -or !$status.brokerPID){throw 'Native broker status failed'}
 $closed=& $exe shutdown --json|ConvertFrom-Json;if($LASTEXITCODE -ne 0 -or !$closed.passed){throw 'Native broker shutdown failed'}
 $shutdownComplete=$true
 if(Get-Process -Id $status.brokerPID -ErrorAction SilentlyContinue){Wait-Process -Id $status.brokerPID -Timeout 5 -ErrorAction Stop}
 Write-Output 'Native portable CLI smoke: PASS (outside checkout, Node absent from PATH)'
}finally{
 if(!$shutdownComplete -and $exe -and (Test-Path -LiteralPath $exe)){& $exe shutdown --json|Out-Null}
 Set-Location -LiteralPath $previous
 $env:PATH=$oldPath;$env:JADE_TOOLS_STATE_DIRECTORY=$oldState
 if($root.StartsWith([IO.Path]::GetTempPath(),[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $root -Recurse -Force}
}
