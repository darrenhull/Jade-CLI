'use strict';
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
function fixture(t){const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-args-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));fs.mkdirSync(path.join(root,'src'));fs.writeFileSync(path.join(root,'jade.json'),JSON.stringify({installPath:'runtime',workspaceRoots:['.'],program:'src/A.cls'}));for(const name of ['A','B'])fs.writeFileSync(path.join(root,'src',name+'.cls'),'source');return root;}
module.exports={fixture};
