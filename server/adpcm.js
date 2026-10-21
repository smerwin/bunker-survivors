// Converts IMA ADPCM (format 0x11) WAV files, as used by many StarCraft II sounds, to 16-bit PCM WAV
// so browsers can decode them with decodeAudioData.
'use strict';

const STEP_TABLE = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

function parseWav(buf) {
  if (buf.toString('latin1', 0, 4) !== 'RIFF' || buf.toString('latin1', 8, 12) !== 'WAVE') return null;
  const info = { fmt: null, data: null };
  let p = 12;
  while (p + 8 <= buf.length) {
    const id = buf.toString('latin1', p, p + 4), size = buf.readUInt32LE(p + 4);
    const body = buf.subarray(p + 8, Math.min(buf.length, p + 8 + size));
    if (id === 'fmt ') info.fmt = body; else if (id === 'data') info.data = body;
    p += 8 + size + (size & 1);
  }
  return info.fmt ? info : null;
}

function isAdpcm(buf) {
  const w = parseWav(buf);
  return !!(w && w.fmt.readUInt16LE(0) === 0x11);
}

function decodeImaAdpcm(buf) {
  const w = parseWav(buf);
  if (!w || !w.data) throw new Error('not a WAV file');
  const fmt = w.fmt;
  const channels = fmt.readUInt16LE(2), sampleRate = fmt.readUInt32LE(4), blockAlign = fmt.readUInt16LE(12);
  const samplesPerBlock = fmt.length >= 20 ? fmt.readUInt16LE(18) : ((blockAlign - 4 * channels) * 8 / (4 * channels) + 1);
  const data = w.data;
  const blocks = Math.floor(data.length / blockAlign);
  const out = new Int16Array(blocks * samplesPerBlock * channels);
  let o = 0;
  for (let b = 0; b < blocks; b++) {
    const base = b * blockAlign;
    const pred = new Int32Array(channels), idx = new Int32Array(channels);
    for (let c = 0; c < channels; c++) {
      pred[c] = data.readInt16LE(base + c * 4);
      idx[c] = Math.min(88, Math.max(0, data[base + c * 4 + 2]));
      out[o + c] = pred[c];
    }
    o += channels;
    let written = 1;
    let p = base + 4 * channels;
    const end = base + blockAlign;
    // Data is interleaved in 4-byte words per channel: 8 nibbles for channel 0, 8 for channel 1, ...
    while (p < end && written < samplesPerBlock) {
      for (let c = 0; c < channels; c++) {
        for (let k = 0; k < 4 && p < end; k++, p++) {
          const byte = data[p];
          for (let n = 0; n < 2; n++) {
            const nib = n === 0 ? (byte & 15) : (byte >> 4);
            const step = STEP_TABLE[idx[c]];
            let diff = step >> 3;
            if (nib & 1) diff += step >> 2;
            if (nib & 2) diff += step >> 1;
            if (nib & 4) diff += step;
            if (nib & 8) diff = -diff;
            pred[c] = Math.max(-32768, Math.min(32767, pred[c] + diff));
            idx[c] = Math.max(0, Math.min(88, idx[c] + INDEX_TABLE[nib]));
            const s = written + k * 2 + n;
            if (s < samplesPerBlock) out[o + (k * 2 + n) * channels + c] = pred[c];
          }
        }
      }
      o += 8 * channels; written += 8;
    }
    o = (b + 1) * samplesPerBlock * channels;
  }
  // Build a PCM16 WAV.
  const pcm = Buffer.from(out.buffer, out.byteOffset, out.length * 2);
  const header = Buffer.alloc(44);
  header.write('RIFF', 0, 'latin1'); header.writeUInt32LE(36 + pcm.length, 4); header.write('WAVE', 8, 'latin1');
  header.write('fmt ', 12, 'latin1'); header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(sampleRate, 24); header.writeUInt32LE(sampleRate * channels * 2, 28); header.writeUInt16LE(channels * 2, 32); header.writeUInt16LE(16, 34);
  header.write('data', 36, 'latin1'); header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

module.exports = { isAdpcm, decodeImaAdpcm };
