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
    // Late-game zerg (they join once the player starts unlocking high tech).
    wingling: U + 'zerg/zerglingupgrade/zerglingupgrade.m3',
    winglingDeath: U + 'zerg/zerglingupgradedeath/zerglingupgradedeath.m3',
    guardian: U + 'zerg/guardian/guardian.m3',
    guardianDeath: U + 'zerg/guardiandeath/guardiandeath.m3',
    devourer: U + 'zerg/devourer/devourer.m3',
    devourerDeath: U + 'zerg/devourerex3_death/devourerex3_death.m3',
    scourge: U + 'zerg/scourge/scourge.m3',
    scourgeDeath: U + 'zerg/scourgedeath/scourgedeath.m3',
    defiler: U + 'zerg/defiler/defiler.m3',
    defilerDeath: U + 'zerg/defiler_ex3_death/defiler_ex3_death_00.m3',
    viper: U + 'zerg/viper_primal_collection/viper_primal_collection.m3',
    viperDeath: U + 'zerg/viper_primal_collection_deathragdoll/viper_primal_collection_deathragdoll.m3',
    overlord: U + 'zerg/overlordtransport/overlordtransport.m3',
    overlordDeath: U + 'zerg/overlordtranportdeath/overlordtranportdeath.m3',
    nydus: U + 'zerg/nydusworm/nydusworm.m3',
    // The Brood War queen (Stukov's classic queen) and the broodlings her Spawn Broodling hatches (brood lords' too).
    broodqueen: U + 'zerg/queenclassic/queenclassic.m3',
    broodqueenDeath: U + 'zerg/queenclassic_death/queenclassic_death.m3',
    broodling: U + 'zerg/broodling_rcz/broodling_rcz.m3',
    broodlingDeath: U + 'zerg/broodling_rcz_death/broodling_rcz_death.m3',
    // Creep: queens plant creep tumors; overlords generate creep from the sky.
    queen: U + 'zerg/queen/queen.m3',
    queenDeath: U + 'zerg/queendeath/queendeath.m3',
    creeptumor: 'assets/buildings/zerg/creeptumor/creeptumor.m3',
    creeptumorDeath: 'assets/buildings/zerg/creeptumordeath/creeptumordeath.m3',
    overlordCreep: U + 'zerg/overlord/overlord.m3',
    overlordCreepDeath: U + 'zerg/overlorddeath/overlorddeath.m3',
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
    // Forward base on the high ground (a maxed-out army expands there).
    commandcenter: 'assets/buildings/terran/commandcenter/commandcenter.m3',
    commandcenterDeath: 'assets/buildings/terran/commandcenterdeath/commandcenterdeath.m3',
    // High-tech reinforcements (unlocked by maxing the tech trees).
    goliath: U + 'terran/goliath/goliath.m3',
    goliathDeath: U + 'terran/goliathdeath/goliathdeath.m3',
    thor: U + 'terran/thor/thor.m3',
    thorDeath: U + 'terran/thordeath/thordeath_00.m3',
    odin: U + 'terran/odin/odin.m3',
    odinDeath: U + 'terran/odindeath/odindeath.m3',
    battlecruiser: U + 'terran/battlecruiser/battlecruiser.m3',
    vikingfighter: U + 'terran/vikingfighter/vikingfighter.m3',
    // Air support can now be shot down (scourge, devourers).
    battlecruiserDeath: U + 'terran/battlecruiserdeath/battlecruiserdeath_00.m3',
    bansheeDeath: U + 'terran/bansheedeath/bansheedeath.m3',
    vikingDeath: U + 'terran/vikingfighterdeath/vikingfighterdeath_00.m3',
    // Late-game Starport support: a wraith squadron and a science vessel.
    wraith: U + 'terran/wraith/wraith.m3',
    wraithDeath: U + 'terran/wraithdeath/wraithdeath.m3',
    sciencevessel: U + 'terran/sciencevessel/sciencevessel.m3',
    sciencevesselDeath: U + 'terran/sciencevesseldeath/sciencevesseldeath.m3',
    // The finale: the Queen of Blades (her own model carries her Death sequence), a leviathan, zerg drop pods.
    kerrigan: U + 'zerg/kerriganinfested/kerriganinfested.m3',
    kerriganDeath: U + 'zerg/kerriganinfested/kerriganinfested.m3',
    leviathan: U + 'zerg/leviathan/leviathan.m3',
    leviathanDeath: U + 'zerg/leviathandeath/leviathandeath.m3',
    droppod: 'assets/effects/zerg/zergdroppod/zergdroppod.m3',      // its Birth sequence is the fall from the sky and the pod bursting open
    // Effects (particle-only effect models cannot be drawn by the viewer, so only mesh-based ones are listed)
    fxMuzzle: 'assets/effects/terran/marineweaponlaunch/marineweaponlaunch.m3',
  };

  // Extra models: zerg strain/skin variants for variety, and projectile meshes (streamed on demand).
  const E = 'assets/effects/';
  BS.EXTRA_MODELS = {
  // Special infantry from the armoury (BS.TERRAN.reaper / ghost), and the overseer that hunts cloaked ghosts.
  reaper: U + 'terran/reaper/reaper.m3', reaperDeath: U + 'terran/reaperdeath/reaperdeath_00.m3',
  ghost: U + 'terran/ghost/ghost.m3', ghostDeath: U + 'terran/ghostdeath/ghostdeath.m3',
  overseer: U + 'zerg/overseer/overseer.m3', overseerDeath: U + 'zerg/overseerdeath/overseerdeath.m3',
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
  projGuardian: E + 'zerg/primalguardianmissile/primalguardianmissile.m3',  // the Brood War acid glob: a big yellow goo ball
  projLeviathan: E + 'zerg/leviathanmissile/leviathanmissile.m3',
  // Science vessel spells, worn by their target: Irradiate's green haze and the Defensive Matrix shell.
  irradiate: E + 'terran/irradiate/irradiate.m3', defensiveMatrix: E + 'terran/defensivematrixshield/defensivematrixshield.m3',
  };

  // SC2 particle effects, replayed by both builds: [m3, optional sequence filter, loops, 'mask' to cut particles out by
  // their material's alpha mask (see server/m3fx.js)].
  const FX = {
  marineLaunch: [E + 'terran/marineweaponlaunch/marineweaponlaunch.m3'], marineImpact: [E + 'terran/marineweaponimpact/marineweaponimpact.m3'],
  firebatLaunch: [E + 'terran/firebatlaunch/firebatlaunch.m3', '^birth'], marauderLaunch: [E + 'terran/maraudermissilelaunch/maraudermissilelaunch.m3'],
  marauderMissile: [E + 'terran/maraudermissile/maraudermissile.m3', null, true], marauderImpact: [E + 'terran/maraudermissileimpact/maraudermissileimpact.m3'],
  tankLaunch: [E + 'terran/siegetankweaponlaunch/siegetankweaponlaunch.m3'], tankImpact: [E + 'terran/siegetankweaponimpact/siegetankweaponimpact.m3'],
  bansheeMissile: [E + 'terran/bansheemissile/bansheemissile.m3', '^stand', true], bansheeImpact: [E + 'terran/bansheemissileimpact/bansheemissileimpact.m3'],
  yamatoMissile: [E + 'terran/yamatomissile/yamatomissile.m3', null, true], yamatoCharge: [E + 'terran/yamatomissilecharge/yamatomissilecharge.m3', null, true], yamatoImpact: [E + 'terran/yamatomissileimpact/yamatomissileimpact.m3'],
  mutaliskMissile: [E + 'zerg/mutaliskmissile/mutaliskmissile.m3', '^stand$', true], mutaliskImpact: [E + 'zerg/mutaliskmissileimpact/mutaliskmissileimpact.m3'],
  hydraliskMissile: [E + 'zerg/hydraliskmissile/hydraliskmissile.m3', null, true], hydraliskImpact: [E + 'zerg/hydraliskmissileimpact/hydraliskmissileimpact.m3'],
  roachMissile: [E + 'zerg/roachmissile/roachmissile.m3', null, true], roachImpact: [E + 'zerg/roachmissileimpact/roachmissileimpact.m3'],
  lurkerMissile: [E + 'zerg/lurkermissile/lurkermissile.m3'], zerglingImpact: [E + 'zerg/zerglingweaponimpact/zerglingweaponimpact.m3'],
  ultraliskImpact: [E + 'zerg/ultraliskweaponimpact/ultraliskweaponimpact.m3'],
  goliathAirMissile: [E + 'terran/goliathairmissile/goliathairmissile.m3', null, true], goliathAirImpact: [E + 'terran/goliathairmissileimpact/goliathairmissileimpact.m3'],
  thorArmLaunch: [E + 'terran/thorarmmissilelaunch/thorarmmissilelaunch.m3'], thorArmMissile: [E + 'terran/thorarmmissile/thorarmmissile.m3', null, true],
  thorArmImpact: [E + 'terran/thorarmmissileimpact/thorarmmissileimpact.m3'], thorShoulderLaunch: [E + 'terran/thorshouldermissilelaunch/thorshouldermissilelaunch.m3'],
  thorShoulderMissile: [E + 'terran/thorshouldermissile/thorshouldermissile.m3', null, true], thorShoulderImpact: [E + 'terran/thorshouldermissileimpact/thorshouldermissileimpact.m3'],
  odinShoulderImpact: [E + 'terran/odinshouldermissileimpact/odinshouldermissileimpact.m3'],
  vikingLaunch: [E + 'terran/vikingfightermissilelaunch/vikingfightermissilelaunch.m3'], vikingMissile: [E + 'terran/vikingfightermissile/vikingfightermissile.m3', null, true],
  vikingImpact: [E + 'terran/vikingfightermissileimpact/vikingfightermissileimpact.m3'],
  bcMissile: [E + 'terran/battlecruisermissile/battlecruisermissile.m3', null, true], bcMissileImpact: [E + 'terran/battlecruisermissileimpact/battlecruisermissileimpact.m3'],
  nukeFalling: [E + 'terran/nukefalling/nukefalling.m3', null, true], nuke: [E + 'terran/nuke/nuke.m3'],
  // The wraith's laser hit (its beam is an M3 ribbon, which neither build reads, so the games draw the beam themselves).
  laserImpact: [E + 'terran/wraithlasermissileimpact/wraithlasermissileimpact.m3'],
  wraithMissile: [E + 'terran/wraithmissile/wraithmissile.m3', '^stand', true], wraithImpact: [E + 'terran/wraithmissileimpact/wraithmissileimpact.m3'],
  reaperLaunch: [E + 'terran/reaperweaponlaunch/reaperweaponlaunch.m3'], reaperImpact: [E + 'terran/reaperweaponimpact/reaperweaponimpact.m3'],
  reaperJump: [E + 'terran/reaperjumplaunch/reaperjumplaunch.m3'], reaperLand: [E + 'terran/reaperjumplanddown/reaperjumplanddown.m3'],
  // The KD8 charge: SC2's own has no effect model of its own, so it borrows the co-op Hellion grenade.
  kd8Missile: [E + 'terran/mirahorner_hellion_basicgrenade_coop_missile/mirahorner_hellion_basicgrenade_coop_missile.m3', null, true],
  kd8Impact: [E + 'terran/mirahorner_hellion_basicgrenade_coop_impact/mirahorner_hellion_basicgrenade_coop_impact.m3'],
  ghostLaunch: [E + 'terran/ghostweaponlaunch/ghostweaponlaunch.m3'], ghostImpact: [E + 'terran/ghostweaponimpact/ghostweaponimpact.m3'],
  snipeImpact: [E + 'terran/ghostsnipehitsprite/ghostsnipehitsprite.m3'],
  samMissile: [E + 'terran/missileturretmissile/missileturretmissile.m3', null, true], samImpact: [E + 'terran/missileturretmissileimpact/missileturretmissileimpact.m3'],
  // Late-game zerg.
  guardianMissile: [E + 'zerg/primalguardianmissile/primalguardianmissile.m3', null, true], guardianImpact: [E + 'zerg/primalguardianmissileimpact/primalguardianmissileimpact.m3'],
  devourerMissile: [E + 'zerg/devourermissile/devourermissile.m3', null, true], acidSpores: [E + 'zerg/devourer_acidspores_impact/devourer_acidspores_impact.m3'],
  darkSwarm: [E + 'zerg/defiler_darkswarm_aoe/defiler_darkswarm_aoe.m3', null, true], plague: [E + 'zerg/defiler_plague/defiler_plague.m3'],
  nydusBirth: [E + 'zerg/nyduscreepspoutbirth/nyduscreepspoutbirth.m3'], viperTongueImpact: [E + 'zerg/vipertongueimpact/vipertongueimpact.m3'],
  // Brood queen: Spawn Broodling flies as a parasite dart and bursts into broodlings on the victim.
  broodSpellMissile: [E + 'zerg/infestorparasitemissile/infestorparasitemissile.m3', null, true],
  broodSpellImpact: [E + 'zerg/kerriganspawnbroodlingsimpact/kerriganspawnbroodlingsimpact.m3'],
  // The finale: Kerrigan's Leaping Strike and Apocalypse, the drop pods' streak down and burst.
  psionicShift: [E + 'zerg/psionicshift/psionicshift.m3', '^birth'], leapImpact: [E + 'zerg/leapingstrikeimpact/leapingstrikeimpact.m3'],
  apocalypseChannel: [E + 'zerg/kerriganapocalypsechannel/kerriganapocalypsechannel.m3', '^stand', true], apocalypse: [E + 'zerg/kerriganapocalypseexplosion/kerriganapocalypseexplosion.m3'],
  // A creep overlord's Generate Creep: globs of creep dribbling from its belly (looped while it hangs there).
  creepDrip: [E + 'zerg/creepdriplaunch/creepdriplaunch.m3', '^stand$', true, 'mask'],
  // Kerrigan's HOTS abilities (Game.updateKerrigan).
  kineticImpact: [E + 'zerg/kineticblastimpact/kineticblastimpact.m3'], crushingGrip: [E + 'zerg/crushinggripdustimpact/crushinggripdustimpact.m3'],
  immobilizationWave: [E + 'zerg/kerrigan_immobilizationwave/kerrigan_immobilizationwave.m3'],
  immobilizationImpact: [E + 'zerg/kerrigan_immobilizationwave_impact/kerrigan_immobilizationwave_impact.m3'],
  mutationImpact: [E + 'zerg/wildmutationimpact/wildmutationimpact.m3'], mutationBuff: [E + 'zerg/wildmutationbuff/wildmutationbuff.m3'],
  kerriganFury: [E + 'zerg/kerriganfury/kerriganfury.m3'],
  podFall: [E + 'zerg/zergdroppod/zergdroppod.m3', '^birth'], podImpact: [E + 'zerg/kerrigandroppodexplosion/kerrigandroppodexplosion.m3'],
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
  // Zerg creep spreading over the ground (tinted purple by both builds).
  BS.CREEP_TEXTURE = 'assets/textures/creep.png';

  const S = 'assets/sounds/';
  BS.SOUNDS = {
    marineFire: [0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => S + 'terran/marine/marine_attacklaunch' + i + '.wav'),
    reaperFire: [0, 1, 2, 3, 4, 5].map(i => S + 'terran/reaper/reaper_attacklaunch' + i + '.wav'),
    kd8Throw: [0, 1, 2].map(i => S + 'terran/reaper/reaper_d8chargeattacklaunch' + i + '.wav'),
    kd8Blast: [1, 2, 3, 4].map(i => S + 'reaper_kd8chargeexplosion0' + i + '.wav'),
    ghostFire: [0, 1, 2].map(i => S + 'terran/ghost/ghost_attacklaunch' + i + '.wav'),
    snipeFire: [S + 'terran/ghost/ghost_snipeattacklaunch.wav'],
    snipeHit: [0, 1, 2, 3, 4].map(i => S + 'terran/ghost/ghost_snipeimpact' + i + '.wav'),
    cloakOn: [S + 'terran/ghost/ghost_cloakon.wav'], cloakOff: [S + 'terran/ghost/ghost_cloakoff.wav'],
    stim: [0, 1, 2, 3].map(i => S + 'terran/marine/marine_stimpack' + i + '.wav'),
    firebatFire: [S + 'terran/firebat/firebat_attacklaunch0.wav'],
    marauderFire: [S + 'terran/marauder/marauder_attacklaunch0.wav'],
    tankFire: [S + 'terran/siegetank/siegetank_siegeattacklaunch0.wav'],
    tankHit: [S + 'terran/siegetank/siegetank_siegeattackimpact0.wav'],
    tankDeath: [S + 'terran/siegetank/siegetank_death0.wav'],
    bansheeFire: [S + 'terran/banshee/banshee_attacklaunch0.wav'],
    bansheeHit: [S + 'terran/banshee/banshee_attackimpact0.wav'],
    samFire: [0, 1, 2].map(i => S + 'terran/missileturret/missileturret_attacklaunch' + i + '.wav'),
    bcFire: [0, 1, 2].map(i => S + 'terran/battlecruiser/battlecruiser_attacklaunch' + i + '.wav'),
    bcMissiles: [0, 1, 2].map(i => S + 'terran/battlecruiser/battlecruiser_missilepodslaunch' + i + '.wav'),
    bcYamato: [S + 'terran/battlecruiser/battlecruiser_yamatoattacklaunch0.wav'],
    bcYamatoCharge: [S + 'terran/battlecruiser/battlecruiser_yamatoattackcharge0.wav'],
    laserHit: [0, 1, 2, 3, 4].map(i => S + 'terran/wraith/wraith_groundattackimpact' + i + '.wav'),
    irradiate: [0, 1, 2, 3].map(i => S + 'terran/sciencevessel/sciencevessel_irradiateimpact' + i + '.wav'),
    thorFire: [0, 1, 2].map(i => S + 'terran/thor/thor_attacklaunch' + i + '.wav'),
    thorHit: [0, 1, 2].map(i => S + 'terran/thor/thor_attackimpact' + i + '.wav'),
    goliathFire: [0, 1, 2].map(i => S + 'terran/goliath/goliath_groundattacklaunch' + i + '.wav'),
    vikingFire: [0, 1, 2].map(i => S + 'terran/viking/viking_fighterattacklaunch' + i + '.wav'),
    nukeLaunch: [S + 'terran/ghost/ghost_nuclearstrikelaunch0.wav'],
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
    guardianFire: [1, 2, 3, 4, 5].map(i => S + 'primalguardian_d_launch_0' + i + '.wav'),
    scourgeHit: [0, 1, 2, 3, 4, 5].map(i => S + 'zerg/scourge/scourge_attacklaunch' + i + '.wav'),
    devourerDeath: [0, 1, 2].map(i => S + 'devourer_deathvox0' + i + '.wav'),
    overlordDeath: [0, 1, 2].map(i => S + 'zerg/overlord/overlord_death' + i + '.wav'),
    overlordUnload: [S + 'zerg/overlord/overlord_unload0.wav'],
    nydusUnload: [S + 'zerg/nydusworm/nydusworm_unload00.wav', S + 'zerg/nydusworm/nydusworm_unload01.wav'],
    viperAbduct: [1, 2, 3].map(i => S + 'ac_viper_attackimpact_0' + i + '.wav'),
    plague: [0, 1, 2].map(i => S + 'zerg/infestor/infestor_plagueattackimpact' + i + '.wav'),
    broodSpell: [1, 2, 3, 4, 5].map(i => S + 'si_queen_broodling_impact0' + i + '.wav'),
    broodlingBirth: [1, 2, 3, 4, 5].map(i => S + 'stukov_broodling_birth_0' + i + '.wav'),
    broodlingDeath: [0, 1, 2, 3, 4].map(i => S + 'zerg/broodling/broodling_death' + i + '.wav'),
    broodqueenDeath: [0, 1, 2].map(i => S + 'zerg/queen/queen_death' + i + '.wav'),
    queenDeath: [0, 1, 2].map(i => S + 'zerg/queen/queen_death' + i + '.wav'),
    tumorPlant: [1, 2, 3, 4].map(i => S + 'creeptumor_placeflarp_' + i + '.wav'),
    tumorBirth: [1, 2, 3, 4, 5].map(i => S + 'creeptumor_newemerge_' + i + '.wav'),
    generateCreep: [S + 'zerg/overlord/overlord_generatecreep0.wav'],
    // The finale.
    finaleRoar: [S + 'zerg/leviathan/leviathan_roar0.wav'],
    kerriganPsi: [0, 1, 2, 3].map(i => S + 'kerrigan_psionicshift0' + i + '.wav'),
    kerriganLeap: [0, 1].map(i => S + 'spell_leapingstrike_launch_0' + i + '.wav'),
    kineticBlast: [1, 2, 3].map(i => S + 'spell_kerrigan_kineticblast' + i + '.wav'),
    kineticHit: [0, 1].map(i => S + 'kerrigan_kineticblast_impact0' + i + '.wav'),
    crushingGrip: [S + 'kerrigan_crushinggrip_launch.wav'],
    kerriganChain: [0, 1, 2, 3, 4, 5].map(i => S + 'spell_kerrigan_chain' + i + '.wav'),
    kerriganDeath: [S + 'kerriganinfested_death_wingfoley.wav', S + 'kerriganinfested_death_bodyfall.wav'],
    podDrop: [0, 1, 2].map(i => S + 'zerg/zergdroppod/zergdroppod_drop' + i + '.wav'),
    podImpact: [0, 1, 2, 3].map(i => S + 'zerg_droppod_explode' + i + '.wav'),
  };

  // Strains and skins (BS.ZERG variants) only mix in once the plain strain has had the field to itself: after waves past
  // its unlock wave, primal ones primalAfter.
  BS.VARIANTS = { after: 3, primalAfter: 10 };

  // Zerg attackers. hp/armor/damage/cooldown/range/speed are SC2 values; bounty and budget are ours.
  // hitFrac: fraction of the attack cooldown at which the damage lands (synced with the animation).
  // variants: strain/skin models mixed in for variety; impactFx: SC2 effect where a melee blow lands.
  BS.ZERG = {
    zergling: { name: 'Zergling', model: 'zergling', death: 'zerglingDeath', surround: true, hp: 30, armor: 0, dmg: 5, cd: 0.497, range: 0.1, speed: 4.13, radius: 0.375, light: true, bounty: 4, budget: 2, unlock: 1, weight: 12, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'zerglingAttack', soundDeath: 'zerglingDeath', height: 0.8,
      variants: ['zerglingRaptor', 'zerglingSwarmling', 'zerglingPrimal'], variantFrom: 4, variantStep: 4, impactFx: 'zerglingImpact' },
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
    // Late game: these join the waves once high tech starts unlocking (late = how many tech cards must be unlocked);
    // the more cards, the more of them. See Game.startWave. Winged zerglings only in lane mode, hopping the mesa.
    wingling: { name: 'Winged Zergling', model: 'wingling', death: 'winglingDeath', surround: true, hp: 35, armor: 0, dmg: 5, cd: 0.4, range: 0.1, speed: 5.6, radius: 0.375, light: true, hops: true, bounty: 5, budget: 2, unlock: 1, late: 2, weight: 5, hitFrac: 0.45, attackSeq: /^attack/i, soundAttack: 'zerglingAttack', soundDeath: 'zerglingDeath', height: 0.8, impactFx: 'zerglingImpact' },
    guardian: { name: 'Guardian', model: 'guardian', death: 'guardianDeath', hp: 150, armor: 2, dmg: 20, cd: 2, range: 9, speed: 2.2, radius: 1.0, armored: true, flying: true, fly: 3.8, prefer: 'mech', bounty: 60, budget: 20, unlock: 16, late: 1, weight: 0.25, hitFrac: 0.4, attackSeq: /^attack$/i, projectile: 'guardian', soundAttack: 'guardianFire', height: 1.4 },
    devourer: { name: 'Devourer', model: 'devourer', death: 'devourerDeath', hp: 250, armor: 2, dmg: 15, cd: 2, range: 6, speed: 2.8, radius: 1.0, armored: true, flying: true, fly: 4.4, antiAir: true, acid: 6, bounty: 60, budget: 18, unlock: 1, late: 2, weight: 1.2, hitFrac: 0.4, attackSeq: /^attack$/i, projectile: 'spores', soundDeath: 'devourerDeath', height: 1.4 },
    scourge: { name: 'Scourge', model: 'scourge', death: 'scourgeDeath', scale: 1.3, hp: 25, armor: 0, dmg: 110, cd: 1, range: 0.2, speed: 6.5, radius: 0.4, light: true, flying: true, fly: 4.6, antiAir: true, suicide: true, bounty: 8, budget: 3, unlock: 1, late: 2, weight: 2.5, hitFrac: 0, height: 0.6 },
    defiler: { name: 'Defiler', model: 'defiler', death: 'defilerDeath', hp: 80, armor: 1, dmg: 0, cd: 8, range: 9, speed: 2.6, radius: 0.6, spell: 'defiler', bounty: 50, budget: 14, unlock: 1, late: 2, weight: 2, hitFrac: 0, height: 1.2 },
    viper: { name: 'Viper', model: 'viper', death: 'viperDeath', hp: 150, armor: 1, dmg: 0, cd: 11, range: 9, speed: 4, radius: 0.75, flying: true, fly: 4, spell: 'abduct', bounty: 50, budget: 16, unlock: 1, late: 3, weight: 1.5, hitFrac: 0, height: 1.4 },
    // Mid game on: the Brood War queen hangs back and casts Spawn Broodling (see Game.castBroodling). Not tech-gated:
    // she joins at wave 14, and stays rare: never more than max of her on the field (see Game.startWave).
    broodqueen: { name: 'Brood Queen', model: 'broodqueen', death: 'broodqueenDeath', hp: 120, armor: 1, dmg: 0, cd: 9, range: 9, speed: 3.8, radius: 0.75, flying: true, fly: 4, spell: 'broodling', bounty: 45, budget: 14, unlock: 14, max: 2, weight: 0.6, hitFrac: 0, soundDeath: 'broodqueenDeath', height: 1.4 },
    // Not bought from the wave budget: special events scheduled by startWave.
    overlord: { name: 'Overlord', model: 'overlord', death: 'overlordDeath', hp: 400, armor: 1, dmg: 0, cd: 1, range: 0, speed: 3, radius: 1.0, armored: true, flying: true, fly: 4.2, transport: true, bounty: 25, budget: 0, unlock: 999, weight: 0, hitFrac: 0, soundDeath: 'overlordDeath', height: 2, uncounted: true },
    // Overlords from wave 3 (BS.OVERLORDS): watchers hang over the field glaring at the bunker (they never attack or hold up
    // the end of a wave); drop overlords unload zerg up the lane, worse ones as the waves go on. No overlord counts toward
    // the alive cap.
    // Hunts cloaked ghosts (BS.OVERSEERS): flies to the nearest one and hangs over it, so the swarm can see and kill it.
    overseer: { name: 'Overseer', model: 'overseer', death: 'overseerDeath', hp: 200, armor: 1, dmg: 0, cd: 1, range: 0, speed: 4.2, radius: 1.0, armored: true, flying: true, fly: 5.0, detector: true, uncounted: true, bounty: 25, budget: 0, unlock: 999, weight: 0, hitFrac: 0, soundDeath: 'overlordDeath', height: 2 },
    overlordWatch: { name: 'Overlord', model: 'overlordCreep', death: 'overlordCreepDeath', hp: 200, armor: 1, dmg: 0, cd: 1, range: 0, speed: 1.6, radius: 1.0, armored: true, flying: true, fly: 5.4, watcher: true, ambient: true, uncounted: true, bounty: 15, budget: 0, unlock: 999, weight: 0, hitFrac: 0, soundDeath: 'overlordDeath', height: 2 },
    overlordDrop: { name: 'Overlord', model: 'overlord', death: 'overlordDeath', hp: 250, armor: 1, dmg: 0, cd: 1, range: 0, speed: 2.6, radius: 1.0, armored: true, flying: true, fly: 4.2, transport: true, front: true, uncounted: true, bounty: 15, budget: 0, unlock: 999, weight: 0, hitFrac: 0, soundDeath: 'overlordDeath', height: 2 },
    // Nydus worms breach behind our lines or (lane) up on the plateaus beside the bunker, armoured enough to disgorge
    // their whole cargo unless focused.
    nydus: { name: 'Nydus Worm', model: 'nydus', death: null, hp: 900, armor: 3, dmg: 0, cd: 1, range: 0, speed: 0, radius: 1.3, armored: true, nydus: true, bounty: 60, budget: 0, unlock: 999, weight: 0, hitFrac: 0, height: 3 },
    broodling: { name: 'Broodling', model: 'broodling', death: 'broodlingDeath', scale: 0.75, hp: 30, armor: 0, dmg: 4, cd: 0.46, range: 0.1, speed: 4.13, radius: 0.25, light: true, life: 8, bounty: 1, budget: 0, unlock: 999, weight: 0, hitFrac: 0.45, attackSeq: /^attack/i, soundDeath: 'broodlingDeath', height: 0.5, impactFx: 'zerglingImpact' },
    // Creep (see BS.CREEP): queens walk with the waves planting creep tumors; tumors spread creep and bud one more
    // tumor each; overlords drift over the field generating creep beneath them.
    queen: { name: 'Queen', model: 'queen', death: 'queenDeath', hp: 175, armor: 1, dmg: 8, cd: 0.71, range: 5, speed: 2.4, radius: 0.875, tumors: true, bounty: 30, budget: 0, unlock: 999, weight: 0, hitFrac: 0.3, attackSeq: /^attack$/i, projectile: 'spine', soundDeath: 'queenDeath', height: 1.8 },
    creeptumor: { name: 'Creep Tumor', model: 'creeptumor', death: 'creeptumorDeath', hp: 50, armor: 0, dmg: 0, cd: 1, range: 0, speed: 0, radius: 0.5, tumor: true, bounty: 3, budget: 0, unlock: 999, weight: 0, hitFrac: 0, height: 0.6 },
    overlordCreep: { name: 'Overlord', model: 'overlordCreep', death: 'overlordCreepDeath', hp: 200, armor: 0, dmg: 0, cd: 1, range: 0, speed: 1.4, radius: 1.0, armored: true, flying: true, fly: 4.8, creeper: true, bounty: 20, budget: 0, unlock: 999, weight: 0, hitFrac: 0, soundDeath: 'overlordDeath', height: 2, uncounted: true },
    // The finale (see Game.startFinale): bosses keep their own hit points (no late-wave toughening) and always spawn,
    // whatever the alive cap. Kerrigan casts Leaping Strike and Apocalypse and is untouchable (nothing aims at her or hurts
    // her: you hold on until the Hyperion comes) and makes for the bunker; the leviathan bombards (bombard = splash radius) and spawns mutalisks.
    // See BS.FINALE.
    kerrigan: { name: 'Kerrigan', model: 'kerrigan', death: 'kerriganDeath', scale: 1.3, hp: 6000, armor: 3, dmg: 45, dmgStructure: 30, cd: 0.7, range: 1.2, speed: 3.4, radius: 0.9, armored: true, cleave: 1.4, boss: 'kerrigan', untouchable: true, prefer: 'bunker', bounty: 2500, budget: 0, unlock: 999, weight: 0, hitFrac: 0.4, attackSeq: /^attack/i, soundDeath: 'kerriganDeath', height: 2.4, deathHold: 6, impactFx: 'ultraliskImpact' },
    leviathan: { name: 'Leviathan', model: 'leviathan', death: 'leviathanDeath', scale: 1.6, hp: 6000, armor: 1, dmg: 40, cd: 2.4, range: 6, speed: 1.6, radius: 3.0, armored: true, flying: true, fly: 7.5, boss: 'leviathan', bombard: 2.4, bounty: 2500, budget: 0, unlock: 999, weight: 0, hitFrac: 0.45, projectile: 'bile', soundAttack: 'guardianFire', soundDeath: 'finaleRoar', height: 4, deathHold: 6 },
  };

  // Creep: a coarse grid (cell metres) of coverage 0..1 that grows where a source reaches and slowly fades (lingers) where
  // none does. Zerg ground units on creep move faster (SC2: +30%). Firebat flames burn it back and scorch the ground for
  // a while, and singe tumors. Queens join from fromWave, creep overlords two waves later. The swarm may keep more tumors
  // alive each wave (base + perWave per creep wave, up to max), so the creep spreads further as the waves go on.
  // Overlords from wave from: watchers (watch[0] a wave, plus watch[1] every 10 waves, at most max), each staying life
  // seconds; and drops (chance per wave, plus chanceGrow a wave up to chanceMax; a second one from second) unloading up
  // the lane n zerg picked from the latest of the cargo tiers the wave has reached.
  // Overseers: from wave from, while there are ghosts on the field, base + perGhost per ghost a wave. A ghost within
  // detect metres of one is revealed.
  BS.OVERSEERS = { from: 18, base: 1, perGhost: 0.5, detect: 5.5 };

  BS.OVERLORDS = { from: 3, watch: [0.7, 0.4], max: 3, life: [25, 45], chance: 0.35, chanceGrow: 0.02, chanceMax: 0.8, second: 15,
    cargo: [
      { from: 3, n: 4, units: ['zergling'] },
      { from: 8, n: 4, units: ['zergling', 'zergling', 'baneling'] },
      { from: 14, n: 3, units: ['baneling', 'roach', 'hydralisk'] },
      { from: 22, n: 4, units: ['roach', 'hydralisk', 'infested'] },
      { from: 30, n: 1, units: ['ultralisk'] },
    ] };

  BS.CREEP = {
    cell: 1, update: 0.25, on: 0.35, grow: 0.6, decay: 0.012, speed: 1.3, fromWave: 8,
    tumor: { radius: 4.5, growth: 0.45, spread: 12, hop: [3, 4.5], base: 4, perWave: 3, max: 30, minBunker: 6, gap: 2 },
    queen: { every: 9, first: [3, 6] },
    overlord: { radius: 3.2, life: 50, drift: 0.3, minDist: 6, drip: 1.0 },    // drip: how far under its middle the creep falls from
    burn: { radius: 1.6, regrow: 5, tumorDmg: 10 },
  };

  // The run lasts EVAC.at seconds of fighting (the clock stops while you shop), then the Hyperion evacuates everyone left:
  // the run is won (+score). She arrives `arrive` seconds before and comes down over the bunker from `from` to `hover`
  // while the fight goes on. finale: the first wave to start after that many seconds is the finale (BS.FINALE).
  BS.EVAC = { at: 1800, finale: 1680, arrive: 10, from: [0, -40, 14], hover: [0, 2.5, 6.5], scale: 1.2, score: 50000 };

  // The finale, the swarm's last push two minutes before the evac: Kerrigan, who cannot be stopped, and a leviathan lead it, and from then until the evac every wave's budget
  // is x budget, with every late-game strain in it, tech or not. Drop pods
  // land all round the base, behind the lines too (podMinR..podMaxR from the bunker): podLand is when the pod hits the
  // ground (its Birth sequence), crushing what it lands on (podDmg within podRadius), then it lets out podCargo zerg.
  // The difficulty curve over a 30-minute run (about 40 waves). Early waves ramp up steadily; past their knees the budget grows
  // (perWave[0] x + perWave[1] x^2 more, x waves past the knee) and the zergling swarm linearly (+swarmPerWave a wave, up
  // to swarmMax), so a maxed-out player meets a steady climb (new strains, creep, drops, milestone assaults) rather than
  // a wall of zerglings. len: wave length in seconds, len[0] at wave 1 to len[1] by lenBy. hp: zerg hit points x
  // (1 + hp x waves past hpFrom).
  // alive/aliveHard: how many zerg may be on the field at once, rising from [0] at wave aliveFrom to [1] by aliveBy (phones
  // scale it down); the rest wait their turn. bountyFrom/bountyFall: past wave bountyFrom a kill pays 1 / (1 + bountyFall
  // x waves past it) of its bounty (a third by wave 40; bosses pay in full), so a strong army stops snowballing minerals
  // and has to make every rebuy count.
  // tierFrom/tierEvery: the swarm evolves on its own as well as answering your tech: from tierFrom, every tierEvery waves
  // count as one more tech card for the late-game strains (see Game.lateTier). milestones: a named assault every three
  // waves from 25 (around 15 minutes in), on top of the budget: extra zerg, drop pods or nydus breaches. A run that
  // clears fast meets more of them before the finale.
  BS.WAVES = { alive: [160, 220], aliveHard: [200, 250], aliveFrom: 20, aliveBy: 40, len: [30, 30], lenBy: 12, knee: 16, perWave: [16, 0.4], swarmKnee: 12, swarmPerWave: 5, swarmMax: 360, hp: 0.02, hpFrom: 10,
    bountyFrom: 28, bountyFall: 0.15,
    tierFrom: 20, tierEvery: 5,
    milestones: {
      25: { name: 'HYDRALISK SWARM', toast: 'A hydralisk swarm pours down the lane!', zerg: { hydralisk: 36 } },
      28: { name: 'ULTRALISK STAMPEDE', toast: 'Ultralisks stampeding down the lane!', zerg: { ultralisk: 6 } },
      31: { name: 'AIR ARMADA', toast: 'A swarm of fliers darkens the sky!', zerg: { mutalisk: 12, guardian: 4, devourer: 3, broodlord: 2 } },
      34: { name: 'THE LEVIATHAN', toast: 'A leviathan looms over the field!', zerg: { leviathan: 1 } },
      37: { name: 'DROP POD ASSAULT', toast: 'Drop pods landing all round the base!', pods: 6 },
      40: { name: 'NYDUS BREACH', toast: 'Nydus worms breaching behind our lines!', nydus: 3 },
      43: { name: 'DARK SWARM', toast: 'Defilers lead a lurker siege!', zerg: { defiler: 4, lurker: 8, ultralisk: 4 } },
      46: { name: 'BROOD WAR', toast: 'The old brood returns in force!', zerg: { broodqueen: 2, guardian: 6, defiler: 2, ultralisk: 4 } },
    } };

  BS.FINALE = { budget: 1.5, pods: 8, podStart: 6, podEvery: 2, podLand: 2.7, podCargo: 4, podDmg: 60, podRadius: 2, podMinR: 7, podMaxR: 13,
    podZerg: ['hydralisk', 'hydralisk', 'roach', 'roach', 'lurker'],        // every other pod brings an ultralisk too
    leap: { cd: 8, min: 3, max: 9, dur: 0.7, dmg: 90, radius: 1.8 },        // Leaping Strike onto a defender out of reach
    apocalypse: { cd: 13, range: 9, delay: 1.2, dmg: 70, radius: 2 },       // a psionic blast on the thickest knot of defenders
    structure: 0.7,                                                         // what Apocalypse and Leaping Strike do to buildings
    // The rest of a maxed-out Heart of the Swarm kit. Her swings use every attack she has and sweep the wings (cleave);
    // every fury.every-th swing sends her into a Fury (half again the attack speed for fury.dur s). Kinetic Blast hits the
    // biggest machine in reach, Crushing Grip and the Immobilization Wave crush defenders (never buildings) and pin them (no firing for
    // stun s), Chain Lightning arcs through troops, Wild Mutation halves the damage the zerg round her take for dur s,
    // and she spawns banelings.
    kit: { kinetic: { cd: 9, range: 9, dmg: 160 }, grip: { cd: 14, range: 7, radius: 2.4, dmg: 50, stun: 2.5 },
      chain: { cd: 8, range: 8, jumps: 5, hop: 4, dmg: 45 }, wave: { cd: 22, radius: 5, min: 4, dmg: 60, stun: 2 },
      mutation: { cd: 20, radius: 9, min: 4, dur: 8 }, banelings: { cd: 16, count: 6 }, fury: { every: 8, dur: 3 } },
    brood: { cd: 15, count: 3 } };                                          // the leviathan spawns mutalisks

  // Bunker occupants.
  BS.INFANTRY = {
    marine: { name: 'Marine', icon: 'btn-unit-terran-marine', hp: 45, dmg: 6, cd: 0.61, range: 5, cost: 50, stimHp: 10, sound: 'marineFire', death: 'marineDeath', desc: 'Gauss rifle. Cheap, reliable, stims well.' },
    firebat: { name: 'Firebat', icon: 'btn-unit-terran-firebat', hp: 100, dmg: 8, dmgLight: 6, cd: 0.86, range: 2, splash: 1.3, groundOnly: true, cost: 100, stimHp: 10, sound: 'firebatFire', death: 'firebatDeath', desc: 'Flamethrower. Burns packs of lings and banelings.' },
    marauder: { name: 'Marauder', icon: 'btn-unit-terran-marauder', hp: 125, dmg: 10, dmgArmored: 10, cd: 1.07, range: 6, slow: 1.5, groundOnly: true, cost: 100, stimHp: 20, sound: 'marauderFire', death: 'marauderDeath', desc: 'Punisher grenades. Slows and shreds armoured.' },
  };

  BS.TERRAN = {
    bunker: { hp: 400, armor: 1, slots: 4, slotsUpgraded: 6, rangeBonus: 1, halfSize: 1.5 },
    scv: { hp: 45, repairRate: 13.8, speed: 3.94, radius: 0.375, cost: 50, max: 4 },
    tank: { hp: 175, armor: 1, dmg: 40, dmgArmored: 30, cd: 2.14, range: 13, minRange: 2, splash: [0.47, 0.78, 1.25], radius: 0.9, cost: 300, costStep: 100, maxLane: 4, maxArena: 4, high: 2 },
    // Air strike: four banshees hover over the lane (and its ledges) and fire missiles at passing zerg.
    banshee: { hp: 140, count: 4, dmg: 16, shots: 2, cd: 0.9, range: 6, speed: 9, fly: 2.4, hover: 14, cost: 200, cooldown: 32 },
    // Troops standing behind the bunker (ejected, or recruited while it is full).
    squad: { max: 48 },
    // Missile turrets: anti-air only, like SC2 (12 x2, range 7).
    sam: { hp: 250, armor: 0, dmg: 12, shots: 2, cd: 0.86, range: 7, radius: 1.0, cost: 100, max: 3, build: 3 },
    // Sensor tower: the radar only works while one stands.
    sensor: { hp: 200, armor: 0, radius: 0.8, cost: 125, build: 3 },
    // Forward base (lane): a maxed-out army takes the plateau east of the stem and an SCV builds a command center there,
    // which mines `income` minerals/s. `troopers` go up with the Odin and the goliaths; below `minSquad` they fall back.
    cc: { hp: 1500, armor: 1, radius: 1.5, scale: 0.6, build: 12, income: 3 },
    expand: { troopers: 16, minSquad: 24, clear: 7 },
    railsRange: 2,
    // High-tech reinforcements. g* = anti-ground weapon, a* = anti-air; shots per volley; walkers walk in from the south.
    goliath: { hp: 150, armor: 1, radius: 0.7, scale: 0.65, cost: 250, max: 4, speed: 3, gDmg: 12, gShots: 2, gCd: 0.9, gRange: 6, aDmg: 10, aShots: 2, aCd: 1.5, aRange: 8 },
    thor: { hp: 450, armor: 1, radius: 1.25, cost: 600, max: 2, speed: 2.2, gDmg: 30, gShots: 2, gCd: 1.28, gRange: 10, aDmg: 6, aShots: 4, aCd: 2.1, aRange: 12, aSplash: 0.6 },
    odin: { hp: 1500, armor: 2, radius: 2.2, cost: 2500, max: 1, speed: 2, gDmg: 60, gShots: 2, gCd: 1.1, gRange: 16, gSplash: 1.2, aDmg: 15, aShots: 6, aCd: 2, aRange: 16, aSplash: 0.8 },
    // Battlecruiser on station: hovers over the lane out of reach; laser batteries at air and ground, Yamato on its own.
    // Six laser turrets on its hardpoints (Ref_Weapon bones), each picking its own target and firing two-shot bursts.
    // pdRange: extra reach of the point defence against scourge and devourers; alertGap: seconds between threat warnings.
    bc: { hp: 1200, armor: 3, cost: 1500, laserG: 8, laserA: 5, laserCd: 0.16, range: 10, pdRange: 4, alertGap: 8, yamato: 240, yamatoCd: 20, yamatoCharge: 2, missiles: 6, missileCd: 6, missileDmg: 30, missileSplash: 1.2,
      turrets: ['Ref_Weapon 10', 'Ref_Weapon 13', 'Ref_Weapon 12', 'Ref_Weapon 15', 'Ref_Weapon 24', 'Ref_Weapon 25'] },
    // A ghost calls a tactical nuke on the densest pack well away from the bunker.
    nuke: { cost: 400, cooldown: 45, dmg: 300, radius: 7, delay: 3, minDist: 9 },
    // Four viking fighters patrol overhead hunting air for a while.
    // Reapers (the Reaper Squad card: squad[level] of them, losses replaced every wave): fast, light, hit-and-run
    // pistoleers that hop cliffs both ways. Two fire teams walk a beat round the plateaus (the arena's ring) at patrol m/s,
    // strung out gap m apart, and hunt ground zerg within hunt m that stay within leash m of their beat; every kd8.cd s a
    // KD8 charge on a pack (damage and a knock back). They regenerate out of combat, and below fallBack of their hit points
    // keep to the beat to do so.
    reaper: { hp: 60, squad: [0, 4, 6], dmg: 4, dmgLight: 5, shots: 2, cd: 1.1, range: 5, speed: 5.25, patrol: 2.2, gap: 1.1, hunt: 7, leash: 9, kite: 4, regen: 3, regenAfter: 4, fallBack: 0.4, scale: 1,
      kd8: { cd: 12, dmg: 10, radius: 1.6, knock: 2.2, range: 5, minPack: 3 } },
    // Ghosts: cloaked snipers on the high ground (lane: over the ramp tops, then the cliffs over the stem). C-10 rifle at
    // anything climbing to them, Steady Targeting on anything big and armoured. Overseers reveal them. Each ghost on the
    // field takes nukeCut off the tactical nuke's price and reload (up to 4).
    // Revealed, a ghost turns its guns on the overseer first and sidesteps evade metres away from it along the plateau.
    ghost: { hp: 150, armor: 1, evade: 3, cost: 200, max: 4, dmg: 10, dmgLight: 10, cd: 1.07, range: 6, speed: 3.94, nukeCut: 0.15,
      snipe: { dmg: 170, range: 10, cd: 8, channel: 1.43, minHp: 300 } },
    // Vikings and wraiths fly close air support: long strafing passes up and down the lane (round the arena) in a spread
    // wedge, always flying straight on, firing on whatever lies within cone radians of their nose and staying on that
    // target while it does. Ship Weapons adds to every shot.
    vikings: { hp: 135, cost: 300, cooldown: 35, count: 4, dmg: 10, shots: 2, cd: 0.55, range: 9, cone: 1.0, hover: 24, fly: 4.2, speed: 12, patrol: 11 },
    // Three wraiths sweep the lane for a while: burst lasers at ground zerg (gDmg every gCd), Gemini missiles at air.
    // Wraiths cloak for cloak.dur s when fired upon (an overseer still sees them), then not again for cloak.cd s.
    wraiths: { hp: 120, cost: 350, cooldown: 40, count: 3, gDmg: 8, gCd: 0.25, aDmg: 10, aShots: 2, aCd: 0.7, range: 7, cone: 1.0, hover: 24, fly: 3.4, speed: 12, patrol: 11,
      cloak: { dur: 6, cd: 12 } },
    // A science vessel on station for `stay` seconds, casting from `range`: Irradiate on the densest pack (dps to the
    // target and everything within radius, for dur s), Defensive Matrix on the most battered of the bunker, tanks and
    // mechs (absorbs hp damage for dur s).
    // Fired upon, the vessel pulls back behind the bunker and returns once it has gone calm seconds unhit.
    vessel: { hp: 200, armor: 1, cost: 400, cooldown: 50, stay: 30, fly: 5.2, speed: 4, scale: 1, range: 11, retreat: { y: -5, speed: 6, calm: 3 },
      irradiate: { cd: 7, dur: 6, dps: 18, radius: 1.8 }, matrix: { cd: 10, hp: 250, dur: 12 } },
    yamato: { dmg: 240, splash: 120, radius: 3, cost: 300, cooldown: 40 },
    stim: { duration: 11, cooldown: 13 },
    repair: { cost: 75, cooldown: 20, amount: 150 },
    hiSecRange: 1,
  };

  // costs: one entry per level (the last level is the max).
  BS.UPGRADES = {
    stim: { name: 'Stimpack', icons: ['btn-ability-terran-stimpack'], costs: [75], short: 'Unlocks STIM ability' },
    weapons: { name: 'Infantry Weapons', icons: ['btn-upgrade-terran-infantryweaponslevel1', 'btn-upgrade-terran-infantryweaponslevel2', 'btn-upgrade-terran-infantryweaponslevel3'], costs: [75, 130, 190, 265, 355, 450, 565, 675, 825, 975], short: '+1 infantry damage' },
    neosteel: { name: 'Neosteel Frame', icons: ['btn-upgrade-terran-neosteelframe'], costs: [115], short: 'Bunker holds six' },
    plating: { name: 'Building Armor', icons: ['btn-upgrade-terran-buildingarmor'], costs: [190, 265, 340, 450, 600, 750, 900, 1050], short: '+150 HP per level' },
    hisec: { name: 'Hi-Sec Auto Tracking', icons: ['btn-upgrade-terran-hisecautotracking'], costs: [150], short: '+1 range, bunker and tanks' },
    vehicle: { name: 'Vehicle Weapons', icons: ['btn-upgrade-terran-vehicleweaponslevel1', 'btn-upgrade-terran-vehicleweaponslevel2', 'btn-upgrade-terran-vehicleweaponslevel3'], costs: [115, 170, 225, 300, 395, 490, 600, 715, 865, 1015], short: '+5 tank damage, +2 walkers' },
    vplate: { name: 'Vehicle Plating', icons: ['btn-upgrade-terran-vehicleplatinglevel1', 'btn-upgrade-terran-vehicleplatinglevel2', 'btn-upgrade-terran-vehicleplatinglevel3'], costs: [130, 190, 260, 350, 460, 575, 700, 840, 1000, 1170], short: '+1 armour, tanks and walkers' },
    ship: { name: 'Ship Weapons', icons: ['btn-upgrade-terran-shipweaponslevel1', 'btn-upgrade-terran-shipweaponslevel2', 'btn-upgrade-terran-shipweaponslevel3'], costs: [115, 170, 225, 300, 395, 490, 600, 715, 865, 1015], short: '+1 damage, every ship' },
    splate: { name: 'Ship Plating', icons: ['btn-upgrade-terran-shipplatinglevel1', 'btn-upgrade-terran-shipplatinglevel2', 'btn-upgrade-terran-shipplatinglevel3'], costs: [130, 190, 260, 350, 460, 575, 700, 840, 1000, 1170], short: '+1 armour, every ship' },
    medivac: { name: 'Medivac on Station', icons: ['btn-ability-terran-heal'], costs: [150], short: 'Heals troops and SCVs' },
    rails: { name: 'Magnetic Rails', icons: ['btn-upgrade-terran-magrailmunitions'], costs: [225], short: '+2 bunker range' },
  };

  // High-tech reinforcements: a shop card puts each in the armoury (see BS.CARDS), where it is bought like a trooper.
  BS.TECH = [
    { id: 'goliath', name: 'Goliath', icon: 'btn-unit-terran-goliath', banner: 'GOLIATHS' },
    { id: 'nuke', name: 'Tactical Nuke', icon: 'btn-ability-terran-nuclearstrike', banner: 'TACTICAL NUKE' },
    { id: 'vikings', name: 'Viking Wing', icon: 'btn-unit-terran-vikingfighter', banner: 'VIKING WING' },
    { id: 'ghost', name: 'Ghost', icon: 'btn-unit-terran-ghost', banner: 'GHOSTS' },
    { id: 'wraiths', name: 'Wraith Squadron', icon: 'btn-unit-terran-wraith', banner: 'WRAITH SQUADRON' },
    { id: 'vessel', name: 'Science Vessel', icon: 'btn-unit-terran-sciencevessel', banner: 'SCIENCE VESSEL' },
    { id: 'thor', name: 'Thor', icon: 'btn-unit-terran-thor', banner: 'THOR' },
    { id: 'bc', name: 'Battlecruiser', icon: 'btn-unit-terran-battlecruiser', banner: 'BATTLECRUISER ON STATION' },
    { id: 'odin', name: 'Odin', icon: 'btn-unit-terran-odin', banner: 'THE ODIN' },
  ];

  // The shop. Every cleared wave (Game.endWave) pauses the fight and offers `offers` random cards, bought with banked
  // minerals; troops, tanks, SCVs, turrets and repairs stay in the armoury throughout. A reroll costs reroll[0], reroll[1] more
  // each time in the same visit. linger: seconds after a wave's last spawn before the shop opens anyway (stragglers wait,
  // frozen, while you shop). clearBonus: minerals for a wave wiped out, [0] + [1] x wave. research: weight multiplier for
  // research cards (up), the backbone of every build.
  BS.SHOP = { offers: 4, reroll: [25, 25], linger: 40, clearBonus: [25, 10], research: 2 };
  // Rarer cards turn up later (from: first wave they can be offered) and less often (weight). Every offer includes at
  // least one research card (up) while there is research left.
  BS.RARITY = {
    common: { name: 'COMMON', weight: 60, from: 1, color: '#c9d4e0' },
    rare: { name: 'RARE', weight: 28, from: 3, color: '#5fb4ff' },
    epic: { name: 'EPIC', weight: 10, from: 8, color: '#c08cff' },
    legendary: { name: 'LEGENDARY', weight: 3, from: 15, color: '#ffb340' },
  };
  // Shop cards. up: research one level of that BS.UPGRADES entry (its costs, icons and text); tech: put that BS.TECH
  // entry in the armoury; otherwise a passive with `max` levels at cost[level].
  // req: what this run needs before the card is offered. unlock: what it takes, over all your runs, before the card joins
  // the pool at all (buys: [card, n] = bought n times in all; best: reach that wave in either mode). See BS.Meta.
  BS.CARDS = [
    { id: 'weapons', up: 'weapons', rarity: 'common' },
    { id: 'neosteel', up: 'neosteel', rarity: 'common' },
    { id: 'plating', up: 'plating', rarity: 'common' },
    { id: 'vehicle', up: 'vehicle', rarity: 'common', req: 'tank' },
    { id: 'vplate', up: 'vplate', rarity: 'common', req: 'vehicle3' },
    // Ships: the air strike's banshees, vikings, wraiths, the science vessel and the battlecruiser (once the Starport is built).
    { id: 'ship', up: 'ship', rarity: 'common', req: 'vikings' },
    { id: 'splate', up: 'splate', rarity: 'common', req: 'ship2' },
    { id: 'stim', up: 'stim', rarity: 'common' },
    { id: 'hisec', up: 'hisec', rarity: 'rare' },
    { id: 'medivac', up: 'medivac', rarity: 'common', req: 'stim' },
    { id: 'rails', up: 'rails', rarity: 'epic', req: 'hisec' },
    { id: 'refinery', name: 'Refinery', icon: 'btn-building-terran-refinery', rarity: 'common', max: 5, cost: [100, 150, 200, 250, 300], desc: '+1 mineral a second' },
    { id: 'shield', name: 'Combat Shield', icon: 'btn-techupgrade-terran-combatshield-color', rarity: 'common', max: 3, cost: [100, 150, 200], desc: 'Troopers +10 HP' },
    { id: 'scvrepair', name: 'Advanced Repair', icon: 'btn-upgrade-swann-scvdoublerepair', rarity: 'common', max: 2, cost: [100, 175], desc: 'SCVs repair 50% faster' },
    { id: 'mule', name: 'MULE Drop', icon: 'btn-unit-terran-mule', rarity: 'rare', max: 3, cost: [150, 200, 250], desc: '+30 minerals as each wave starts' },
    { id: 'bounty', name: 'Bounty Contracts', icon: 'btn-building-terran-merccompound', rarity: 'rare', max: 3, cost: [175, 250, 325], desc: 'Kills pay 15% more' },
    { id: 'incinerator', name: 'Incinerator Gauntlets', icon: 'btn-upgrade-raynor-incineratorgauntlets', rarity: 'rare', max: 2, cost: [150, 225], desc: 'Firebat flames: wider, +2 damage' },
    { id: 'concussive', name: 'Concussive Shells', icon: 'btn-ability-terran-punishergrenade-color', rarity: 'rare', max: 2, cost: [150, 225], desc: 'Marauders: +1 s slow, +4 vs armored' },
    { id: 'medpacks', name: 'Stabilizer Medpacks', icon: 'btn-upgrade-raynor-stabilizermedpacks', rarity: 'rare', max: 2, cost: [150, 225], req: 'medivac', desc: 'Medivac heals 60% faster' },
    { id: 'firesup', name: 'Fire-Suppression System', icon: 'btn-upgrade-swann-firesuppressionsystem', rarity: 'rare', max: 2, cost: [200, 300], desc: 'Bunker regenerates 3 HP a second' },
    { id: 'rotors', name: 'Hyperflight Rotors', icon: 'btn-upgrade-terran-hyperflightrotors', rarity: 'rare', max: 2, cost: [150, 225], desc: 'Air strike: +1 banshee, 25% faster refuel' },
    { id: 'recruit', name: 'Recruitment Drive', icon: 'btn-building-terran-barracks', rarity: 'rare', max: 2, cost: [200, 300], desc: 'Troopers cost 15% less' },
    { id: 'spoils', name: 'Spoils of War', icon: 'btn-ability-terran-salvage', rarity: 'rare', max: 3, cost: [200, 275, 350], desc: '10% interest on banked minerals each shop (up to 50)', unlock: { best: 10 } },
    // Past the top of a research tree: levels without end (grow: each costs that much more than the last).
    { id: 'capacitors', name: 'Ultra-Capacitors', icon: 'btn-research-terran-ultracapacitors', rarity: 'common', max: 99, cost: 500, grow: 125, req: 'weapons10', desc: 'Infantry +1 damage' },
    { id: 'maelstrom', name: 'Maelstrom Rounds', icon: 'btn-techupgrade-terran-maelstromrounds', rarity: 'common', max: 99, cost: 500, grow: 125, req: 'vehicle10', desc: 'Tanks +5 damage, mechs +2' },
    { id: 'fortified', name: 'Neosteel Fortified Armor', icon: 'btn-upgrade-mengsk-engineeringbay-neosteelfortifiedarmor', rarity: 'common', max: 99, cost: 450, grow: 125, req: 'plating8', desc: 'Bunker +150 HP' },
    { id: 'vikings', tech: 'vikings', name: 'Starport', icon: 'btn-building-terran-starport', rarity: 'common', cost: 250, req: 'wave16', desc: 'Viking wing in the armoury' },
    { id: 'wraiths', tech: 'wraiths', name: 'Wraith Squadron', icon: 'btn-unit-terran-wraith', rarity: 'common', cost: 300, req: 'vikings', desc: 'Wraiths in the armoury: lasers and missiles' },
    // More fliers per call, up to twelve apiece.
    { id: 'vikingplus', name: 'Viking Reinforcements', icon: 'btn-unit-terran-vikingfighter', rarity: 'common', max: 8, cost: [200, 250, 300, 350, 400, 450, 500, 550], req: 'vikings', desc: 'Viking wing: +1 viking (up to 12)' },
    { id: 'wraithplus', name: 'Wraith Reinforcements', icon: 'btn-unit-terran-wraith', rarity: 'common', max: 9, cost: [225, 275, 325, 375, 425, 475, 525, 575, 625], req: 'wraiths', desc: 'Wraith squadron: +1 wraith (up to 12)' },
    { id: 'vessel', tech: 'vessel', name: 'Science Facility', icon: 'btn-building-terran-sciencefacility', rarity: 'rare', cost: 400, req: 'vikings', desc: 'Science vessel in the armoury: Irradiate, Defensive Matrix' },
    { id: 'siegerange', name: 'Siege Optics', icon: 'btn-upgrade-swann-vehiclerangeincrease', rarity: 'epic', cost: 300, req: 'tank', desc: 'Siege tanks +2 range', unlock: { buys: ['vehicle', 3] } },
    { id: 'goliath', tech: 'goliath', name: 'Factory', icon: 'btn-building-terran-factory', rarity: 'epic', cost: 300, req: 'vehicle2', desc: 'Goliaths in the armoury', unlock: { buys: ['vehicle', 5] } },
    { id: 'nuke', tech: 'nuke', name: 'Ghost Academy', icon: 'btn-building-terran-ghostacademy', rarity: 'epic', cost: 350, req: 'weapons3', desc: 'Tactical nukes in the armoury', unlock: { buys: ['weapons', 10] } },
    { id: 'reaper', name: 'Reaper Squad', icon: 'btn-unit-terran-reaper', rarity: 'rare', max: 2, cost: [250, 300], req: 'wave8', desc: 'Reapers patrol the high ground (4, then 6); losses replaced every wave', unlock: { best: 10 } },
    // Lane only: the plateaus either side of the stem. Goliaths and thors take posts up there (below them without it),
    // two more siege tanks deploy up there, and the Ghost Program needs it.
    { id: 'highground', name: 'High Ground', icon: 'btn-building-terran-sensordome', rarity: 'epic', cost: 350, req: ['tank', 'wave12'], lane: true, desc: 'Plateau posts: +2 siege tanks; goliaths and thors move up; ghosts' },
    { id: 'ghost', tech: 'ghost', name: 'Ghost Program', icon: 'btn-unit-terran-ghost', rarity: 'epic', cost: 400, req: ['nuke', 'highground'], desc: 'Cloaked ghosts on the high ground; each makes nukes cheaper', unlock: { buys: ['nuke', 1] } },
    { id: 'thor', tech: 'thor', name: 'Armory', icon: 'btn-building-terran-armory', rarity: 'epic', cost: 450, req: 'plating3', desc: 'Thors in the armoury', unlock: { buys: ['plating', 6] } },
    { id: 'bc', tech: 'bc', name: 'Fusion Core', icon: 'btn-building-terran-fusioncore', rarity: 'legendary', cost: 800, req: 'wave20', desc: 'The battlecruiser in the armoury', unlock: { best: 25 } },
    { id: 'odin', tech: 'odin', name: 'The Odin', icon: 'btn-unit-terran-odin', rarity: 'legendary', cost: 1200, req: 'bc', desc: 'The Odin in the armoury', unlock: { buys: ['bc', 1] } },
  ];

  // Between runs: every run earns vespene (gas per wave cleared, milestone extra every tenth, evac when the Hyperion took
  // you off, x arena in the arena), spent
  // at the Hyperion on permanent perks (costs: one per level). Saved in localStorage by BS.Meta (meta.js).
  BS.META = {
    gas: { perWave: 1, milestone: 5, evac: 25, arena: 1.5 },
    perks: [
      { id: 'minerals', name: 'War Chest', icon: 'btn-building-terran-commandcenter', desc: '+50 starting minerals', costs: [10, 20, 35, 50, 70] },
      { id: 'firebat', name: 'Veteran Fireteam', icon: 'btn-unit-terran-firebat', desc: 'Start with a firebat in the bunker', costs: [30] },
      { id: 'hull', name: 'Reinforced Bunker', icon: 'btn-building-terran-planetaryfortress', desc: '+50 bunker HP', costs: [15, 30, 45, 60, 80] },
      { id: 'luck', name: 'Field Intelligence', icon: 'btn-building-terran-sensortower-silver', desc: 'Rarer cards turn up more often', costs: [25, 50, 75] },
      { id: 'reroll', name: 'Logistics', icon: 'btn-ability-terran-calldownextrasupplies', desc: 'One free reroll every shop', costs: [40, 80] },
      { id: 'offer', name: 'Requisitions', icon: 'btn-building-terran-merccompound', desc: 'A fifth card in every shop', costs: [100] },
    ],
  };

  // The ladder: the forces on both sides grow run by run. The first run is marines against zerglings (lots of them);
  // every run that clears enough waves (LADDER_WAVES on the first rung, one more on each rung after) opens the next
  // rung: a Terran unit (or two) and a zerg strain (or a few).
  // terran: armoury items (infantry, scv, sam, sensor, tank), abilities (strike, yamato) and high-tech or unit shop
  // cards (by card id); zerg: BS.ZERG keys. Anything not listed is there from the start. The Hyperion still comes at
  // 30 minutes and Kerrigan at 28 whatever the rung: early runs simply fall sooner.
  BS.LADDER = [
    { t: 'Marines', z: 'Zerglings and winglings', terran: ['marine'], zerg: ['zergling', 'wingling'] },
    { t: 'Firebats', z: 'Hydralisks', terran: ['firebat'], zerg: ['hydralisk'] },
    { t: 'Marauders', z: 'Roaches', terran: ['marauder'], zerg: ['roach'] },
    { t: 'SCVs', z: 'Banelings', terran: ['scv'], zerg: ['baneling'] },
    { t: 'Siege tanks', z: 'Infested terrans', terran: ['tank'], zerg: ['infested'] },
    { t: 'Missile turrets and the air strike', z: 'Mutalisks and overlords', terran: ['sam', 'strike'], zerg: ['mutalisk', 'overlordWatch'] },
    { t: 'Sensor tower and reapers', z: 'Lurkers', terran: ['sensor', 'reaper'], zerg: ['lurker'] },
    { t: 'Goliaths and the high ground', z: 'Ultralisks', terran: ['goliath', 'highground'], zerg: ['ultralisk'] },
    { t: 'Vikings and wraiths', z: 'Brood lords and guardians', terran: ['vikings', 'wraiths'], zerg: ['broodlord', 'guardian'] },
    { t: 'Ghosts and the nuke', z: 'Brood queens and defilers', terran: ['ghost', 'nuke'], zerg: ['broodqueen', 'defiler', 'overseer'] },
    { t: 'Science vessel and the Yamato', z: 'Vipers, devourers and scourge', terran: ['vessel', 'yamato'], zerg: ['viper', 'devourer', 'scourge'] },
    { t: 'Thors', z: 'Creep, drops and nydus worms', terran: ['thor'], zerg: ['queen', 'overlordCreep', 'overlord', 'overlordDrop', 'nydus'] },
    { t: 'The battlecruiser and the Odin', z: 'The full swarm', terran: ['bc', 'odin'], zerg: [] },
  ];
  BS.LADDER_WAVES = 3;
  // The zergling swarm on top of each wave's budget, times this on the first rungs (they have little else to throw).
  BS.LADDER_SWARM = [3, 2.5, 2, 1.6, 1.3, 1.15];
  // ...and the cap on zerg alive at once, times this.
  BS.LADDER_ALIVE = [1.5, 1.35, 1.2, 1.1];
  // ...and in the lane this share of that swarm are winglings, hopping straight down the middle over the mesa.
  BS.LADDER_WINGLINGS = [0.3, 0.25, 0.2, 0.15, 0.1];
  // Zerglings evolve as a run goes on, as in StarCraft II: Metabolic Boost (speed x), +1 melee attack and +1 carapace at
  // each of the plating waves, then Adrenal Glands (attack cooldown x). New zerglings hatch with whatever the swarm has.
  BS.LING_EVO = { types: ['zergling', 'wingling'], boost: { wave: 4, speed: 1.35 }, plating: { waves: [7, 12, 17, 22, 27, 32], dmg: 1, armor: 1 }, adrenal: { wave: 14, cd: 0.7 } };
  // Shop cards that only make sense with a unit the ladder has not opened yet.
  BS.LADDER_CARD_NEEDS = { incinerator: 'firebat', concussive: 'marauder', scvrepair: 'scv', mule: 'scv', rotors: 'strike' };

  // Abilities on the action bar.
  BS.ABILITIES = [
    { id: 'stim', name: 'STIM', icon: 'btn-ability-terran-stimpack', key: 'S', accent: '#ffbf3c' },
    { id: 'strike', name: 'AIR STRIKE', icon: 'btn-unit-terran-banshee', key: 'A', accent: '#ff7a47' },
    { id: 'repair', name: 'REPAIR', icon: 'btn-ability-terran-repair', key: 'R', accent: '#5fe3ff' },
    { id: 'yamato', name: 'YAMATO', icon: 'btn-unit-terran-battlecruiser', key: 'Y', accent: '#bd6bff' },
  ];
})(window.BS);
