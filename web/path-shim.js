// The slice of Node's path the shared server code uses at load time.
exports.join = (...a) => a.join('/').replace(/\/+/g, '/');
exports.dirname = p => p.replace(/\/[^/]*$/, '');
exports.basename = p => p.replace(/^.*\//, '');
exports.extname = p => { const m = /\.[^./]+$/.exec(p); return m ? m[0] : ''; };
