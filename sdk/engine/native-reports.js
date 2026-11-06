"use strict";
const { parseSource, sourcePosition } = require("./source_symbols");
function compilerDiagnostics(output, source, text, staged) {
  output = output.replace(/^\d{4}\/\d{2}\/\d{2} \d{2}:\d{2}:\d{2}(?:\.\d+)?: /gm, '');
  const model = parseSource(text, source),
    diagnostics = [];
  for (const match of output.matchAll(
    /Compile error (\d+) \(([^\r\n]+)\) in file ([^\r\n]+) in (\w+)::(\w+)::(\w+) at line (\d+):/g,
  )) {
    if (staged && match[3].toLowerCase() !== staged.toLowerCase()) continue;
    const member = model.classes
      .find((row) => row.name === match[5])
      ?.members.find((row) => row.name === match[6]);
    const line = member
      ? member.bodyStart === undefined || Number(match[7]) === 0
        ? sourcePosition(model, member.selection).line + 1
        : sourcePosition(model, member.bodyStart).line + Number(match[7])
      : 1;
    diagnostics.push({
      source,
      schema: match[4],
      owner: match[5],
      method: match[6],
      compiler_error: Number(match[1]),
      message: match[2],
      line,
      column: 1,
    });
  }
  for (const segment of output.split(/(?=Loading schema file )/)) {
    const file = /^Loading schema file (.+) \.\.\./.exec(segment)?.[1];
    if (!file || (staged && file.toLowerCase() !== staged.toLowerCase()))
      continue;
    for (const match of segment.matchAll(
      /Compile Error (\d+)\s*-\s*([^\r\n]+)(?:\s*\r?\nAt line (\d+):)?/gi,
    ))
      diagnostics.push({
        source,
        compiler_error: Number(match[1]),
        message: match[2],
        line: Math.max(1, Number(match[3] || 1)),
        column: 1,
      });
  }
  return diagnostics;
}
function nativeTestReport(log, selection) {
  const model = selection.text
    ? parseSource(selection.text, selection.source)
    : undefined;
  const cls = model?.classes.find((row) => row.name === selection.owner);
  const names = selection.methods?.length
    ? selection.methods
    : (cls?.members || [])
        .filter(
          (row) =>
            row.bodyStart !== undefined &&
            /\bunitTest\b/i.test(row.flags || "") &&
            !/\bunitTestIgnore\b/i.test(row.flags || ""),
        )
        .map((row) => row.name);
  const summary = [
    ...log.matchAll(
      /total tests=(\d+) failed=(\d+) skipped=(\d+) succeeded=(\d+)/g,
    ),
  ].at(-1);
  const failures = new Map(),
    unidentified = [];
  const entries = [...log.matchAll(/ : (\w+)::(\w+)::(\w+): ([^\r\n]+)/g)];
  for (let index = 0; index < entries.length; index++) {
    const match = entries[index],
      block = log.slice(match.index, entries[index + 1]?.index ?? log.length);
    const stackStart = block.indexOf("Call Stack:");
    const frames =
      stackStart < 0
        ? []
        : [
            ...block
              .slice(stackStart)
              .matchAll(/(\w+)::(\w+)::(\w+)\((\d+)\)/g),
          ];
    let method =
      match[1] === selection.schema &&
      match[2] === selection.owner &&
      names.includes(match[3])
        ? match[3]
        : undefined;
    if (!method) {
      const callers = new Set(
        frames
          .filter(
            (frame) =>
              frame[1] === selection.schema &&
              frame[2] === selection.owner &&
              names.includes(frame[3]),
          )
          .map((frame) => frame[3]),
      );
      if (callers.size === 1) method = [...callers][0];
    }
    if (
      !method &&
      names.length === 1 &&
      summary &&
      Number(summary[1]) === 1 &&
      Number(summary[2]) === 1
    )
      method = names[0];
    const qualified =
      match[1] + "::" + match[2] + "::" + match[3] + ": " + match[4];
    if (!method) {
      unidentified.push(qualified);
      continue;
    }
    const actualMember =
      match[1] === selection.schema && match[2] === selection.owner
        ? cls?.members.find((row) => row.name === match[3])
        : undefined;
    const member =
      actualMember || cls?.members.find((row) => row.name === method);
    const direct =
      match[1] === selection.schema &&
      match[2] === selection.owner &&
      match[3] === method;
    const row = {
      owner: selection.owner,
      method,
      status: "failed",
      message: direct ? match[4] : qualified,
      source: selection.source,
      line: member ? sourcePosition(model, member.selection).line + 1 : 1,
      column: 1,
    };
    const frame = frames.find(
      (frame) =>
        frame[1] === selection.schema &&
        frame[2] === selection.owner &&
        frame[3] === member?.name,
    );
    if (frame && member?.bodyStart !== undefined) {
      const start =
        member.bodyStart +
        (model.text.slice(member.bodyStart).match(/^\s*/)?.[0].length || 0);
      row.line =
        sourcePosition(model, start + Math.max(0, Number(frame[4]) - 1)).line +
        1;
    }
    const prior = failures.get(method);
    if (prior && prior.message !== row.message)
      row.message = prior.message + "\n" + row.message;
    failures.set(method, row);
  }
  const complete = Boolean(
    !unidentified.length &&
    summary &&
    Number(summary[1]) === names.length &&
    Number(summary[2]) === failures.size &&
    Number(summary[3]) === 0 &&
    Number(summary[4]) + failures.size === names.length,
  );
  return {
    tests_completed: complete,
    cleanup_complete: complete,
    tests_selected: names.length,
    failures: summary ? Number(summary[2]) : undefined,
    results: complete
      ? names.map(
          (method) =>
            failures.get(method) || {
              owner: selection.owner,
              method,
              status: "passed",
            },
        )
      : [],
    ...(complete
      ? {}
      : {
          error:
            "Native test log did not identify a complete result for every selected method. See Jade Tests output." +
            (unidentified.length
              ? " Native failure: " + [...new Set(unidentified)].join("; ")
              : ""),
        }),
  };
}
module.exports = { compilerDiagnostics, nativeTestReport };
