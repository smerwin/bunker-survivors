// Publishes the web edition to GitHub Pages: builds the SC2 worker, then commits client/ (code only, never any
// StarCraft II files) on top of the gh-pages branch, which Pages serves at https://smerwin.github.io/bunker-survivors/.
// Usage: npm run deploy:web
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.join(__dirname, '..');
const git = (args, cwd, quiet) => execFileSync('git', args, { cwd, stdio: ['ignore', 'pipe', quiet ? 'ignore' : 'inherit'] }).toString().trim();
execFileSync(process.execPath, [path.join(__dirname, 'build-web.js')], { stdio: 'inherit' });
const origin = git(['remote', 'get-url', 'origin'], root);
const rev = git(['rev-parse', '--short', 'HEAD'], root);
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'bs-pages-'));
git(['init', '-q', '-b', 'gh-pages'], out);
// Continue the published history when there is one, so every deploy is a plain fast-forward push.
try { git(['fetch', '-q', origin, 'gh-pages'], out, true); git(['reset', '-q', '--soft', 'FETCH_HEAD'], out); } catch (e) { /* first deploy */ }
fs.cpSync(path.join(root, 'client'), out, { recursive: true });
fs.writeFileSync(path.join(out, '.nojekyll'), '');
git(['add', '-A'], out);
const who = ['-c', 'user.name=' + git(['config', 'user.name'], root), '-c', 'user.email=' + git(['config', 'user.email'], root)];
git([...who, 'commit', '-q', '--allow-empty', '-m', 'Web edition from ' + rev], out);
git(['push', '-q', origin, 'gh-pages'], out);
fs.rmSync(out, { recursive: true, force: true });
console.log('Published ' + rev + ' to the gh-pages branch.');
