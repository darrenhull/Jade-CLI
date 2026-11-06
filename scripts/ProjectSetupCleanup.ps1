function Wait-ProjectSetupExit {
 param([Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
       [ValidateRange(1,60)][int]$TimeoutSeconds=5)
 try { Wait-Process -Id $ProcessId -Timeout $TimeoutSeconds -ErrorAction Stop }
 catch {
  # An acknowledged shutdown can exit before Wait-Process acquires its handle.
  # A live process is still a cleanup failure; never delete its current directory.
  if(Get-Process -Id $ProcessId -ErrorAction SilentlyContinue){throw}
 }
}
