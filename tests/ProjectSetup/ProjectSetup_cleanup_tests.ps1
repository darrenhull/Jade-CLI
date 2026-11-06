$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('jade-setup-exit-'+[guid]::NewGuid().ToString('N'))
$child=$null
try{
 New-Item -ItemType Directory -Path $root|Out-Null
 $start=[Diagnostics.ProcessStartInfo]::new()
 $start.FileName=Join-Path $env:SystemRoot 'System32/cmd.exe'
 $start.Arguments='/c ping -n 4 127.0.0.1 >nul'
 $start.WorkingDirectory=$root;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $child=[Diagnostics.Process]::Start($start)
 $locked=$false
 try{Remove-Item -LiteralPath $root -Recurse -Force}catch{$locked=$true}
 if(!$locked -or $child.HasExited){throw 'Fixture must reproduce an active child holding its Windows current directory'}
 . (Join-Path $PSScriptRoot '../../scripts/ProjectSetupCleanup.ps1')
 $timedOut=$false
 try{Wait-ProjectSetupExit -ProcessId $child.Id -TimeoutSeconds 1}catch{$timedOut=$true}
 if(!$timedOut -or $child.HasExited -or !(Test-Path -LiteralPath $root)){throw 'Live child timeout must fail and preserve the locked directory'}
 Wait-ProjectSetupExit -ProcessId $child.Id
 if(!$child.HasExited){throw 'Cleanup returned before owned child exited'}
 Remove-Item -LiteralPath $root -Recurse -Force
 if(Test-Path -LiteralPath $root){throw 'Temporary folder was not removed after exit'}
 Wait-ProjectSetupExit -ProcessId $child.Id
 Write-Output 'ProjectSetup owned-process current-directory lock and already-exited cleanup: PASS'
}finally{
 if($child -and !$child.HasExited){$child.Kill();$child.WaitForExit()}
 if($child){$child.Dispose()}
 $target=[IO.Path]::GetFullPath($root)
 if($target.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $target)){Remove-Item -LiteralPath $target -Recurse -Force}
}
