#requires -Version 7.0
# Validate only this CLI's native distribution; no download or consumer update behavior.
function Initialize-NativePathInspection {
 if('JadeBuildTools.NativePathInfo' -as [type]){return}
 Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace JadeBuildTools {
 public static class NativePathInfo {
  [StructLayout(LayoutKind.Sequential)] struct AttributeTag { public uint Attributes; public uint Tag; }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandleEx(SafeFileHandle file,int kind,out AttributeTag information,uint size);
  public static uint ReadTag(string path) {
   using(var file=CreateFileW(path,0,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
    if(file.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
    AttributeTag info;if(!GetFileInformationByHandleEx(file,9,out info,8))throw new Win32Exception(Marshal.GetLastWin32Error());
    return info.Tag;
   }
  }
  public static bool IsCloudDataTag(uint tag) { return (tag & 0xFFFF0FFFu)==0x9000001Au; }
 }
}
'@
}
function Assert-NativeDataPath($Item){
 if(!($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)){return}
 if($Item.LinkTarget){throw 'Native release paths must not contain links.'}
 Initialize-NativePathInspection
 if(![JadeBuildTools.NativePathInfo]::IsCloudDataTag([JadeBuildTools.NativePathInfo]::ReadTag($Item.FullName))){throw 'Native release path contains an unsupported reparse point.'}
}

function Assert-NativePath([string]$Relative){
 if(!$Relative -or $Relative.Contains('\') -or $Relative.StartsWith('/') -or $Relative -match '[:*?"<>|\x00-\x1f]' -or ($Relative.Split('/')|Where-Object{!$_ -or $_ -eq '.' -or $_ -eq '..' -or $_ -match '[. ]$' -or $_ -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)'})){throw "Invalid native runtime path: $Relative"}
}
function Assert-NativeContained([string]$Base,[string]$Candidate){
 $parent=[IO.Path]::GetFullPath($Base).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 $absolute=[IO.Path]::GetFullPath($Candidate)
 if(!$absolute.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Native release path escapes its package directory.'}
 for($check=$absolute; $check.Length -ge $parent.TrimEnd('\','/').Length; $check=[IO.Path]::GetDirectoryName($check)){
  if(Test-Path -LiteralPath $check){Assert-NativeDataPath (Get-Item -LiteralPath $check -Force)}
  if(!$check -or $check -eq [IO.Path]::GetPathRoot($check)){break}
 }
}
function Test-NativeDistribution([string]$Directory){
 $manifestFile=Join-Path $Directory 'distribution.json'
 if(!(Test-Path -LiteralPath $manifestFile)){throw 'Native CLI distribution.json is missing.'}
 $manifest=Get-Content -LiteralPath $manifestFile -Raw|ConvertFrom-Json -AsHashtable
 if($manifest.formatVersion -ne 2 -or $manifest.runtime -ne 'native-cpp' -or $manifest.platform -ne 'win32' -or $manifest.arch -ne 'x64' -or $manifest.executable -ne 'jade-cli.exe' -or $manifest.packageVersion -isnot [string] -or $manifest.packageVersion -notmatch '^\d+\.\d+\.\d+$' -or $manifest.runtimeFiles -isnot [Collections.IDictionary]){throw 'Invalid native C++ CLI distribution identity.'}
 $expected=@{};foreach($entry in $manifest.runtimeFiles.GetEnumerator()){$expected[$entry.Key]=$entry.Value};$expected['jade-cli.exe']=$manifest.executableSha256
 foreach($relative in $expected.Keys){
  Assert-NativePath $relative;$file=Join-Path $Directory $relative;Assert-NativeContained $Directory $file
  if($expected[$relative] -isnot [string] -or $expected[$relative] -notmatch '^[a-fA-F0-9]{64}$' -or !(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected[$relative]){throw "Native CLI checksum mismatch: $relative"}
 }
 foreach($file in Get-ChildItem -LiteralPath $Directory -Force -Recurse){Assert-NativeDataPath $file;if(!$file.PSIsContainer){$relative=[IO.Path]::GetRelativePath($Directory,$file.FullName).Replace('\','/');if($relative -ne 'distribution.json' -and !$expected.ContainsKey($relative)){throw "Unlisted native runtime file: $relative"}}}
 return $manifest
}
