'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process');

function fixture(t,{existing=[],author='github-actions[bot]'}={}){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-agent-pr-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const repo='darrenhull/Jade-CLI',head='codex/agent/cli-pipelines',base='codex/workspace-split',marker=path.join(root,'must-not-execute');
 const originalBody='Existing review context with literal $(Set-Content -LiteralPath "'+marker+'" hacked), backticks ` and quotes " preserved.';
 const current={number:2,html_url:'https://github.com/'+repo+'/pull/2',user:{login:author,type:author==='github-actions[bot]'?'Bot':'User'},head:{ref:head,repo:{full_name:repo}},base:{ref:base},requested_reviewers:[]};
 const config=path.join(root,'fixture.json'),calls=path.join(root,'calls.jsonl');fs.writeFileSync(config,JSON.stringify({Existing:existing,Current:current,Original:{number:1,title:'Build and test Jade tools',body:originalBody,html_url:'https://github.com/'+repo+'/pull/1'},Commit:{commit:{message:'Add a future Jade feature\n\n'+originalBody}}}));
 const workflow=fs.readFileSync(path.resolve(__dirname,'../../.github/workflows/open-agent-pr.yml'),'utf8').replace(/\r\n/g,'\n');
 const markerText='        run: |\n',start=workflow.indexOf(markerText);assert.notEqual(start,-1);
 const body=workflow.slice(start+markerText.length).split('\n').map(line=>line.replace(/^          /,'')).join('\n');
 const fake=String.raw`$script:Fixture = Get-Content -LiteralPath $env:AGENT_PR_FIXTURE -Raw | ConvertFrom-Json
function gh {
  $global:LASTEXITCODE = 0
  $method = 'GET'
  $methodIndex = [array]::IndexOf($args, '--method')
  if ($methodIndex -ge 0) { $method = $args[$methodIndex + 1] }
  $endpoint = @($args | Where-Object { $_ -like 'repos/*' })[0]
  $payload = $null
  $inputIndex = [array]::IndexOf($args, '--input')
  if ($inputIndex -ge 0) { $payload = Get-Content -LiteralPath $args[$inputIndex + 1] -Raw | ConvertFrom-Json }
  $call = @{ Method = $method; Endpoint = $endpoint; Payload = $payload }
  [IO.File]::AppendAllText($env:AGENT_PR_CALLS, (ConvertTo-Json -InputObject $call -Depth 30 -Compress) + [Environment]::NewLine)
  if ($endpoint -like '*/pulls?state=*') { ConvertTo-Json -InputObject @($script:Fixture.Existing) -Depth 30 -Compress; return }
  if ($endpoint -like '*/pulls/1' -and $method -eq 'GET') { $script:Fixture.Original | ConvertTo-Json -Depth 30 -Compress; return }
  if ($endpoint -like '*/commits/*' -and $method -eq 'GET') { $script:Fixture.Commit | ConvertTo-Json -Depth 30 -Compress; return }
  if ($endpoint -like '*/pulls' -and $method -eq 'POST') { $script:Fixture.Current | ConvertTo-Json -Depth 30 -Compress; return }
  if ($endpoint -like '*/pulls/2' -and $method -eq 'GET') { $script:Fixture.Current | ConvertTo-Json -Depth 30 -Compress; return }
  if ($endpoint -like '*/pulls/2/requested_reviewers' -and $method -eq 'POST') { $script:Fixture.Current.requested_reviewers = @(@{login='darrenhull'}); $script:Fixture.Current | ConvertTo-Json -Depth 30 -Compress; return }
  throw ('Unexpected mock GitHub request: ' + $method + ' ' + $endpoint)
}
`;
 const script=path.join(root,'workflow.ps1');fs.writeFileSync(script,fake+'\n'+body);
 const run=overrides=>spawnSync(process.platform==='win32'?path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe'):'pwsh',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',script],{cwd:root,windowsHide:true,encoding:'utf8',env:{...process.env,GITHUB_EVENT_NAME:'push',GITHUB_ACTOR:'darrenhull',GITHUB_REPOSITORY:repo,GITHUB_REF:'refs/heads/'+head,GITHUB_SHA:'a'.repeat(40),GITHUB_RUN_ID:'12345',DEFAULT_BRANCH:base,SOURCE_PR:'1',REVIEWER:'darrenhull',RUNNER_TEMP:root,GITHUB_OUTPUT:path.join(root,'outputs'),GITHUB_STEP_SUMMARY:path.join(root,'summary'),AGENT_PR_FIXTURE:config,AGENT_PR_CALLS:calls,...overrides}});
 return {run,marker,originalBody,head,base,current,calls:()=>fs.existsSync(calls)?fs.readFileSync(calls,'utf8').trim().split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line)):[]};
}

