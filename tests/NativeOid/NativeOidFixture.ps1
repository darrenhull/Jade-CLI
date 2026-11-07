function Assert-NativeOidIniPaths([string]$Text,[string]$Seed) {
 $prefix=[IO.Path]::GetFullPath($Seed).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 foreach($key in @('JournalRootDirectory','FileDirectory')) {
  if($Text -notmatch ('(?im)^\s*'+$key+'\s*=\s*(?!<default>)(\S[^\r\n]*)\r?$')) {
   throw "Private native fixture requires an explicit owned $key."
  }
 }
 foreach($match in [regex]::Matches($Text,'(?im)^\s*[^;#=\r\n]*Directory\s*=\s*([^\r\n]+)')) {
  $directory=$match.Groups[1].Value.Trim()
  if($directory -eq '<default>'){continue}
  if(![IO.Path]::IsPathFullyQualified($directory) -or ![IO.Path]::GetFullPath($directory).StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {
   throw 'Private native fixture INI contains a mutable directory outside its own root.'
  }
 }
}
