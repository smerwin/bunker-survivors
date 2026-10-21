// Unit, model and sound definitions. Numbers follow StarCraft II (Faster game speed).
// Models are StarCraft II assets streamed from the player's own install by the companion server.
window.BS = window.BS || {};
(function (BS) {
  'use strict';

  const U = 'assets/units/';
  BS.MODELS = {
    // Zerg
    zergling: U + 'zerg/zergling/zergling.m3',
    zerglingDeath: U + 'zerg/zerglingdeath/zerglingdeath.m3',
    baneling: U + 'zerg/baneling/baneling.m3',
    banelingDeath: U + 'zerg/banelingdeath/banelingdeath.m3',
    roach: U + 'zerg/roach/roach.m3',
    roachDeath: U + 'zerg/roachdeath/roachdeath.m3',
    hydralisk: U + 'zerg/hydralisk/hydralisk.m3',
    hydraliskDeath: U + 'zerg/hydraliskdeath/hydraliskdeath.m3',
    mutalisk: U + 'zerg/mutalisk/mutalisk.m3',
    mutaliskDeath: U + 'zerg/mutaliskdeath/mutaliskdeath_00.m3',
    lurker: U + 'zerg/lurker/lurker.m3',
    lurkerDeath: U + 'zerg/lurkerdeath/lurkerdeath_00.m3',
    ultralisk: U + 'zerg/ultralisk/ultralisk.m3',
    ultraliskDeath: U + 'zerg/ultraliskdeath/ultraliskdeath_00.m3',
    broodlord: U + 'zerg/broodlord/broodlord.m3',
    broodlordDeath: U + 'zerg/broodlorddeath/broodlorddeath_00.m3',
    infested: U + 'zerg/infestedmarine/infestedmarine.m3',
    infestedDeath: U + 'zerg/infestedmarinedeathragdoll/infestedmarinedeathragdoll.m3',
    // Terran
    bunker: 'assets/buildings/terran/bunker/bunker.m3',
    bunkerDeath: 'assets/buildings/terran/bunkerdeath/bunkerdeath.m3',
    scv: U + 'terran/scv/scv.m3',
    scvDeath: U + 'terran/scvdeath/scvdeath.m3',
    siegetank: U + 'terran/siegetank/siegetank.m3',
    siegetankMorph: U + 'terran/siegetankmorph/siegetankmorph.m3',
    siegetankDeath: U + 'terran/siegetanksiegeddeath/siegetanksiegeddeath.m3',
    medivac: U + 'terran/medivac/medivac.m3',
    banshee: U + 'terran/banshee/banshee.m3',
    marine: U + 'terran/marine/marine.m3',
    firebat: U + 'terran/firebat/firebat.m3',
    marauder: U + 'terran/marauder/marauder.m3',
    // Effects (particle-only effect models cannot be drawn by the viewer, so only mesh-based ones are listed)
    fxMuzzle: 'assets/effects/terran/marineweaponlaunch/marineweaponlaunch.m3',
  };

  // Loaded first so the game can start quickly; everything else streams in the background.
  BS.CORE_MODELS = ['bunker', 'zergling', 'zerglingDeath', 'scv', 'fxMuzzle'];

  // Ground texture (a StarCraft II Char tileset).
  BS.GROUND_TEXTURE = 'assets/textures/charex2_1.dds';

  const S = 'assets/sounds/';
  BS.SOUNDS = {
    marineFire: [0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => S + 'terran/marine/marine_attacklaunch' + i + '.wav'),
    stim: [0, 1, 2, 3].map(i => S + 'terran/marine/marine_stimpack' + i + '.wav'),
    firebatFire: [S + 'terran/firebat/firebat_attacklaunch0.wav'],
    marauderFire: [S + 'terran/marauder/marauder_attacklaunch0.wav'],
    tankFire: [S + 'terran/siegetank/siegetank_siegeattacklaunch0.wav'],
    tankHit: [S + 'terran/siegetank/siegetank_siegeattackimpact0.wav'],
    tankDeath: [S + 'terran/siegetank/siegetank_death0.wav'],
    bansheeFire: [S + 'terran/banshee/banshee_attacklaunch0.wav'],
    bansheeHit: [S + 'terran/banshee/banshee_attackimpact0.wav'],
    scvRepair: [S + 'terran/scv/scv_repair0.wav'],
    medivacUnload: [S + 'terran/medivac/medivac_unload0.wav'],
    zerglingAttack: [S + 'zerg/zergling/zergling_attacklaunch0.wav'],
    zerglingDeath: [S + 'zerg/zergling/zergling_death0.wav'],
    banelingDeath: [0, 1, 2].map(i => S + 'zerg/baneling/baneling_death' + i + '.wav'),
    hydraliskFire: [S + 'zerg/hydralisk/hydralisk_attacklaunchranged0.wav'],
    hydraliskDeath: [S + 'zerg/hydralisk/hydralisk_death0.wav'],
    roachFire: [S + 'zerg/roach/roach_attacklaunchranged0.wav'],
    roachDeath: [S + 'zerg/roach/roach_death0.wav'],
    mutaliskFire: [S + 'zerg/mutalisk/mutalisk_attacklaunch0.wav'],
    mutaliskDeath: [S + 'zerg/mutalisk/mutalisk_death0.wav'],
    ultraliskAttack: [S + 'zerg/ultralisk/ultralisk_attacklaunch0.wav'],
    ultraliskDeath: [S + 'zerg/ultralisk/ultralisk_death0.wav'],
    explosion: [S + 'siegetank_explosion_add_01.wav', S + 'siegetank_explosion_add_02.wav'],
  };

  // Zerg attackers. hp/armor/damage/cooldown/range/speed are SC2 values; bounty and budget are ours.
  // hitFrac: fraction of the attack cooldown at which the damage lands (synced with the animation).
  BS.ZERG = {
    zergling: { name: 'Zergling', model: 'zergling', death: 'zerglingDeath', hp: 35, armor: 0, dmg: 5, cd: 0.497, range: 0.1, speed: 4.13, radius: 0.375, light: true, bounty: 6, budget: 2, unlock: 1, weight: 10, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'zerglingAttack', soundDeath: 'zerglingDeath', height: 0.8 },
    baneling: { name: 'Baneling', model: 'baneling', death: 'banelingDeath', hp: 30, armor: 0, dmg: 16, dmgLight: 19, dmgStructure: 80, cd: 1, range: 0.25, speed: 3.5, radius: 0.375, light: true, suicide: true, splash: 2.2, bounty: 10, budget: 4, unlock: 3, weight: 5, soundDeath: 'banelingDeath', height: 0.8 },
    roach: { name: 'Roach', model: 'roach', death: 'roachDeath', hp: 145, armor: 1, dmg: 16, cd: 1.43, range: 4, speed: 3.15, radius: 0.625, armored: true, bounty: 22, budget: 7, unlock: 5, weight: 5, hitFrac: 0.35, attackSeq: /^attack$/i, projectile: 'acid', soundAttack: 'roachFire', soundDeath: 'roachDeath', height: 0.9 },
    hydralisk: { name: 'Hydralisk', model: 'hydralisk', death: 'hydraliskDeath', hp: 90, armor: 0, dmg: 12, cd: 0.59, range: 5, speed: 3.15, radius: 0.625, light: true, bounty: 28, budget: 8, unlock: 6, weight: 5, hitFrac: 0.3, attackSeq: /^attack$/i, projectile: 'spine', soundAttack: 'hydraliskFire', soundDeath: 'hydraliskDeath', height: 1.6 },
    infested: { name: 'Infested Marine', model: 'infested', death: 'infestedDeath', hp: 50, armor: 0, dmg: 8, cd: 0.61, range: 5, speed: 2.25, radius: 0.375, light: true, bounty: 8, budget: 3, unlock: 7, weight: 6, hitFrac: 0.3, attackSeq: /^attack/i, projectile: 'bullet', soundAttack: 'marineFire', height: 1.2 },
    mutalisk: { name: 'Mutalisk', model: 'mutalisk', death: 'mutaliskDeath', hp: 120, armor: 0, dmg: 9, bounce: 3, cd: 1.09, range: 3, speed: 5.6, radius: 0.75, light: true, flying: true, fly: 2.6, bounty: 30, budget: 9, unlock: 9, weight: 4, hitFrac: 0.3, attackSeq: /^attack/i, projectile: 'glaive', soundAttack: 'mutaliskFire', soundDeath: 'mutaliskDeath', height: 1.2 },
    lurker: { name: 'Lurker', model: 'lurker', death: 'lurkerDeath', hp: 200, armor: 1, dmg: 20, dmgArmored: 10, cd: 1.43, range: 8, speed: 4.13, radius: 0.75, armored: true, burrows: true, bounty: 50, budget: 16, unlock: 12, weight: 2, hitFrac: 0.35, projectile: 'spines', height: 1.0 },
    ultralisk: { name: 'Ultralisk', model: 'ultralisk', death: 'ultraliskDeath', hp: 500, armor: 2, dmg: 35, cd: 0.61, range: 1, speed: 4.13, radius: 1.0, armored: true, cleave: 1.5, bounty: 100, budget: 30, unlock: 15, weight: 1.5, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'ultraliskAttack', soundDeath: 'ultraliskDeath', height: 2.4, deathHold: 5 },
    broodlord: { name: 'Brood Lord', model: 'broodlord', death: 'broodlordDeath', hp: 225, armor: 1, dmg: 20, cd: 1.79, range: 10, speed: 1.97, radius: 1.0, armored: true, flying: true, fly: 3.4, broodlings: true, bounty: 120, budget: 34, unlock: 18, weight: 1.2, hitFrac: 0.3, projectile: 'brood', height: 1.6 },
    broodling: { name: 'Broodling', model: 'zergling', death: 'zerglingDeath', scale: 0.55, hp: 30, armor: 0, dmg: 4, cd: 0.46, range: 0.1, speed: 4.13, radius: 0.25, light: true, life: 8, bounty: 1, budget: 0, unlock: 999, weight: 0, hitFrac: 0.45, attackSeq: /^attack/i, height: 0.5 },
  };

  // Bunker occupants.
  BS.INFANTRY = {
    marine: { name: 'Marine', icon: 'btn-unit-terran-marine', hp: 45, dmg: 6, cd: 0.61, range: 5, cost: 50, stimHp: 10, sound: 'marineFire', desc: 'Gauss rifle. Reliable, cheap, stims well.' },
    firebat: { name: 'Firebat', icon: 'btn-unit-terran-firebat', hp: 100, dmg: 8, dmgLight: 6, cd: 0.86, range: 2, splash: 1.3, groundOnly: true, cost: 100, stimHp: 10, sound: 'firebatFire', desc: 'Flamethrower. Burns packs of lings and banelings.' },
    marauder: { name: 'Marauder', icon: 'btn-unit-terran-marauder', hp: 125, dmg: 10, dmgArmored: 10, cd: 1.07, range: 6, slow: 1.5, groundOnly: true, cost: 100, stimHp: 20, sound: 'marauderFire', desc: 'Punisher grenades. Slows and shreds armored.' },
  };

  BS.TERRAN = {
    bunker: { hp: 400, armor: 1, slots: 4, rangeBonus: 1, halfSize: 1.5 },
    scv: { hp: 45, repairRate: 13.8, repairCostPerHp: 0.25, speed: 3.94, radius: 0.375, cost: 50, max: 4 },
    tank: { hp: 175, armor: 1, dmg: 40, dmgArmored: 30, cd: 2.14, range: 13, minRange: 2, splash: [0.47, 0.78, 1.25], radius: 0.9, cost: 300, maxLane: 2, maxArena: 4 },
    banshee: { dmg: 12, shots: 2, cd: 0.55, range: 7, speed: 9, fly: 4.5, cost: 150, cooldown: 25 },
  };

  BS.UPGRADES = {
    stim: { name: 'Stimpack', icon: 'btn-ability-terran-stimpack', cost: 100, desc: 'Research. Then tap STIM: +50% fire rate for 11s, costs unit HP.' },
    weapons: { name: 'Infantry Weapons', icons: ['btn-upgrade-terran-infantryweaponslevel1', 'btn-upgrade-terran-infantryweaponslevel2', 'btn-upgrade-terran-infantryweaponslevel3'], costs: [100, 175, 250], desc: '+1 damage for every bunker unit.' },
    vehicle: { name: 'Vehicle Weapons', icon: 'btn-upgrade-terran-vehicleweaponslevel1', costs: [150, 225, 300], desc: '+5 (+3 armored) siege tank damage.' },
    neosteel: { name: 'Neosteel Frame', icon: 'btn-upgrade-terran-neosteelframe', cost: 150, desc: 'Bunker holds 6 units.' },
    plating: { name: 'Reinforced Plating', icon: 'btn-building-terran-bunker', cost: 250, desc: '+150 bunker HP, +1 armor.' },
    medivac: { name: 'Medivac on Station', icon: 'btn-ability-terran-heal', cost: 200, desc: 'Heals bunker infantry 9 HP/s so they can stim again.' },
  };
})(window.BS);
