param([string]$Seed=$env:JADE_OID_TEST_SEED,[string]$JadeHome=$env:JADE_HOME)
$ErrorActionPreference='Stop'
if(!$Seed -or !$JadeHome){throw 'Explicit disposable JADE_OID_TEST_SEED and licensed JADE_HOME are required.'}
. (Join-Path $PSScriptRoot '../NativeOidFixture/NativeOidFixture.ps1')
Assert-NativeOidIniPaths -Text ([IO.File]::ReadAllText((Join-Path $Seed 'working.ini'))) -Seed $Seed
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $root 'scripts/NativeToolchain.ps1')
Import-NativeToolchain
$directory=Join-Path ([IO.Path]::GetTempPath()) ('jade-oid-native-test-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory)|Out-Null
$previousSeed=$env:JADE_OID_TEST_SEED;$previousHome=$env:JADE_HOME
try {
 $env:JADE_OID_TEST_SEED=$Seed;$env:JADE_HOME=$JadeHome
 $exe=Join-Path $directory 'NativeOid_roundtrip_tests.exe'
 & cl.exe /nologo /std:c++20 /EHsc /MT /O2 /UNDEBUG /utf-8 /DUNICODE /D_UNICODE ('/I'+(Join-Path $JadeHome 'include')) ('/I'+(Join-Path $root 'native-src/third_party')) (Join-Path $PSScriptRoot 'NativeOid_roundtrip_tests.cpp') ('/Fo'+$directory+'\') ('/Fe'+$exe) /link bcrypt.lib
 if($LASTEXITCODE -ne 0){throw 'Native OID integration test compilation failed.'}
 & $exe
 if($LASTEXITCODE -ne 0){throw 'Native OID integration test failed.'}
} finally {
 $env:JADE_OID_TEST_SEED=$previousSeed;$env:JADE_HOME=$previousHome
 $resolved=[IO.Path]::GetFullPath($directory)
 if(!$resolved.StartsWith([IO.Path]::GetTempPath(),[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe native test output cleanup path.'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
