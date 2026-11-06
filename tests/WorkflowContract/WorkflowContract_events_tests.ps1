$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workflows = Join-Path $root '.github/workflows'
$build = [IO.File]::ReadAllText((Join-Path $workflows 'build-release.yml')).Replace("`r`n", "`n")
if ($build -notmatch '(?m)^    branches: \[''main''\]$') { throw 'Heavy builds must run only for main pushes, not feature pushes.' }
if ($build -notmatch '(?m)^  pull_request:' -or $build -notmatch '(?m)^  workflow_dispatch:' -or $build -match '(?m)^    tags:') { throw 'Keep PR/manual validation and only main push publication; tags must not race the main publisher.' }
if ($build -match '(?m)^  open-agent-pr:') { throw 'PR metadata creation must not require the heavy native build.' }
$agent = [IO.File]::ReadAllText((Join-Path $workflows 'agent-pr.yml')).Replace("`r`n", "`n")
if ($agent -notmatch 'codex/agent/\*\*' -or $agent -notmatch 'uses: \./\.github/workflows/open-agent-pr.yml') { throw 'Keep bot PR creation in a separate agent-branch workflow.' }
if ($agent -match 'build-native-cli|package-native-cli|setup-node|actions: write|gh workflow run|workflow_dispatch') { throw 'The lightweight agent workflow must not dispatch duplicate PR builds.' }
$opener = [IO.File]::ReadAllText((Join-Path $workflows 'open-agent-pr.yml')).Replace("`r`n", "`n")
$scriptText = [regex]::Match($opener, '(?s)        run: \|\n(.*)$').Groups[1].Value -replace '(?m)^          ', ''
if (!$scriptText) { throw 'PR opener script was not found.' }
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('jade-workflow-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$variables = @('GITHUB_EVENT_NAME','GITHUB_ACTOR','GITHUB_REPOSITORY','GITHUB_REF','GITHUB_SHA','GITHUB_RUN_ID','RUNNER_TEMP','GITHUB_OUTPUT','GITHUB_STEP_SUMMARY','DEFAULT_BRANCH','SOURCE_PR','REVIEWER')
$before = @{}
foreach ($name in $variables) { $before[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function gh {
    param([Parameter(ValueFromRemainingArguments=$true)][object[]]$Arguments)
    $global:LASTEXITCODE = 0
    $argsText = @($Arguments | ForEach-Object { [string]$_ })
    if ($argsText[0] -eq 'pr' -and $argsText[1] -eq 'merge') {
        throw 'Work-in-progress pull requests must not queue automatic merge.'
    }
    if ($argsText[0] -eq 'pr' -and $argsText[1] -eq 'ready') {
        if ($argsText -notcontains '--undo') { throw 'A code push must never mark work ready for review.' }
        $script:pull.draft=$true; $script:draftConversions++; return
    }
    $method = if ($argsText -contains '--method') { $argsText[[array]::IndexOf($argsText,'--method')+1] } else { 'GET' }
    $endpoint = @($argsText | Where-Object { $_ -like 'repos/*' })[0]
    if ($endpoint -like '*/pulls?state=*') {
        if (!$script:existing) { return '[]' }
        return ConvertTo-Json -InputObject @($script:pull) -Depth 10 -Compress
    }
    if ($endpoint -like '*/commits/*') { return '{"commit":{"message":"Native workflow change\n\nValidated native workflow policy."}}' }
    if ($method -eq 'POST' -and $endpoint -eq 'repos/darrenhull/Jade-CLI/pulls') {
        $payloadPath=$argsText[[array]::IndexOf($argsText,'--input')+1]
        if (!(Get-Content -LiteralPath $payloadPath -Raw|ConvertFrom-Json).draft) { throw 'New work-in-progress pull requests must be drafts.' }
        $script:createdPosts++; return ($script:pull | ConvertTo-Json -Depth 10 -Compress)
    }
    if ($endpoint -like '*/requested_reviewers') { $script:pull.requested_reviewers = @(@{login='darrenhull'}); return ($script:pull | ConvertTo-Json -Depth 10 -Compress) }
    if ($endpoint -eq 'repos/darrenhull/Jade-CLI/pulls/7') { return ($script:pull | ConvertTo-Json -Depth 10 -Compress) }
    throw "Unexpected mocked GitHub request: $($argsText -join ' ')"
}
try {
    foreach ($entry in @{GITHUB_EVENT_NAME='push';GITHUB_ACTOR='darrenhull';GITHUB_REPOSITORY='darrenhull/Jade-CLI';GITHUB_REF='refs/heads/codex/agent/workflow-contract';GITHUB_SHA=('a'*40);GITHUB_RUN_ID='123';RUNNER_TEMP=$temporary;DEFAULT_BRANCH='main';SOURCE_PR='0';REVIEWER='darrenhull'}.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key,$entry.Value,'Process') }
    foreach ($reuse in @($false,$true)) {
        $script:existing = $reuse; $script:createdPosts = 0; $script:draftConversions = 0
        $script:pull = @{number=7;html_url='https://github.com/darrenhull/Jade-CLI/pull/7';user=@{login='github-actions[bot]';type='Bot'};head=@{ref='codex/agent/workflow-contract';sha=('a'*40);repo=@{full_name='darrenhull/Jade-CLI'}};base=@{ref='main'};requested_reviewers=@();draft=(!$reuse)}
        $env:GITHUB_OUTPUT = Join-Path $temporary ('output-' + $reuse + '.txt')
        $env:GITHUB_STEP_SUMMARY = Join-Path $temporary ('summary-' + $reuse + '.txt')
        & ([scriptblock]::Create($scriptText))
        $outputs = [IO.File]::ReadAllText($env:GITHUB_OUTPUT).Replace("`r`n", "`n")
        if ($outputs -notmatch '(?m)^number=7$') { throw 'Verified PR identity output was lost.' }
        if ($script:createdPosts -ne [int](!$reuse)) { throw 'Existing PR reuse must not post a second PR.' }
        if ($script:pull.requested_reviewers[0].login -ne 'darrenhull') { throw 'Human review was not requested.' }
        if (!$script:pull.draft -or $script:draftConversions -ne [int]$reuse) { throw 'New and reused work must remain draft until implementation, checks and review fixes are complete.' }
    }
    Write-Output 'CLI main/PR event policy and bot new/reuse reviewer contracts passed.'
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name,$before[$name],'Process') }
    Remove-Item Function:\gh
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
