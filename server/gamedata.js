// The game's own data tables (models, effects, sounds) live in client/js/units.js so the browser and the tools share
// one copy. The file only assigns into window.BS, so it is evaluated in a bare sandbox here.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

function loadGameData() {
  const ctx = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'client', 'js', 'units.js'), 'utf8'), ctx);
  return ctx.window.BS;
}

module.exports = { loadGameData };
