// Sound playback using StarCraft II sound files streamed from the companion server.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  class GameAudio {
    constructor() {
      this.ctx = null;
      this.buffers = new Map();
      this.lastPlay = new Map();
      this.muted = false;
      this.volume = 0.8;
      this.voices = 0;
      try { this.muted = localStorage.getItem('bs.muted') === '1'; } catch (e) { /* ignore */ }
    }

    // Must be called from a user gesture on mobile.
    unlock() {
      if (!this.ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return;
        this.ctx = new AC();
        this.master = this.ctx.createGain();
        this.master.gain.value = this.muted ? 0 : this.volume;
        this.master.connect(this.ctx.destination);
        this.preload();
      }
      if (this.ctx.state === 'suspended') this.ctx.resume();
    }

    setMuted(m) {
      this.muted = m;
      if (this.master) this.master.gain.value = m ? 0 : this.volume;
      try { localStorage.setItem('bs.muted', m ? '1' : '0'); } catch (e) { /* ignore */ }
    }

    preload() {
      for (const list of Object.values(BS.SOUNDS)) for (const path of list) this.load(path);
    }

    load(path) {
      if (this.buffers.has(path)) return this.buffers.get(path);
      const p = fetch('asset/' + path).then(r => r.ok ? r.arrayBuffer() : Promise.reject(new Error(r.status)))
        .then(ab => new Promise((res, rej) => this.ctx.decodeAudioData(ab, res, rej)))
        .catch(() => null);
      this.buffers.set(path, p);
      return p;
    }

    // Plays one random variant of a sound key. Throttled per key so volleys do not stack.
    play(key, opts) {
      if (!this.ctx || this.muted) return;
      const list = BS.SOUNDS[key];
      if (!list) return;
      const now = this.ctx.currentTime;
      const minGap = (opts && opts.gap) || 0.07;
      if (now - (this.lastPlay.get(key) || -1) < minGap) return;
      if (this.voices > 24) return;
      this.lastPlay.set(key, now);
      const path = list[Math.floor(Math.random() * list.length)];
      const p = this.load(path);
      p.then(buf => {
        if (!buf || !this.ctx) return;
        const src = this.ctx.createBufferSource();
        src.buffer = buf;
        src.playbackRate.value = (opts && opts.rate) || (0.96 + Math.random() * 0.08);
        const g = this.ctx.createGain();
        g.gain.value = (opts && opts.vol) || 0.6;
        src.connect(g); g.connect(this.master);
        this.voices++;
        src.onended = () => { this.voices--; };
        src.start();
      });
    }
  }

  BS.audio = new GameAudio();
})(window.BS);
