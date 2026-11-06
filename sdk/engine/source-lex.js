'use strict';
function lexSource(text){
  const code=text.split(''),excluded=[];let i=0;
  while(i<text.length){const start=i;let kind,closed=false;
    if(text.startsWith('//',i)){kind='comment';i=text.indexOf('\n',i);if(i<0)i=text.length;}
    else if(text.startsWith('/*',i)){kind='comment';const end=text.indexOf('*/',i+2);closed=end>=0;i=end<0?text.length:end+2;}
    else if(['"',"'",'`'].includes(text[i])){const quote=text[i++];kind=quote==='`'?'documentation':'string';
      while(i<text.length){if(text[i]==='\\'&&quote!=='`'){i=Math.min(text.length,i+2);continue;}if(text[i]===quote){if(quote!=='`'&&text[i+1]===quote){i+=2;continue;}i++;closed=true;break;}i++;}
    }else{i++;continue;}
    excluded.push({start,end:i,kind,closed,quote:text[start]});for(let at=start;at<i;at++)if(code[at]!=='\r'&&code[at]!=='\n')code[at]=' ';
  }return {code:code.join(''),excluded};
}
function excludedAt(model,offset){return (model.excluded||[]).find(range=>range.start<=offset&&(offset<range.end||(!range.closed&&offset===range.end)));}
function parseParameters(text){const parameters=[];const expression=/((?:\b[A-Za-z_]\w*\s*,\s*)*\b[A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)(?:\[(\d+)\])?(?:\s+(input|output|io|constant))?/g;
  for(const match of text.matchAll(expression))for(const name of match[1].split(',').map(value=>value.trim()))parameters.push({name,type:match[2],length:match[3]?Number(match[3]):undefined,usage:match[4]||'constant'});return parameters;
}
function signature(text){const match=/^\s*([A-Za-z_]\w*)\s*\(([^)]*)\)\s*(?::\s*([A-Za-z_]\w*)(?:\[(\d+)\])?)?\s*([^;]*)(?:;|$)/.exec(text);if(!match)return;
  const flags=match[5].replace(/\bnumber\s*=[\s\S]*$/,'').trim(),has=word=>new RegExp('\\b'+word+'\\b','i').test(flags);
  return {name:match[1],parameters:parseParameters(match[2]),returnType:match[3],flags,access:has('protected')?2:has('readOnly')?1:0,subaccess:has('subschemaHidden')?3:has('subschemaProtected')?2:0,typeMethod:has('typeMethod'),updating:has('updating')};
}
module.exports={lexSource,excludedAt,parseParameters,signature};
