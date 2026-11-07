$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NativeOidFixture.ps1')
$seed=Join-Path ([IO.Path]::GetTempPath()) 'jade-cli-integration-seed-path-proof'
$valid="[PersistentDb]`nJournalRootDirectory=$seed\journals`n[TransientDb]`nFileDirectory=$seed\transient`n[JadeEnvironment]`nProgramDataDirectory=$seed\environment`n"
Assert-NativeOidIniPaths -Text $valid -Seed $seed
Assert-NativeOidIniPaths -Text ($valid.Replace("`n","`r`n")) -Seed $seed
foreach($bad in @($valid.Replace("$seed\journals","$seed-other\journals"),$valid.Replace("$seed\environment",'C:\other\environment'),$valid.Replace("JournalRootDirectory=$seed\journals",'JournalRootDirectory=<default>'),$valid.Replace("FileDirectory=$seed\transient",'FileDirectory=relative'))) {
 $rejected=$false
 try {Assert-NativeOidIniPaths -Text $bad -Seed $seed} catch {$rejected=$true}
 if(!$rejected){throw 'A copied or unisolated native fixture INI was accepted.'}
}
Write-Output 'NativeOidFixture owned-directory preflight: PASS'
