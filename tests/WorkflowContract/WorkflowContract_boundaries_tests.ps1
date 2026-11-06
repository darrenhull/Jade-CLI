$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workflows = Join-Path $root '.github/workflows'
if (Test-Path -LiteralPath (Join-Path $workflows 'update-consumer.yml')) {
    throw 'The native CLI must not own consumer dependency update automation.'
}
foreach ($file in Get-ChildItem -LiteralPath $workflows -Filter '*.yml') {
    $text = [IO.File]::ReadAllText($file.FullName)
    $repositories = @([regex]::Matches($text, 'darrenhull/[A-Za-z0-9_-]+') | ForEach-Object Value | Sort-Object -Unique)
    foreach ($repository in $repositories) {
        if ($repository -ne 'darrenhull/Jade-CLI') { throw "CLI workflow names an unrelated product: $($file.Name)" }
    }
}
$build = [IO.File]::ReadAllText((Join-Path $workflows 'build-release.yml'))
if ($build -notmatch '(?m)^  build:') { throw 'Preserve the required native build check.' }
if ($build -match 'setup-node@|developer-tools:|tests/NativeReleaseUpdater') { throw 'Native CLI CI must not run consumer or Node development tooling.' }
if ($build -notmatch 'WorkflowContract') { throw 'Native workflow boundary checks must execute in CI.' }
Write-Output 'CLI workflow boundaries passed.'
