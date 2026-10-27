// The slice of Node's zlib the shared server code uses, on pako (already a dependency of mdx-m3-viewer).
const pako = require('pako');
exports.inflateSync = b => Buffer.from(pako.inflate(b));
exports.deflateSync = b => Buffer.from(pako.deflate(b));
