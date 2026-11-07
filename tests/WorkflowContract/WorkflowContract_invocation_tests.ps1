$ErrorActionPreference = 'Stop'
$testFile = (Join-Path $PSScriptRoot 'WorkflowContract_release_tests.ps1').Replace("'", "''")
# GitHub's pwsh runner exits LASTEXITCODE even when the final assertion succeeded.
& pwsh -NoProfile -Command "& '$testFile'; if (Test-Path variable:LASTEXITCODE) { exit `$LASTEXITCODE }"
if ($LASTEXITCODE -ne 0) { throw 'Release contract leaked a negative-fixture native exit code into the hosted runner.' }
Write-Output 'Release contract succeeds under the actual hosted PowerShell exit-code wrapper.'
