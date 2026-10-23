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
    marineDeath: U + 'terran/marinedeath/marinedeath.m3',
    firebatDeath: U + 'terran/firebatdeath/firebatdeath.m3',
    marauderDeath: U + 'terran/marauderdeath/marauderdeath.m3',
    missileturret: 'assets/buildings/terran/missileturret/missileturret.m3',
    missileturretDeath: 'assets/buildings/terran/missileturretdeath/missileturretdeath.m3',
    sensortower: 'assets/buildings/terran/sensortower/sensortower.m3',
    sensortowerDeath: 'assets/buildings/terran/sensortowerdeath/sensortowerdeath.m3',
    // Effects (particle-only effect models cannot be drawn by the viewer, so only mesh-based ones are listed)
    fxMuzzle: 'assets/effects/terran/marineweaponlaunch/marineweaponlaunch.m3',
  };

  // Extra models: zerg strain/skin variants for variety, and projectile meshes (streamed on demand).
  const E = 'assets/effects/';
  BS.EXTRA_MODELS = {
  zerglingWinged: U + 'zerg/zerglingupgrade/zerglingupgrade.m3',
  zerglingRaptor: U + 'zerg/zerglingex1a/zerglingex1a.m3', zerglingRaptorDeath: U + 'zerg/zerglingex1adeath/zerglingex1adeath.m3',
  zerglingSwarmling: U + 'zerg/zerglingex1b/zerglingex1b.m3', zerglingSwarmlingDeath: U + 'zerg/zerglingex1bdeath/zerglingex1bdeath.m3',
  zerglingPrimal: U + 'zerg/zergling_primal_collection/zergling_primal_collection.m3', zerglingPrimalDeath: U + 'zerg/zergling_primal_collection_death/zergling_primal_collection_death.m3',
  zerglingPrimalWinged: U + 'zerg/zerglingupgrade_primal_collection/zerglingupgrade_primal_collection.m3',
  banelingSplitter: U + 'zerg/banelingex1a/banelingex1a.m3', banelingHunter: U + 'zerg/banelingex1b/banelingex1b.m3',
  banelingPrimal: U + 'zerg/baneling_primal_collection/baneling_primal_collection.m3',
  roachGlial: U + 'zerg/roachupgrade/roachupgrade.m3', roachVile: U + 'zerg/roachex1a/roachex1a.m3', roachCorpser: U + 'zerg/roachex1b/roachex1b.m3',
  roachPrimal: U + 'zerg/roach_primal_collection/roach_primal_collection.m3',
  hydraliskImpaler: U + 'zerg/hydraliskex1a/hydraliskex1a.m3', hydraliskLurkerStrain: U + 'zerg/hydraliskex1b/hydraliskex1b.m3',
  hydraliskRemastered: U + 'zerg/hydralisk_remastered/hydralisk_remastered.m3', hydraliskPrimal: U + 'zerg/hydralisk_primal_collection/hydralisk_primal_collection.m3',
  mutaliskPrimal: U + 'zerg/primalzergmutalisk/primalzergmutalisk.m3', mutaliskPrimalSkin: U + 'zerg/mutalisk_primal_collection/mutalisk_primal_collection.m3',
  lurkerSeeker: U + 'zerg/lurkerex1a/lurkerex1a.m3', lurkerSpine: U + 'zerg/lurkerex1b/lurkerex1b.m3',
  ultraliskRemastered: U + 'zerg/ultralisk_remastered/ultralisk_remastered.m3', ultraliskPrimal: U + 'zerg/ultralisk_primal_collection/ultralisk_primal_collection.m3',
  broodlordEx1: U + 'zerg/broodlordex1/broodlordex1.m3', broodlordPrimal: U + 'zerg/broodlord_primal_collection/broodlord_primal_collection.m3',
  // Projectiles (mesh part; their particles come from the FX list below).
  projMutalisk: E + 'zerg/mutaliskmissile/mutaliskmissile.m3', projBanshee: E + 'terran/bansheemissile/bansheemissile.m3',
  projYamato: E + 'terran/yamatomissile/yamatomissile.m3', projHydralisk: E + 'zerg/hydraliskmissile/hydraliskmissile.m3',
  projRoach: E + 'zerg/roachmissile/roachmissile.m3', projMarauder: E + 'terran/maraudermissile/maraudermissile.m3',
  projSam: E + 'terran/missileturretmissile/missileturretmissile.m3',
  };

  // SC2 particle effects, replayed by both builds: [m3, optional sequence filter, loops].
  const FX = {
  marineLaunch: [E + 'terran/marineweaponlaunch/marineweaponlaunch.m3'], marineImpact: [E + 'terran/marineweaponimpact/marineweaponimpact.m3'],
  firebatLaunch: [E + 'terran/firebatlaunch/firebatlaunch.m3', '^birth'], marauderLaunch: [E + 'terran/maraudermissilelaunch/maraudermissilelaunch.m3'],
  marauderMissile: [E + 'terran/maraudermissile/maraudermissile.m3', null, true], marauderImpact: [E + 'terran/maraudermissileimpact/maraudermissileimpact.m3'],
  tankLaunch: [E + 'terran/siegetankweaponlaunch/siegetankweaponlaunch.m3'], tankImpact: [E + 'terran/siegetankweaponimpact/siegetankweaponimpact.m3'],
  bansheeMissile: [E + 'terran/bansheemissile/bansheemissile.m3', '^stand', true], bansheeImpact: [E + 'terran/bansheemissileimpact/bansheemissileimpact.m3'],
  yamatoMissile: [E + 'terran/yamatomissile/yamatomissile.m3', null, true], yamatoImpact: [E + 'terran/yamatomissileimpact/yamatomissileimpact.m3'],
  mutaliskMissile: [E + 'zerg/mutaliskmissile/mutaliskmissile.m3', '^stand$', true], mutaliskImpact: [E + 'zerg/mutaliskmissileimpact/mutaliskmissileimpact.m3'],
  hydraliskMissile: [E + 'zerg/hydraliskmissile/hydraliskmissile.m3', null, true], hydraliskImpact: [E + 'zerg/hydraliskmissileimpact/hydraliskmissileimpact.m3'],
  roachMissile: [E + 'zerg/roachmissile/roachmissile.m3', null, true], roachImpact: [E + 'zerg/roachmissileimpact/roachmissileimpact.m3'],
  lurkerMissile: [E + 'zerg/lurkermissile/lurkermissile.m3'], zerglingImpact: [E + 'zerg/zerglingweaponimpact/zerglingweaponimpact.m3'],
  ultraliskImpact: [E + 'zerg/ultraliskweaponimpact/ultraliskweaponimpact.m3'],
  samMissile: [E + 'terran/missileturretmissile/missileturretmissile.m3', null, true], samImpact: [E + 'terran/missileturretmissileimpact/missileturretmissileimpact.m3'],
  };
  // Every death model also carries its blood/acid burst: the plain "Death" one (the numbered sequences are SC2's
  // damage-type variants, which blow body parts up; m3fx falls back to them only when there is no plain one).
  BS.fxList = () => {
    const out = Object.assign({}, FX);
    for (const [key, m3] of Object.entries(Object.assign({}, BS.MODELS, BS.EXTRA_MODELS))) if (/Death$/.test(key)) out[key] = [m3, '^death$'];
    return out;
  };

  // Loaded first so the game can start quickly; everything else streams in the background.
  BS.CORE_MODELS = ['bunker', 'zergling', 'zerglingDeath', 'scv', 'marine', 'medivac', 'fxMuzzle'];
  // Effects fetched up front (the rest load the first time they are needed).
  BS.CORE_FX = ['marineLaunch', 'marineImpact', 'zerglingDeath', 'zerglingImpact', 'tankLaunch', 'tankImpact', 'banelingDeath'];

  // Ground texture (a StarCraft II Char tileset).
  BS.GROUND_TEXTURE = 'assets/textures/charex2_1.dds';
  // Cliff faces of the lane's chokepoint (Char cliff rock, dark at the top, glowing at the base).
  BS.CLIFF_TEXTURE = 'assets/textures/charex1cliff0_diffuse_00.png';

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
    samFire: [0, 1, 2].map(i => S + 'terran/missileturret/missileturret_attacklaunch' + i + '.wav'),
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
  // variants: strain/skin models mixed in for variety; impactFx: SC2 effect where a melee blow lands.
  BS.ZERG = {
    zergling: { name: 'Zergling', model: 'zergling', death: 'zerglingDeath', hp: 30, armor: 0, dmg: 5, cd: 0.497, range: 0.1, speed: 4.13, radius: 0.375, light: true, bounty: 4, budget: 2, unlock: 1, weight: 12, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'zerglingAttack', soundDeath: 'zerglingDeath', height: 0.8,
      variants: ['zerglingWinged', 'zerglingRaptor', 'zerglingSwarmling', 'zerglingPrimal', 'zerglingPrimalWinged'], impactFx: 'zerglingImpact' },
    baneling: { name: 'Baneling', model: 'baneling', death: 'banelingDeath', hp: 30, armor: 0, dmg: 16, dmgLight: 19, dmgStructure: 80, cd: 1, range: 0.25, speed: 3.5, radius: 0.375, light: true, suicide: true, splash: 2.2, bounty: 10, budget: 4, unlock: 3, weight: 5, soundDeath: 'banelingDeath', height: 0.8,
      variants: ['banelingSplitter', 'banelingHunter', 'banelingPrimal'] },
    roach: { name: 'Roach', model: 'roach', death: 'roachDeath', hp: 145, armor: 1, dmg: 16, cd: 1.43, range: 4, speed: 3.15, radius: 0.625, armored: true, bounty: 22, budget: 7, unlock: 5, weight: 5, hitFrac: 0.35, attackSeq: /^attack$/i, projectile: 'acid', soundAttack: 'roachFire', soundDeath: 'roachDeath', height: 0.9,
      variants: ['roachGlial', 'roachVile', 'roachCorpser', 'roachPrimal'] },
    hydralisk: { name: 'Hydralisk', model: 'hydralisk', death: 'hydraliskDeath', hp: 90, armor: 0, dmg: 12, cd: 0.59, range: 5, speed: 3.15, radius: 0.625, light: true, bounty: 28, budget: 8, unlock: 6, weight: 5, hitFrac: 0.3, attackSeq: /^attack$/i, projectile: 'spine', soundAttack: 'hydraliskFire', soundDeath: 'hydraliskDeath', height: 1.6,
      variants: ['hydraliskImpaler', 'hydraliskLurkerStrain', 'hydraliskRemastered', 'hydraliskPrimal'] },
    infested: { name: 'Infested Marine', model: 'infested', death: 'infestedDeath', hp: 50, armor: 0, dmg: 8, cd: 0.61, range: 5, speed: 2.25, radius: 0.375, light: true, bounty: 8, budget: 3, unlock: 7, weight: 6, hitFrac: 0.3, attackSeq: /^attack/i, projectile: 'bullet', soundAttack: 'marineFire', height: 1.2 },
    mutalisk: { name: 'Mutalisk', model: 'mutalisk', death: 'mutaliskDeath', hp: 120, armor: 0, dmg: 9, bounces: 3, cd: 1.09, range: 3, speed: 5.6, radius: 0.75, light: true, flying: true, fly: 2.6, bounty: 30, budget: 9, unlock: 9, weight: 4, hitFrac: 0.3, attackSeq: /^attack/i, projectile: 'glaive', soundAttack: 'mutaliskFire', soundDeath: 'mutaliskDeath', height: 1.2,
      variants: ['mutaliskPrimal', 'mutaliskPrimalSkin'] },
    lurker: { name: 'Lurker', model: 'lurker', death: 'lurkerDeath', hp: 200, armor: 1, dmg: 20, dmgArmored: 10, cd: 1.43, range: 8, speed: 4.13, radius: 0.75, armored: true, burrows: true, bounty: 50, budget: 16, unlock: 12, weight: 2, hitFrac: 0.35, projectile: 'spines', height: 1.0,
      variants: ['lurkerSeeker', 'lurkerSpine'] },
    ultralisk: { name: 'Ultralisk', model: 'ultralisk', death: 'ultraliskDeath', hp: 500, armor: 2, dmg: 35, cd: 0.61, range: 1, speed: 4.13, radius: 1.0, armored: true, cleave: 1.5, bounty: 100, budget: 30, unlock: 15, weight: 1.5, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'ultraliskAttack', soundDeath: 'ultraliskDeath', height: 2.4, deathHold: 5,
      variants: ['ultraliskRemastered'], impactFx: 'ultraliskImpact' },
    broodlord: { name: 'Brood Lord', model: 'broodlord', death: 'broodlordDeath', hp: 225, armor: 1, dmg: 20, cd: 1.79, range: 6, speed: 1.97, radius: 1.0, armored: true, flying: true, fly: 3.4, broodlings: true, bounty: 120, budget: 34, unlock: 18, weight: 1.2, hitFrac: 0.3, projectile: 'brood', height: 1.6,
      variants: ['broodlordEx1', 'broodlordPrimal'] },
    broodling: { name: 'Broodling', model: 'zergling', death: 'zerglingDeath', scale: 0.55, hp: 30, armor: 0, dmg: 4, cd: 0.46, range: 0.1, speed: 4.13, radius: 0.25, light: true, life: 8, bounty: 1, budget: 0, unlock: 999, weight: 0, hitFrac: 0.45, attackSeq: /^attack/i, height: 0.5, impactFx: 'zerglingImpact' },
  };

  // Bunker occupants.
  BS.INFANTRY = {
    marine: { name: 'Marine', icon: 'btn-unit-terran-marine', hp: 45, dmg: 6, cd: 0.61, range: 5, cost: 50, stimHp: 10, sound: 'marineFire', death: 'marineDeath', desc: 'Gauss rifle. Cheap, reliable, stims well.' },
    firebat: { name: 'Firebat', icon: 'btn-unit-terran-firebat', hp: 100, dmg: 8, dmgLight: 6, cd: 0.86, range: 2, splash: 1.3, groundOnly: true, cost: 100, stimHp: 10, sound: 'firebatFire', death: 'firebatDeath', desc: 'Flamethrower. Burns packs of lings and banelings.' },
    marauder: { name: 'Marauder', icon: 'btn-unit-terran-marauder', hp: 125, dmg: 10, dmgArmored: 10, cd: 1.07, range: 6, slow: 1.5, groundOnly: true, cost: 100, stimHp: 20, sound: 'marauderFire', death: 'marauderDeath', desc: 'Punisher grenades. Slows and shreds armoured.' },
  };

  BS.TERRAN = {
    bunker: { hp: 400, armor: 1, slots: 4, slotsUpgraded: 6, rangeBonus: 1, halfSize: 1.5 },
    scv: { hp: 45, repairRate: 13.8, speed: 3.94, radius: 0.375, cost: 50, max: 4 },
    tank: { hp: 175, armor: 1, dmg: 40, dmgArmored: 30, cd: 2.14, range: 13, minRange: 2, splash: [0.47, 0.78, 1.25], radius: 0.9, cost: 300, costStep: 100, maxLane: 4, maxArena: 4 },
    // Air strike: four banshees hover over the lane (and its ledges) and fire missiles at passing zerg.
    banshee: { count: 4, dmg: 12, shots: 2, cd: 1.1, range: 6, speed: 9, fly: 2.4, hover: 14, cost: 200, cooldown: 32 },
    // Troops standing behind the bunker (ejected, or recruited while it is full).
    squad: { max: 48 },
    // Missile turrets: anti-air only, like SC2 (12 x2, range 7).
    sam: { hp: 250, armor: 0, dmg: 12, shots: 2, cd: 0.86, range: 7, radius: 1.0, cost: 100, max: 3, build: 3 },
    // Sensor tower: the radar only works while one stands.
    sensor: { hp: 200, armor: 0, radius: 0.8, cost: 125, build: 3 },
    railsRange: 2,
    yamato: { dmg: 240, splash: 120, radius: 3, cost: 300, cooldown: 40 },
    stim: { duration: 11, cooldown: 13 },
    repair: { cost: 75, cooldown: 20, amount: 150 },
    hiSecRange: 1,
  };

  // costs: one entry per level (the last level is the max).
  BS.UPGRADES = {
    stim: { name: 'Stimpack', icons: ['btn-ability-terran-stimpack'], costs: [100], short: 'Unlocks STIM ability' },
    weapons: { name: 'Infantry Weapons', icons: ['btn-upgrade-terran-infantryweaponslevel1', 'btn-upgrade-terran-infantryweaponslevel2', 'btn-upgrade-terran-infantryweaponslevel3'], costs: [100, 175, 250, 350, 475, 600, 750, 900, 1100, 1300], short: '+1 infantry damage' },
    neosteel: { name: 'Neosteel Frame', icons: ['btn-upgrade-terran-neosteelframe'], costs: [150], short: 'Bunker holds six' },
    plating: { name: 'Building Armor', icons: ['btn-upgrade-terran-buildingarmor'], costs: [250, 350, 450, 600, 800, 1000, 1200, 1400], short: '+150 HP per level' },
    hisec: { name: 'Hi-Sec Auto Tracking', icons: ['btn-upgrade-terran-hisecautotracking'], costs: [200], short: '+1 range, bunker and tanks' },
    vehicle: { name: 'Vehicle Weapons', icons: ['btn-upgrade-terran-vehicleweaponslevel1'], costs: [150, 225, 300, 400, 525, 650, 800, 950, 1150, 1350], short: '+5 tank damage' },
    medivac: { name: 'Medivac on Station', icons: ['btn-ability-terran-heal'], costs: [200], short: 'Heals troops and SCVs' },
    rails: { name: 'Magnetic Rails', icons: ['btn-upgrade-terran-magrailmunitions'], costs: [300], short: '+2 bunker range' },
  };

  // Abilities on the action bar.
  BS.ABILITIES = [
    { id: 'stim', name: 'STIM', icon: 'btn-ability-terran-stimpack', key: 'S', accent: '#ffbf3c' },
    { id: 'strike', name: 'AIR STRIKE', icon: 'btn-unit-terran-banshee', key: 'A', accent: '#ff7a47' },
    { id: 'repair', name: 'REPAIR', icon: 'btn-ability-terran-repair', key: 'R', accent: '#5fe3ff' },
    { id: 'yamato', name: 'YAMATO', icon: 'btn-unit-terran-battlecruiser', key: 'Y', accent: '#bd6bff' },
  ];
})(window.BS);
