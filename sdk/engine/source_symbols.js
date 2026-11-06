'use strict';
const {lexSource,signature}=require('./source-lex');

function maskTrivia(text) {
  return lexSource(text).code;
}

function parseSource(text, uri = '') {
  const {code,excluded} = lexSource(text);
  if(/\.scm(?:$|[?#])/i.test(uri))return {text,code,uri,classes:[],schema:undefined,parentSchema:undefined};
  const schema = /\b(\w+)\s+subschemaOf\s+(\w+)\b/.exec(code);
  const classes = [...code.matchAll(/\b([A-Za-z_]\w*)\s+subclassOf\s+([A-Za-z_]\w*)[^;]*;/g)].map(match => ({
    name: match[1], parent: match[2], kind: 'class', start: match.index, end: match.index + match[0].length,
    selection: match.index, members: [], regions: []
  }));
  const classByName=new Map();for(const cls of classes)if(!classByName.has(cls.name))classByName.set(cls.name,cls);
  const definitions = [...code.matchAll(/^\s*([A-Za-z_]\w*)\s+(completeDefinition|partialDefinition)\s*$/gm)];
  for (let i = 0; i < definitions.length; i++) {
    const definition = definitions[i];
    let cls = classByName.get(definition[1]);
    if(!cls){cls={name:definition[1],kind:'class',partial:true,start:definition.index,selection:definition.index,end:definition.index,members:[],regions:[]};classes.push(cls);classByName.set(cls.name,cls);}
    else cls.partial=definition[2]==='partialDefinition';
    const nextDefinition=definitions[i+1]?.index??code.length;
    const stop = code.slice(definition.index,nextDefinition).search(/^\s*(?:typeSources|databaseDefinitions)\s*$/m);
    const end = stop<0?nextDefinition:definition.index+stop;
    cls.regions.push([definition.index, end]);
    cls.end = Math.max(cls.end, end);
    const section = code.slice(definition.index, end);
    const fields=section.split(/\bjadeMethodDefinitions\b/)[0];
    for (const match of fields.matchAll(/^\s*([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)(?:\[(\d+)\])?([^;]*);/gm)) {
      const selection = definition.index + match.index + match[0].indexOf(match[1]);
      cls.members.push({ name: match[1], type: match[2], length:match[3]?Number(match[3]):undefined,access:/\bprotected\b/i.test(match[4])?2:/\breadOnly\b/i.test(match[4])?1:0,subaccess:/\bsubschemaHidden\b/i.test(match[4])?3:0,kind: 'property', start: selection,
        selection, end: definition.index + match.index + match[0].length });
    }
    for (const match of section.matchAll(/^\s*([A-Za-z_]\w*)\s*\([^)]*\)\s*(?::\s*\w+(?:\[\d+\])?)?[^;]*;/gm)) {
      if(['completeDefinition','partialDefinition'].includes(match[1]))continue;
      const selection = definition.index + match.index+match[0].indexOf(match[1]);
      const metadata=signature(match[0]);
      cls.members.push({ ...metadata,name: match[1], kind: 'method', start: selection, selection,
        end: definition.index + match.index + match[0].length, declaration: true });
    }
  }
  const sources = [...code.matchAll(/^\s*([A-Za-z_]\w*)\s*\(\s*\r?\n\s*jadeMethodSources\s*$/gm)];
  for (let i = 0; i < sources.length; i++) {
    const header = sources[i];
    const cls = classByName.get(header[1]);
    if (!cls) continue;
    const start = header.index + header[0].length;
    const end = sources[i + 1]?.index ?? code.length;
    cls.regions.push([header.index, end]);
    cls.end = Math.max(cls.end, end);
    const bodies=[...code.slice(start,end).matchAll(/^\s*([A-Za-z_]\w*)\s*\r?\n\s*\{/gm)];
    for (const match of bodies) {
      const selection = start + match.index + match[0].indexOf(match[1]);
      const bodyStart=start+match.index+match[0].length;let depth=1,bodyEnd=bodyStart;for(;bodyEnd<end;bodyEnd++){if(code[bodyEnd]==='{')depth++;else if(code[bodyEnd]==='}'&&!--depth){bodyEnd++;break;}}
      const header=code.slice(bodyStart,bodyEnd),metadata=signature(header),signatureEnd=bodyStart+Math.max(0,header.indexOf(';')+1);
      const method = { ...metadata,name: match[1], kind: 'method',declaration:false,start: selection, selection,bodyStart,signatureEnd,
        end: bodyEnd };
      const existing = cls.members.findIndex(item => item.kind === 'method' && item.name === method.name);
      if (existing >= 0) cls.members[existing] = {...cls.members[existing],...method};
      else cls.members.push(method);
    }
    cls.members.sort((a, b) => a.selection - b.selection);
  }
  const lineStarts=[0];for(let at=code.indexOf('\n');at>=0;at=code.indexOf('\n',at+1))lineStarts.push(at+1);
  return { text, code, excluded,lineStarts,uri, classes:classes.filter(cls=>!cls.partial||cls.members.length||definitions.some(def=>def[1]===cls.name&&def[2]==='partialDefinition')), schema: schema?.[1], parentSchema: schema?.[2] };
}

function sourcePosition(model,offset){const starts=model.lineStarts||[0];let low=0,high=starts.length;while(low+1<high){const middle=(low+high)>>1;if(starts[middle]<=offset)low=middle;else high=middle;}return {line:low,character:Math.max(0,offset-starts[low])};}

function resolveSymbol(models, current, offset, word) {
  if (current.code.slice(offset, offset + word.length) !== word) return undefined;
  const all = models.flatMap(model => model.classes.map(cls => ({ model, cls })));
  const parents = new Map(models.filter(model => model.schema).map(model => [model.schema, model.parentSchema]));
  function classNamed(name, namespace = current.schema) {
    const matches = all.filter(item => item.cls.name === name);
    if (namespace) {
      const seen = new Set();
      while (namespace && !seen.has(namespace)) {
        seen.add(namespace);
        const scoped = matches.filter(item => item.model.schema === namespace);
        if (scoped.length === 1) return scoped[0];
        if (scoped.length > 1) return undefined;
        namespace = parents.get(namespace);
      }
      return undefined;
    }
    return matches.find(item => item.model.uri === current.uri) || matches.find(item =>
      item.model.uri.replace(/\\/g, '/').split('/').at(-1)?.toLowerCase() === name.toLowerCase() + '.cls') || matches[0];
  }
  const owner = current.classes.find(cls => cls.regions.some(([start, end]) => start <= offset && offset < end)) || current.classes[0];
  const prefix = current.code.slice(Math.max(0, offset - 160), offset);
  const receiver = /([A-Za-z_]\w*)\s*\.\s*$/.exec(prefix)?.[1];
  let name = owner?.name;
  if (receiver && receiver !== 'self') {
    if (classNamed(receiver)) name = receiver;
    else {
      const method = owner?.members.find(item => item.kind === 'method' && item.start <= offset && offset <= item.end);
      const scope = current.code.slice(method?.start ?? 0, offset);
      const declaration = [...scope.matchAll(/\b([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)/g)].reverse().find(match => match[1] === receiver);
      name = declaration?.[2];
    }
  }
  const seen = new Set();
  let namespace = current.schema;
  while (name && !seen.has(namespace + '::' + name)) {
    seen.add(namespace + '::' + name);
    const target = classNamed(name, namespace);
    if (!target) break;
    const symbol = target.cls.members.find(item => item.name === word);
    if (symbol) return { model: target.model, cls: target.cls, symbol };
    name = target.cls.parent;
    namespace = target.model.schema;
  }
  if (!receiver) {
    const target = classNamed(word);
    if (target) return { ...target, symbol: target.cls };
  }
  return undefined;
}

module.exports = { maskTrivia, parseSource, resolveSymbol,sourcePosition };
