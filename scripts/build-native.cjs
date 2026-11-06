'use strict';
const fs=require('node:fs'),path=require('node:path'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),jade=process.env.JADE_HOME||process.env.JADE_INSTALL;
if(process.platform!=='win32')throw Error('Native Jade helpers require Windows x64.');
if(!jade||!fs.existsSync(path.join(jade,'include','jomcalls.h')))throw Error('Set JADE_HOME to the installed Jade SDK/runtime containing include/jomcalls.h.');
if(spawnSync('where.exe',['cl.exe'],{windowsHide:true}).status!==0)throw Error('Run from an x64 Visual Studio Developer Command Prompt with the C++20 toolchain.');
const source=path.join(root,'native-src'),out=path.join(root,'sdk','native'),build=path.join(root,'build');
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(build,{recursive:true});
const targets=[
 ['jade-native-helper',['main','model','jade_adapter','cache_refresh','sdk_abi'].map(n=>'native-helper/'+n+'.cpp'),['bcrypt.lib']],
 ['jade-debug-helper',['debugger/debug_host.cpp'],['bcrypt.lib']],
 ['jade-inspect-helper',['inspector/inspector.cpp'],[]],
 ['jade-test-helper',['test-runner/test_host.cpp'],[]]
];
for(const [name,files,libraries]of targets){
 const directory=path.join(build,name);fs.mkdirSync(directory,{recursive:true});
 const args=['/nologo','/std:c++20','/EHsc','/O2','/MD','/DUNICODE','/D_UNICODE','/utf-8','/I'+path.join(jade,'include'),'/I'+path.join(source,'third_party'),...files.map(f=>path.join(source,f)),'/Fo'+directory+path.sep,'/Fe'+path.join(out,name+'.exe'),...(libraries.length?['/link',...libraries]:[])];
 const result=spawnSync('cl.exe',args,{cwd:directory,stdio:'inherit',windowsHide:true});
 if(result.status!==0){process.exitCode=result.status??1;break;}
}
