'use strict';
const path=require('node:path');
const {StandalonePackager}=require('../sdk/automation/StandalonePackager');
new StandalonePackager({root:path.resolve(__dirname,'..'),distributionName:'jade-cli-win-x64',executableName:'jade-cli.exe',entrypoint:'bin/jade-cli.js',runtimeFolders:['bin','sdk'],runtimeFiles:['README.md','ThirdPartyNotices.txt','docs/architecture.drawio','docs/architecture.drawio.svg'],nodeLicense:'licenses/node-v24.22.1-LICENSE.txt'}).build({inject:require('postject').inject}).catch(error=>{console.error(error.message);process.exitCode=1;});