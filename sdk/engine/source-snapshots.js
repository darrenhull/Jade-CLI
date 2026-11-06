'use strict';
const fs = require('node:fs');
const path = require('node:path');

// A baseline remains inspectable without shadowing edited workspace classes.
// Opening the baseline itself as a workspace makes it a normal source root.
function isNestedSnapshot(uri, workspace) {
  const root = workspace.getWorkspaceFolder?.(uri)?.uri.fsPath;
  if (!root) return false;
  let directory = path.dirname(uri.fsPath);
  const workspaceRoot = path.resolve(root);
  while (directory !== workspaceRoot && directory !== path.dirname(directory)) {
    if (fs.existsSync(path.join(directory, '.jade-extraction'))) return true;
    directory = path.dirname(directory);
  }
  return false;
}
module.exports = { isNestedSnapshot };
