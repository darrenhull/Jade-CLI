param([string]$JadeHome=$env:JADE_HOME)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$JadeHome -or !(Test-Path -LiteralPath (Join-Path $JadeHome 'include/jomcalls.h'))){throw 'Set JADE_HOME to an installed licensed Jade SDK with include/jomcalls.h.'}
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
Import-NativeToolchain
$targets=@{
 'jade-native-helper'=@('native-helper/main.cpp','native-helper/model.cpp','native-helper/jade_adapter.cpp','native-helper/cache_refresh.cpp','native-helper/sdk_abi.cpp')
 'jade-debug-helper'=@('debugger/debug_host.cpp')
 'jade-inspect-helper'=@('inspector/inspector.cpp')
 'jade-test-helper'=@('test-runner/test_host.cpp')
}
foreach($target in $targets.Keys){
 $directory=Join-Path $root ('build/'+$target);New-Item -ItemType Directory -Path $directory -Force|Out-Null
 $sources=@($targets[$target]|ForEach-Object {Join-Path $root ('native-src/'+$_)})
 $arguments=@('/nologo','/std:c++20','/EHsc','/O2','/MT','/utf-8','/DUNICODE','/D_UNICODE',('/I'+(Join-Path $JadeHome 'include')),('/I'+(Join-Path $root 'native-src/third_party')))+$sources+@(('/Fo'+$directory+'\'),('/Fe'+(Join-Path $root ('native-bin/'+$target+'.exe'))),'/link','bcrypt.lib')
 & cl.exe @arguments
 if($LASTEXITCODE -ne 0){throw "Native Jade helper build failed: $target"}
}
