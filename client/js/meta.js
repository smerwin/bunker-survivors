// Progress between runs: vespene earned by every run, perks bought with it at the Hyperion (BS.META.perks), and how
// many times each shop card has been bought over all runs, which unlocks new cards (BS.CARDS unlock). Kept in
// localStorage; everything still works (from scratch each visit) where storage is blocked.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const KEY = 'bs.meta';
  let st = null;

  function load() {
    if (st) return st;
    try { st = JSON.parse(localStorage.getItem(KEY) || 'null'); } catch (e) { st = null; }
    st = Object.assign({ gas: 0, perks: {}, buys: {}, best: 0, rung: 0 }, st || {});
    return st;
  }
  function save() { try { localStorage.setItem(KEY, JSON.stringify(st)); } catch (e) { /* private mode */ } }

  const Meta = {
    state: () => load(),
    perk: id => load().perks[id] || 0,
    perkDef: id => BS.META.perks.find(p => p.id === id),
    perkCost(id) { const p = Meta.perkDef(id), l = Meta.perk(id); return l < p.costs.length ? p.costs[l] : null; },
    buyPerk(id) {
      const s = load(), cost = Meta.perkCost(id);
      if (cost === null || s.gas < cost) return false;
      s.gas -= cost; s.perks[id] = (s.perks[id] || 0) + 1; save();
      return true;
    },
    // The ladder rung reached (BS.LADDER): forced (playtesting, ?rung=N or ?tech=max) or earned.
    force: null,
    rung() { const top = BS.LADDER.length - 1; return Math.min(top, Meta.force !== null ? Meta.force : load().rung || 0); },
    // The rung on which a Terran unit or ability (an armoury item, ability or card id) or a zerg strain joins the fight.
    terranRung: id => { const i = BS.LADDER.findIndex(r => r.terran.includes(id)); return i < 0 ? 0 : i; },
    zergRung: id => { const i = BS.LADDER.findIndex(r => r.zerg.includes(id)); return i < 0 ? 0 : i; },
    // Has this card joined the pool? (No unlock condition: from the start.)
    unlocked(card) {
      const u = card.unlock, s = load();
      if (!u) return true;
      if (u.best && s.best < u.best) return false;
      if (u.buys && (s.buys[u.buys[0]] || 0) < u.buys[1]) return false;
      return true;
    },
    unlockText(card) {
      const u = card.unlock;
      if (!u) return '';
      if (u.best) return 'Clear wave ' + u.best;
      const c = BS.CARDS.find(x => x.id === u.buys[0]);
      return 'Buy ' + Meta.cardName(c) + ' ' + u.buys[1] + (u.buys[1] > 1 ? ' times' : ' time');
    },
    cardName: c => c.name || BS.UPGRADES[c.up].name,
    // A card bought in a run. Returns the cards this purchase unlocked.
    recordBuy(id) {
      const s = load(), was = BS.CARDS.filter(c => !Meta.unlocked(c));
      s.buys[id] = (s.buys[id] || 0) + 1; save();
      return was.filter(c => Meta.unlocked(c));
    },
    // The run is over (evacuated: the Hyperion took you off): bank its vespene and best wave. Returns { gas, unlocked: [cards] }.
    // Waves a run on rung i (0 is the first) must clear to open the next.
    ladderWaves: i => BS.LADDER_WAVES + i,
    // A run that clears enough waves opens the next rung of the ladder (rung: that rung's index, else null).
    endRun(mode, wavesCleared, evacuated) {
      const s = load(), G = BS.META.gas, was = BS.CARDS.filter(c => !Meta.unlocked(c));
      let rung = null;
      if (Meta.force === null && wavesCleared >= Meta.ladderWaves(s.rung || 0) && (s.rung || 0) < BS.LADDER.length - 1) rung = s.rung = (s.rung || 0) + 1;
      const gas = Math.round((wavesCleared * G.perWave + Math.floor(wavesCleared / 10) * G.milestone + (evacuated ? G.evac : 0)) * (mode === 'arena' ? G.arena : 1));
      s.gas += gas; s.best = Math.max(s.best, wavesCleared); save();
      return { gas, unlocked: was.filter(c => Meta.unlocked(c)), rung };
    },
  };

  BS.Meta = Meta;
})(window.BS);
