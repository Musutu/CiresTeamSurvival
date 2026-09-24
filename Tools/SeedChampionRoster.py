"""Author the initial roster from user direction. Refuses to overwrite edits."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path('C:/Users/Eric/Desktop/Unit Type.txt')
SKILLS={}
def skill(key,name,mechanic,family,delivery,status='planned'):
    SKILLS[key]=dict(id=key,displayName=name,status=status,mechanic=mechanic,vfxFamily=family,delivery=delivery)
    return key

for row in [
('iron_guard','Iron Guard','Take 40% less damage for eight seconds.','holy','self'),
('shield_slam','Shield Slam','Melee strength strike, two-second slow and monster threat.','holy','targeted'),
('war_cry','War Cry','Taunt nearby monsters/bots for six seconds and briefly guard.','war','ground_circle'),
('chain_spark','Chain Spark','Intelligence lightning chains to up to four nearby enemies.','arcane','chain'),
('ember_lance','Ember Lance','Authored moving fire skillshot with validated collision policies.','fire','projectile'),
('frost_bind','Frost Bind','Authored moving frost skillshot with slow on impact.','frost','projectile'),
('cleaving_strike','Cleaving Strike','Primary-stat melee strike hits nearby enemies.','steel','ground_circle'),
('piercing_shot','Piercing Shot','Authored physical skillshot with per-target hit limits.','steel','projectile'),
('shadow_step','Shadow Step','Dash toward an enemy and strike using agility.','shadow','targeted'),
('restoring_light','Restoring Light','Heal an ally or self using intelligence.','holy','ally'),
('sanctuary','Sanctuary','Heal nearby allies and briefly guard them.','holy','ground_circle'),
('purify','Purify','Remove an ally or self slow and restore health.','holy','ally'),
('venom_ground','Venom Ground','Delayed poison circle; entering applies poison and leaving removes this area contribution.','venom','ground_circle'),
('cinder_cone','Cinder Cone','Authored warning cone followed by fire-area damage.','fire','ground_cone'),
('grave_line','Grave Line','Authored warning line followed by spectral-area damage.','shadow','ground_line'),
('ashen_square','Ashen Ward','Authored square ground-area warning and effect.','ash','ground_square'),
('blight_sigil','Blight Sigil','Authored custom-polygon poison ground area.','venom','ground_polygon'),
('summoned_wall','Runestone Wall','Temporary destructible world wall with authored dimensions and collision.','stone','construct'),
('protection_dome','Aegis Dome','Temporary protection construct with authored projectile response.','holy','construct'),
('oathbound_guardian','Oathbound Guardian','One player-commandable allied unit with health, lifespan and attack stats.','spectral','summon'),
('spectral_pack','Spectral Pack','Three temporary AI companions attack the selected hostile target.','spectral','summon'),
('stone_skin','Stone Skin','Learned passive reduces incoming damage by ten percent.','stone','passive'),
('battle_rhythm','Battle Rhythm','Learned passive makes basic attacks twenty percent faster.','war','passive'),
('deep_reserves','Deep Reserves','Learned passive increases mana and energy regeneration by fifty percent.','arcane','passive'),
('soul_conduit','Soul Conduit','Learned passive increases healing by twenty-five percent.','holy','passive'),
('second_wind','Second Wind','Heal only yourself for eighteen percent maximum health; selected allies never receive the heal.','holy','self'),
('bastion_of_dawn','Bastion of Dawn','Ultimate heals self for thirty percent max health and guards nearby allies for eight seconds.','holy','ground_circle'),
('cataclysm','Cataclysm','Ultimate intelligence blast strikes up to twelve enemies near the selected target.','fire','ground_circle'),
('executioners_verdict',"Executioner's Verdict",'Ultimate primary-stat strike with capped missing-health bonus.','steel','targeted'),
('renewal','Renewal','Ultimate heals nearby living allies and clears their slows; no revival.','holy','ground_circle'),
]: skill(*row,status='implemented')

for row in [
('bear_maul','Gravewood Maul','Heavy claw strike generates extra threat against its target.','primal','targeted'),
('bear_roar','Deepwood Roar','A visible cone roar taunts enemies and weakens their next attack.','primal','ground_cone'),
('bear_charge','Rootbreaker Charge','Telegraphed short rush stops at a blocking wall and stuns the first enemy.','primal','ground_line'),
('bear_hibernate','Ironroot Slumber','Channel self-healing while stationary; taking damage can interrupt it.','nature','self'),
('bear_ancient_hide','Ancient Hide','After sustained incoming attacks gain a short defensive hide buff.','primal','passive'),
('bear_colossus','Elder of the Deepwood','Temporary larger bear form increases threat and defensive reach.','primal','transformation'),
('paladin_righteous_flail','Righteous Flail','Shield relic anchors a short flail cleave that marks threatened enemies.','holy','ground_cone'),
('paladin_relic_vow','Relic Vow','Bind a consenting ally to the relic for temporary shared protection.','holy','ally'),
('paladin_holy_flail','Merciful Censer','Flail impact releases a healing pulse around the struck hostile target.','holy','targeted'),
('paladin_pilgrim_light','Pilgrim Light','A slow visible orb heals the first allied unit it reaches.','holy','projectile'),
('miner_pickfall','Pickfall','Armor-chipping pick strike leaves a short vulnerability debuff.','stone','targeted'),
('miner_faultline','Faultline','Warning line erupts in stone after a delay and briefly slows enemies.','stone','ground_line'),
('miner_lantern','Deep Lantern','Place a lantern that reveals the local ground and grants allies resistance.','ember','construct'),
('miner_orehide','Orehide','Blocking damage builds a small temporary mineral shield.','stone','passive'),
('miner_mountain','Heart of the Mountain','A warned stone ring rises to protect allies and constrain enemy movement.','stone','ground_circle'),
('golem_granite_fist','Granite Fist','Large rocky golem slams a warned cone and generates tank threat.','stone','ground_cone'),
('golem_ether_anchor','Ether Anchor','Anchor an enemy briefly within a visible stone circle.','ether','ground_circle'),
('golem_moss_bloom','Verdant Bloom','Mossy rock opens a warned healing garden for allies.','nature','ground_circle'),
('golem_living_granite','Living Granite','Apply a moss-covered shield to an ally; effective healing restores a portion.','nature','ally'),
('golem_fel_fist','Felfire Fist','Fel-flamed golem punches through a short cone of enemies.','fel','ground_cone'),
('golem_ether_furnace','Ether Furnace','Consume stored ether to empower a short sequence of basic attacks.','fel','self'),
('golem_construct_core','Construct Core','Incoming damage slowly charges the selected golem role resource.','ether','passive'),
('golem_worldstone','Worldstone Awakened','Temporary ether overgrowth amplifies the selected tank, support or bruiser function.','ether','transformation'),
('chieftain_axe_hook','Chieftain Hook','An axe hook pulls the first unprotected target a short distance.','war','projectile'),
('chieftain_banner','Blood-Oath Banner','Plant a destructible banner that bolsters nearby allies.','war','construct'),
('chieftain_courage','Unbroken Clan','Nearby allies grant the chieftain a capped resilience bonus.','war','passive'),
('chieftain_earthshout','Earthshout','Large delayed shout taunts enemies and shields allies in a visible circle.','war','ground_circle'),
('behemoth_totem_sweep','Totem Sweep','Elephant-rhino hybrid swings its massive totem through a warned cone.','earth','ground_cone'),
('behemoth_tusk_line','Tuskbreaker','A heavy line charge stops at solid collision.','earth','ground_line'),
('behemoth_totem_bulwark','Totem Bulwark','Brace the massive totem as a temporary destructible barricade.','earth','construct'),
('behemoth_ancestral_weight','Ancestral Weight','Standing still briefly increases resistance and threat retention.','earth','passive'),
('behemoth_stampede','Ancestral Stampede','Warned spectral stampede crosses a broad ground lane.','earth','ground_line'),
('drakish_dragon_oath','Dragon Oath','Timed dragon form grants exactly two cleaving slash attacks, then breathes a weak fireball toward the furthest valid enemy to gain ranged threat; returning to human sword/shield form ends the sequence.','dragon','transformation'),
('drakish_scale_guard','Scale Guard','Brief dragon-scale defense protects the sword-and-shield footman.','dragon','self'),
('drakish_wing_rebuke','Wing Rebuke','Warned wing sweep knocks nearby enemies away from allies.','dragon','ground_cone'),
('drakish_ember_memory','Ember Memory','The weak threat fireball applies a short, minor burn; no full damage-spell scaling.','dragon','passive'),
('drakish_ancient_pact','Ancient Pact','Extend one dragon transformation with a visible landing circle and group guard.','dragon','transformation'),
('troll_axe_frenzy','Axe Frenzy','Rapid dual-axe melee sequence with a visible commitment window.','blood','targeted'),
('troll_blood_leap','Bloodbound Leap','Leap to a warned location and cleave on landing.','blood','ground_circle'),
('troll_twin_throw','Twin Throw','Launch two visible axes with independent collision and hit limits.','blood','projectile'),
('troll_returning_axes','Returning Axes','Recall thrown axes along warned return lanes; each target has a bounded hit count.','blood','ground_line'),
('troll_hunger','Berserker Hunger','Missing health grants a capped attack-speed bonus.','blood','passive'),
('troll_red_moon','Red Moon Frenzy','Brief ultimate empowers the selected melee or thrown dual-axe form.','blood','self'),
('dryad_root_snare','Root Snare','Warned roots hold enemies inside a small ground circle.','nature','ground_circle'),
('dryad_seed_mend','Seed Mend','Plant a healing seed on an ally that blooms after a short delay.','nature','ally'),
('dryad_thorn_line','Thornweave','A visible thorn line slows enemies crossing it.','nature','ground_line'),
('dryad_green_covenant','Green Covenant','Effective healing builds a small, capped regeneration reserve.','nature','passive'),
('dryad_grove_renewal','Grove Renewal','Grow a visible sanctuary of healing trees for living allies.','nature','ground_circle'),
('whisp_guiding_mote','Guiding Mote','Send a visible healing mote to the first allied unit in its path.','spirit','projectile'),
('whisp_spirit_tether','Spirit Tether','Short-range healing tether breaks immediately outside its range.','spirit','ally'),
('whisp_fey_trail','Fey Trail','Leave temporary luminous ground motes that restore allies on contact.','spirit','ground_line'),
('whisp_lantern_soul','Lantern Soul','Remaining near an injured ally improves resource regeneration.','spirit','passive'),
('whisp_constellation','Kindred Constellation','Visible links pulse healing to nearby living allies; no revival.','spirit','ground_circle'),
('centaur_grove_javelin','Grove Javelin','A root-tipped javelin harms enemies and leaves a small healing bloom.','nature','projectile'),
('centaur_trailblaze','Evergrove Trail','A warned forward path grants allied movement speed.','nature','ground_line'),
('centaur_herd_call','Herd Call','Call a temporary spectral grove companion with a bounded lifetime.','nature','summon'),
('centaur_steady_gait','Steady Gait','Briefly moving without damage strengthens the next effective heal.','nature','passive'),
('centaur_spring_march','Spring March','A moving grove aura sustains allies during a short formation advance.','nature','ground_circle'),
('keeper_dawn_beam','Dawn Beam','A warned light line heals allies and lightly damages enemies.','light','ground_line'),
('keeper_lantern_ward','Lantern Ward','Place a destructible ward that grants a small allied shield pulse.','light','construct'),
('keeper_beacon','Beacon of Return','Mark an allied rally point with a visible ground beacon and short haste buff.','light','ground_circle'),
('keeper_last_light','Last Light','A low-health ally can receive one small emergency shield per cooldown.','light','passive'),
('keeper_sunrise','Sunrise Vigil','A broad, clearly warned radiance restores living allies over time.','light','ground_circle'),
]: skill(*row)

def profile(id,name,family,variant,role,primary,archetype,style,description,art,actives,passive,ultimate,ranged=False):
    return dict(id=id,displayName=name,familyId=family,variant=variant,description=description,
      runtimeArchetype=archetype,primaryStat=primary,strength=20 if primary=='strength' else 10,
      agility=20 if primary=='agility' else 10,intelligence=20 if primary=='intelligence' else 10,
      basicAttackRange=(1500 if style in ('bow','axes') else 1300 if style=='lance' else 1200) if ranged else 220,attackSeconds=1.5,attackStyle=style,threatRole=role,
      roles=[role]+(['support'] if role=='healer' else []),artFamily=art,artStatus='prototype_fallback',
      artProvenance='User Unit Type.txt direction and authored dark-fantasy concept; no approved production model is bound by this roster.',
      startsWithSkills=[],actives=[SKILLS['second_wind' if (role=='tank' and s in ('restoring_light','sanctuary','purify')) or (role=='damage' and s=='war_cry') else s] for s in actives.split()],passive=SKILLS[passive],ultimate=SKILLS[ultimate])

PROFILES=[
profile('knight','Iron Warden','knight','Crusader','tank','strength',0,'sword','God-backed crusader; the existing Warden identity becomes the Knight profile.','blackened crusader plate, shield, muted gold relics','shield_slam iron_guard war_cry cleaving_strike restoring_light protection_dome','stone_skin','bastion_of_dawn'),
profile('ranger','Ash Ranger','ranger','Waystalker','damage','agility',1,'bow','A patient hunter combining arrows, frost and poisoned ground.','ash-grey leather, hood, weathered bow','piercing_shot frost_bind shadow_step venom_ground grave_line spectral_pack','battle_rhythm','executioners_verdict',True),
profile('scholar','Veil Scholar','wizard','Healer','healer','intelligence',2,'arcane','The existing Scholar becomes the healing Wizard variant.','veiled scholar robes, pale runes, floating focus','restoring_light sanctuary purify chain_spark ember_lance protection_dome','soul_conduit','renewal',True),
profile('lancer','Dusk Lancer','lancer','Skirmisher','damage','agility',3,'lance','A mobile lance wielder with precise thrusts and sweeping melee control.','dark segmented armor, long runed lance','cleaving_strike piercing_shot shadow_step iron_guard war_cry ashen_square','battle_rhythm','executioners_verdict',True),
profile('summoner','Rift Summoner','summoner','Binder','damage','intelligence',4,'arcane','Retained requested class: one commandable guardian and a three-unit AI spectral pack.','rift-marked robes, spirit chains, summoning focus','oathbound_guardian spectral_pack summoned_wall protection_dome chain_spark venom_ground','deep_reserves','cataclysm',True),
profile('bear','Gravewood Bear','bear','Elderhide','tank','strength',0,'claws','An ancient bear holds enemies with claws, roars and resilient hide.','large scarred bear, root growth, restrained amber eyes','bear_maul bear_roar bear_charge bear_hibernate iron_guard war_cry','bear_ancient_hide','bear_colossus'),
profile('paladin_righteous','Relic Paladin','paladin','Righteous','tank','strength',0,'flail','Class-selection Righteous path uses a shield relic and flail for tanking.','shield relic, spiked flail, dark plate, restrained gold','paladin_righteous_flail paladin_relic_vow shield_slam iron_guard war_cry sanctuary','stone_skin','bastion_of_dawn'),
profile('paladin_holy','Relic Paladin','paladin','Holy','healer','intelligence',2,'flail','Class-selection Holy path keeps shield relic and flail while supporting allies.','same shield relic and flail, ivory cloth, soft holy censer light','paladin_holy_flail paladin_pilgrim_light paladin_relic_vow restoring_light sanctuary purify','soul_conduit','renewal'),
profile('dwarf_miner','Deepdelve Miner','dwarf_miner','Oath of Stone','tank','strength',0,'axes','A dwarf miner uses pick, lantern and faultlines to protect the party.','stout armored dwarf, mining pick, warm lantern, ore plates','miner_pickfall miner_faultline miner_lantern summoned_wall iron_guard war_cry','miner_orehide','miner_mountain'),
profile('ether_golem_tank','Ether Golem','ether_golem','Granite Tank','tank','strength',0,'claws','Ether constructs a large rocky tank golem at class selection.','large granite boulders, ether-lit joints, broad silhouette','golem_granite_fist golem_ether_anchor iron_guard war_cry summoned_wall shield_slam','golem_construct_core','golem_worldstone'),
profile('ether_golem_support','Ether Golem','ether_golem','Verdant Support','healer','intelligence',2,'arcane','Ether constructs a verdant, green-mossed rock golem for healing and support.','mossy green stone, living vines, subtle emerald ether','golem_moss_bloom golem_living_granite restoring_light sanctuary purify protection_dome','soul_conduit','golem_worldstone',True),
profile('ether_golem_bruiser','Ether Golem','ether_golem','Felfire Bruiser','damage','strength',0,'claws','Ether constructs a green fel-flamed bruiser golem for melee damage.','cracked dark rock, green fel flames, heavy striking arms','golem_fel_fist golem_ether_furnace cleaving_strike cinder_cone shadow_step iron_guard','golem_construct_core','golem_worldstone'),
profile('orc_chieftain','Blood-Oath Chieftain','orc_chieftain','Clan Bulwark','tank','strength',0,'axes','An orc chieftain anchors the frontline with clan banners and brutal axe control.','broad orc, black iron, clan banner, heavy axe','chieftain_axe_hook chieftain_banner war_cry iron_guard cleaving_strike shield_slam','chieftain_courage','chieftain_earthshout'),
profile('totemic_behemoth','Totemic Behemoth','totemic_behemoth','Ancestral Bulwark','tank','strength',0,'totem','Large elephant/rhino hybrid carries a massive totem for smashing and defense.','elephant-rhino hybrid, massive carved totem, ritual stone armor','behemoth_totem_sweep behemoth_tusk_line behemoth_totem_bulwark war_cry iron_guard cleaving_strike','behemoth_ancestral_weight','behemoth_stampede'),
profile('drakish_footman','Drakish Footman','drakish_footman','Dragon Pact','tank','strength',0,'sword','Human sword-and-shield tank; timed dragon form supplies two cleaves then a weak furthest-target fireball for threat.','human scale-mail footman plus separate dragon form, ember-edged scales','drakish_dragon_oath drakish_scale_guard drakish_wing_rebuke shield_slam war_cry iron_guard','drakish_ember_memory','drakish_ancient_pact'),
profile('wizard','Cinder Arcanist','wizard','Damage','damage','intelligence',2,'arcane','The damage Wizard commands fire, frost, lightning and warned ground geometry.','charred robes, runic staff, restrained ember and violet glow','ember_lance frost_bind chain_spark cinder_cone grave_line ashen_square','deep_reserves','cataclysm',True),
profile('troll_berserker_melee','Red-Moon Berserker','troll_berserker','Melee Dual Axes','damage','agility',1,'axes','Agile troll rapidly attacks in melee with dual axes.','lean muscular troll, tusks, ritual wraps, paired axes','troll_axe_frenzy troll_blood_leap cleaving_strike shadow_step iron_guard war_cry','troll_hunger','troll_red_moon'),
profile('troll_berserker_ranged','Red-Moon Berserker','troll_berserker','Thrown Dual Axes','damage','agility',1,'axes','Agile troll throws dual axes with visible travel and bounded return hits.','same troll family, paired throwing axes, dark red ritual wraps','troll_twin_throw troll_returning_axes piercing_shot frost_bind shadow_step venom_ground','troll_hunger','troll_red_moon',True),
profile('dryad','Thornweave Dryad','dryad','Grove Tender','healer','intelligence',2,'staff','A woodland spirit controls roots while tending injured allies.','bark skin, thorn crown, sparse verdant leaves, wooden staff','dryad_root_snare dryad_seed_mend dryad_thorn_line restoring_light sanctuary purify','dryad_green_covenant','dryad_grove_renewal',True),
profile('whisp','Lantern Whisp','whisp','Kindred Guide','healer','intelligence',2,'arcane','A small luminous spirit guides and sustains the party through a dark world.','floating spirit core, wispy pale-green ribbons, restrained halo','whisp_guiding_mote whisp_spirit_tether whisp_fey_trail restoring_light purify protection_dome','whisp_lantern_soul','whisp_constellation',True),
profile('evergrove_centaur','Evergrove Centaur','evergrove_centaur','Spring Warden','healer','intelligence',2,'lance','A mobile centaur carries grove javelins and leads allies along healing trails.','antlered woodland centaur, weathered barding, grove javelin','centaur_grove_javelin centaur_trailblaze centaur_herd_call restoring_light sanctuary purify','centaur_steady_gait','centaur_spring_march',True),
profile('keeper_of_light','Keeper of the Light','keeper_of_light','Last Lantern','healer','intelligence',2,'staff','A solemn lantern keeper protects living allies with beams, wards and rallying light.','aged robed guardian, tall lantern staff, muted gold-white light','keeper_dawn_beam keeper_lantern_ward keeper_beacon restoring_light sanctuary purify','keeper_last_light','keeper_sunrise',True),
]

def build():
    raw=SOURCE.read_bytes();raw.decode('cp1252')
    return dict(schemaVersion=1,profile='CireChampionRoster',engine='Unreal',engineVersion='5.8.3',
        statPolicy='existing_stat_per_point',source=dict(path=SOURCE.as_posix(),encoding='windows-1252',sha256=hashlib.sha256(raw).hexdigest()),champions=PROFILES)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--overwrite',action='store_true');a=p.parse_args()
    dest=ROOT/'Content/Data/ChampionRoster.json'
    if dest.exists() and not a.overwrite:p.exit(1,'Roster already exists; edit it directly or explicitly pass --overwrite.\n')
    dest.write_text(json.dumps(build(),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Wrote {len(PROFILES)} profiles: {dest}')
if __name__=='__main__':main()
