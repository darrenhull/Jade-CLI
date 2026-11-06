function Import-NativeToolchain {
 if(Get-Command cl.exe -ErrorAction SilentlyContinue){return}
 $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
 $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if(!$installation){throw 'Install the Visual Studio x64 C++ build tools.'}
 $dev=Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
 $environment=& $env:ComSpec /d /c "call `"$dev`" -arch=x64 -host_arch=x64 >nul && set"
 if($LASTEXITCODE -ne 0){throw 'The Visual Studio x64 toolchain could not initialize.'}
 foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
}
