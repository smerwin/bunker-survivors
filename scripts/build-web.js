// Builds the web edition's worker (web/sc2-worker.js plus the shared server modules) into client/gen/sc2-worker.js.
// With it the client runs from any static host: the visitor's own StarCraft II folder stands in for the server.
'use strict';
const path = require('path');
const root = path.join(__dirname, '..');
require('esbuild').build({
  entryPoints: [path.join(root, 'web', 'sc2-worker.js')],
  outfile: path.join(root, 'client', 'gen', 'sc2-worker.js'),
  bundle: true, format: 'iife', platform: 'browser', target: 'es2020', minify: true, sourcemap: false, logLevel: 'info',
  inject: [path.join(root, 'web', 'buffer-shim.js')],
  define: { __dirname: '"/"', 'require.main': 'undefined' },
  alias: { zlib: './web/zlib-shim.js', path: './web/path-shim.js', fs: './web/empty.js', os: './web/empty.js', https: './web/empty.js', vm: './web/empty.js' },
}).catch(() => process.exit(1));