test('open preserves original context as data and requests human review on a verified bot PR',t=>{const f=fixture(t),result=f.run();assert.ifError(result.error);assert.equal(result.status,0,result.stderr);const created=f.calls().find(call=>call.Method==='POST'&&call.Endpoint.endsWith('/pulls'));assert.equal(created.Payload.title,'Build and test Jade tools');assert.equal(created.Payload.head,f.head);assert.equal(created.Payload.base,f.base);assert.ok(created.Payload.body.includes(f.originalBody));assert.match(created.Payload.body,/Prepared by Codex on behalf of @darrenhull/);assert.match(created.Payload.body,/GitHub Actions App/);assert.equal(fs.existsSync(f.marker),false);assert.ok(f.calls().some(call=>call.Endpoint.endsWith('/requested_reviewers')&&call.Payload.reviewers[0]==='darrenhull'));});
test('open reuses an existing bot PR without creating another',t=>{const existing={number:2,user:{login:'github-actions[bot]'}};const f=fixture(t,{existing:[existing]}),result=f.run();assert.equal(result.status,0,result.stderr);assert.equal(f.calls().filter(call=>call.Method==='POST'&&call.Endpoint.endsWith('/pulls')).length,0);});
test('open rejects an existing human PR rather than changing its ownership',t=>{const f=fixture(t,{existing:[{number:2,user:{login:'darrenhull'}}]}),result=f.run();assert.notEqual(result.status,0);assert.match(result.stderr,/human-authored/);assert.equal(f.calls().filter(call=>call.Method==='POST').length,0);});
test('open refuses untrusted actors, repositories and branch prefixes before any API call',t=>{for(const overrides of [{GITHUB_ACTOR:'someone-else'},{GITHUB_REPOSITORY:'another-owner/Jade-CLI'},{GITHUB_REPOSITORY:'darrenhull/unrelated'},{GITHUB_REF:'refs/heads/codex/ci-releases'}]){const f=fixture(t),result=f.run(overrides);assert.notEqual(result.status,0);assert.equal(f.calls().length,0);}});
test('open fails if the newly created PR is not authored by the GitHub Actions bot',t=>{const f=fixture(t,{author:'darrenhull'}),result=f.run();assert.notEqual(result.status,0);assert.match(result.stderr,/bot author/);assert.equal(f.calls().filter(call=>call.Endpoint.endsWith('/requested_reviewers')).length,0);});
test('open uses the final commit for future topics when no original PR is selected',t=>{const f=fixture(t),result=f.run({SOURCE_PR:'0'});assert.equal(result.status,0,result.stderr);const created=f.calls().find(call=>call.Method==='POST'&&call.Endpoint.endsWith('/pulls'));assert.equal(created.Payload.title,'Add a future Jade feature');assert.ok(created.Payload.body.includes(f.originalBody));assert.match(created.Payload.body,/actions\/runs\/12345/);assert.equal(f.calls().some(call=>call.Endpoint.endsWith('/pulls/1')),false);assert.equal(fs.existsSync(f.marker),false);});
