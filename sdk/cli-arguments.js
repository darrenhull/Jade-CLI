'use strict';
const fs=require('node:fs'),path=require('node:path');
function discover(cwd){for(let folder=cwd;;folder=path.dirname(folder)){const file=path.join(folder,'jade.json');if(fs.existsSync(file))return file;if(path.dirname(folder)===folder)return undefined;}}
function prepareCommand(command,argv,cwd=process.cwd()){
 const values={},files=[];let positional=false;
 for(let i=0;i<argv.length;i++){const arg=argv[i];if(arg==='--'){positional=true;continue;}if(!positional&&arg.startsWith('--')){const key=arg.slice(2);if(key==='json'){values[key]=true;continue;}if(!['config','workspace','program','query','limit','offset','target','args'].includes(key))throw Error('Unknown option: '+arg);if(!argv[i+1]||argv[i+1].startsWith('--'))throw Error('Missing value for '+arg);values[key]=argv[++i];}else files.push(path.resolve(cwd,arg));}
 const configFile=values.config?path.resolve(cwd,values.config):discover(cwd);
 if(!configFile&&['build','test'].includes(command))throw Error('No jade.json found in this folder or its parents. Create jade.json or supply --config <file>.');
 const config=configFile?JSON.parse(fs.readFileSync(configFile,'utf8').replace(/^\uFEFF/,'')):{};
 const base=configFile?path.dirname(configFile):cwd;
 for(const key of ['installPath','databasePath','iniPath','storagePath','program','workspace'])if(config[key])config[key]=path.resolve(base,config[key]);
 for(const key of ['workspaceRoots','files'])if(config[key])config[key]=config[key].map(file=>path.resolve(base,file));
 if(!config.workspaceRoots)config.workspaceRoots=[base];
 if(values.workspace)config.workspaceRoots=[path.resolve(cwd,values.workspace)];
 if(values.program){if(files.length)throw Error('Use positional source files or --program, not both.');files.push(path.resolve(cwd,values.program));}
 if(files.length&&!['build','test'].includes(command))throw Error('Source file arguments are supported by build and test.');
 const projectBuild=command==='build'&&!files.length;
 const selected=[...new Set(files.length?files:!projectBuild&&config.program?[config.program]:[])];
 if(['build','test'].includes(command)){if(!selected.length&&!projectBuild)throw Error('Supply at least one source file to '+command+'.');for(const file of selected)if(!fs.existsSync(file)||!fs.statSync(file).isFile())throw Error('Source is not a file: '+file);}
 return {command,values,config,configFile,files:selected,projectBuild};
}
async function runCommand(prepared,execute){
 const {command,values,config}=prepared;let files=prepared.files;
 if(prepared.projectBuild){
  if(config.workspaceRoots.length!==1)throw Error('Project builds currently require one workspace root in jade.json.');
  const paths=await require('./engine/native-project').listProjectFiles(config.workspaceRoots[0]);
  files=paths.filter(file=>path.extname(file).toLowerCase()==='.cls').sort();
  if(!files.length)files=paths.filter(file=>path.extname(file).toLowerCase()==='.scm'||path.basename(file).toLowerCase()==='schemadefinitions.sec').sort();
  if(!files.length)throw Error('No Jade source files found in '+config.workspaceRoots[0]+'.');
 }
 const request={command,config,query:values.query,limit:values.limit,offset:values.offset,target:values.target,args:values.args?JSON.parse(values.args):{}};
 if(!['build','test'].includes(command))return execute(request);
 if(command==='build')return execute({...request,config:{...config,program:files[0],files:prepared.projectBuild?files:[...new Set([...(config.files||[]),...files])]}});
 const workspaceFiles=(await Promise.all(config.workspaceRoots.map(root=>require('./engine/native-project').listProjectFiles(root))))
  .flat().filter(file=>path.extname(file).toLowerCase()==='.cls').sort();
 const dependencyFiles=[...new Set([...(config.files||[]),...workspaceFiles,...files])];
 const results=[];
 for(const file of files){const result=await execute({...request,config:{...config,program:file,files:dependencyFiles}});results.push({file,result});if(result.cancelled)break;}
 if(files.length===1)return results[0].result;
 return {passed:results.every(row=>row.result.passed!==false),code:results.find(row=>row.result.passed===false)?.result.code||0,cancelled:results.some(row=>row.result.cancelled),results};
}
module.exports={prepareCommand,runCommand};
