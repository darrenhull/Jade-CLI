'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawnSync}=require('node:child_process');
function fixture(t){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-release-ancestry-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const git=(...args)=>{const result=spawnSync('git',args,{cwd:root,encoding:'utf8',windowsHide:true});assert.equal(result.status,0,result.stderr);return result.stdout.trim();};
 git('init','-b','main');
 const commit=text=>{fs.writeFileSync(path.join(root,'source.txt'),text);git('add','source.txt');git('-c','user.name=Release Policy Test','-c','user.email=test@example.invalid','-c','commit.gpgsign=false','commit','-m',text);return git('rev-parse','HEAD');};
 const approved=commit('approved source');git('update-ref','refs/remotes/origin/main',approved);git('switch','-c','codex/unmerged-feature');const unmerged=commit('unreviewed source');
 const workflow=fs.readFileSync(path.resolve(__dirname,'../../.github/workflows/release-ancestry.yml'),'utf8');
 const marker='        run: |\n',start=workflow.indexOf(marker);assert.notEqual(start,-1,'the policy workflow must contain its executable guard');
 const script=workflow.slice(start+marker.length).split(/\r?\n/).map(line=>line.replace(/^          /,'')).join('\n');
 const scriptFile=path.join(root,'guard.ps1');fs.writeFileSync(scriptFile,script);
 const verify=sha=>spawnSync(process.platform==='win32'?path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe'):'pwsh',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',scriptFile],{cwd:root,encoding:'utf8',windowsHide:true,env:{...process.env,GITHUB_SHA:sha,DEFAULT_BRANCH:'main'}});
 return {approved,unmerged,verify};
}
test('verify permits a tested tag on the approved branch',t=>{const f=fixture(t),result=f.verify(f.approved);assert.ifError(result.error);assert.equal(result.status,0,result.stderr);});
test('verify rejects a tag on an unmerged feature commit',t=>{const f=fixture(t),result=f.verify(f.unmerged);assert.ifError(result.error);assert.notEqual(result.status,0);assert.match(result.stderr,/approved default branch/);});
