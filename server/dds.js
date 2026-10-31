// Minimal DDS (DXT1/DXT3/DXT5) decoder and PNG encoder, used to turn SC2 button icons into PNGs.
'use strict';
const zlib = require('zlib');

function decodeDDS(buf) {
  if (buf.toString('latin1', 0, 4) !== 'DDS ') throw new Error('not a DDS file');
  const height = buf.readUInt32LE(12), width = buf.readUInt32LE(16);
  const fourcc = buf.toString('latin1', 84, 88);
  const data = buf.subarray(128);
  const out = Buffer.alloc(width * height * 4);
  const bw = Math.max(1, (width + 3) >> 2), bh = Math.max(1, (height + 3) >> 2);
  const blockSize = fourcc === 'DXT1' ? 8 : 16;
  const c = [[0, 0, 0, 255], [0, 0, 0, 255], [0, 0, 0, 255], [0, 0, 0, 255]];
  const alphas = new Uint8Array(16);
  for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
    let p = (by * bw + bx) * blockSize;
    alphas.fill(255);
    if (fourcc === 'DXT3') {
      for (let i = 0; i < 8; i++) { const v = data[p + i]; alphas[i * 2] = (v & 15) * 17; alphas[i * 2 + 1] = (v >> 4) * 17; }
      p += 8;
    } else if (fourcc === 'DXT5') {
      const a0 = data[p], a1 = data[p + 1];
      const al = [a0, a1];
      if (a0 > a1) for (let i = 1; i <= 6; i++) al.push(Math.round(((7 - i) * a0 + i * a1) / 7));
      else { for (let i = 1; i <= 4; i++) al.push(Math.round(((5 - i) * a0 + i * a1) / 5)); al.push(0, 255); }
      let bits = 0n;
      for (let i = 0; i < 6; i++) bits |= BigInt(data[p + 2 + i]) << BigInt(8 * i);
      for (let i = 0; i < 16; i++) alphas[i] = al[Number((bits >> BigInt(3 * i)) & 7n)];
      p += 8;
    }
    const c0 = data.readUInt16LE(p), c1 = data.readUInt16LE(p + 2);
    const rgb = (v) => [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31];
    const [r0, g0, b0] = rgb(c0), [r1, g1, b1] = rgb(c1);
    c[0] = [r0, g0, b0, 255]; c[1] = [r1, g1, b1, 255];
    if (fourcc !== 'DXT1' || c0 > c1) {
      c[2] = [(2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255];
      c[3] = [(r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255];
    } else {
      c[2] = [(r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255];
      c[3] = [0, 0, 0, 0];
    }
    const idx = data.readUInt32LE(p + 4);
    for (let i = 0; i < 16; i++) {
      const x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
      if (x >= width || y >= height) continue;
      const col = c[(idx >> (2 * i)) & 3];
      const o = (y * width + x) * 4;
      out[o] = col[0]; out[o + 1] = col[1]; out[o + 2] = col[2];
      out[o + 3] = fourcc === 'DXT1' ? col[3] : alphas[i];
    }
  }
  return { width, height, data: out };
}

const CRC_TABLE = (() => { const t = new Int32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c; } return t; })();
function crc32(buf) { let c = -1; for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function encodePNG({ width, height, data }) {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) { raw[y * (width * 4 + 1)] = 0; data.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

// Particle textures with an alpha mask baked in (see m3fx extractFx opts.mask): "dir/diffuse+mask.dds" names the
// diffuse texture dir/diffuse.dds cut out by dir/mask.dds, and "dir/diffuse+mask@4x4.dds" the same with a 4 x 4 flipbook
// mask (the diffuse repeated in every cell). maskedParts returns { diffuse, mask, cols, rows }, or null for a plain
// texture; maskedPng multiplies the diffuse alpha by the mask's (its alpha, or its brightness if it has none).
function maskedParts(path) {
  const m = /^(.*\/)?([^/]+)\+([^/@]+?)(?:@(\d+)x(\d+))?(\.dds)$/.exec(path);
  if (!m) return null;
  const dir = m[1] || '';
  return { diffuse: dir + m[2] + '.dds', mask: dir + m[3] + '.dds', cols: +(m[4] || 1), rows: +(m[5] || 1) };
}
function maskedPng(diffuseBuf, maskBuf, cols = 1, rows = 1) {
  const d = decodeDDS(diffuseBuf), k = decodeDDS(maskBuf), q = k.data;
  const cells = cols * rows > 1, W = cells ? k.width : d.width, H = cells ? k.height : d.height, out = Buffer.alloc(W * H * 4);
  const cw = W / cols, ch = H / rows;
  let flat = true;
  for (let i = 3; i < q.length && flat; i += 4) flat = q[i] === 255;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    const di = (Math.floor((y % ch) / ch * d.height) * d.width + Math.floor((x % cw) / cw * d.width)) * 4;
    const j = (Math.floor(y * k.height / H) * k.width + Math.floor(x * k.width / W)) * 4;
    const a = flat ? (q[j] * 0.3 + q[j + 1] * 0.59 + q[j + 2] * 0.11) : q[j + 3];
    out[i] = d.data[di]; out[i + 1] = d.data[di + 1]; out[i + 2] = d.data[di + 2];
    out[i + 3] = Math.round(d.data[di + 3] * a / 255);
  }
  return encodePNG({ width: W, height: H, data: out });
}

module.exports = { decodeDDS, encodePNG, ddsToPng: (buf) => encodePNG(decodeDDS(buf)), maskedParts, maskedPng };
