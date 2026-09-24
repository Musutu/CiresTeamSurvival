# Audio provenance

Every sound shipped in `Content/Audio/{Music,Ambience,Footsteps,SFX,UI}` comes from the sources below.
All are free for commercial use and redistribution: **CC0 1.0** (no attribution required) or **CC BY 4.0**
(attribution required - given in-game under Options > Audio > Music credits, and here).
The machine-readable record with download URLs and SHA-256 hashes is `Art/Audio/AudioSources.json`;
`Art/Audio/ProcessReport.json` lists every processed output, its source keys and measured levels.
Rebuild: `Tools/FetchAudioSources.py` -> `Tools/DecodeAudioSources.py` -> `Tools/ProcessAudio.py` -> `Tools/BuildAudioContent.py`.
Freesound files are the site's public HQ previews (Ogg Vorbis) of CC0 uploads; the licence of each upload was
checked by the fetch script, which refuses anything that is not CC0.

The pre-existing `Content/Audio/CireCombat` and `Content/UI/WowUI/Sounds` sounds are original synthesized work
from earlier passes and are not covered here.

## Music - Kevin MacLeod (incompetech.com), CC BY 4.0

Required attribution (also shown in game):

> "The Pyre" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Oppressive Gloom" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Five Armies" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Crusade" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Killers" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Black Vortex" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Death and Axes" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Hero Theme" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

> "Greta Sting" Kevin MacLeod (incompetech.com)  
> Licensed under Creative Commons: By Attribution 4.0 License  
> http://creativecommons.org/licenses/by/4.0/  

| Asset | Title | Used for | Download |
|---|---|---|---|
| `Music/MUS_ThePyre` | The Pyre | town / prep (night) | https://incompetech.com/music/royalty-free/mp3-royaltyfree/The%20Pyre.mp3 |
| `Music/MUS_OppressiveGloom` | Oppressive Gloom | town / recovery alternate | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Oppressive%20Gloom.mp3 |
| `Music/MUS_FiveArmies` | Five Armies | combat waves | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Five%20Armies.mp3 |
| `Music/MUS_Crusade` | Crusade | combat waves alternate | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Crusade.mp3 |
| `Music/MUS_Killers` | Killers | Pack Leader / lane boss | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Killers.mp3 |
| `Music/MUS_BlackVortex` | Black Vortex | boss alternate | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Black%20Vortex.mp3 |
| `Music/MUS_DeathandAxes` | Death and Axes | arena PvP | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Death%20and%20Axes.mp3 |
| `Music/MUS_HeroTheme` | Hero Theme | victory stinger | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Hero%20Theme.mp3 |
| `Music/MUS_GretaSting` | Greta Sting | defeat stinger | https://incompetech.com/music/royalty-free/mp3-royaltyfree/Greta%20Sting.mp3 |

## Sound effects and ambience - Freesound.org, CC0 1.0

| Key | Title | Author | Page | Shipped as |
|---|---|---|---|---|
| wind_rolling | Rolling Wind - looping | ERR0 | https://freesound.org/people/ERR0/sounds/210220/ | AMB_WindRolling |
| wind_howl | Cold Howling Wind/Breeze (Loopable) | The_Isot_is_Back | https://freesound.org/people/The_Isot_is_Back/sounds/638434/ | AMB_WindHowl |
| crows | crow-calls.wav | a1234 | https://freesound.org/people/a1234/sounds/26959/ | AMB_Crow_01, AMB_Crow_02, AMB_Crow_03, AMB_Crow_04, AMB_Crow_05 |
| chains | chains rattling.wav | simosco | https://freesound.org/people/simosco/sounds/235534/ | AMB_Chains_01, AMB_Chains_02, AMB_Chains_03, AMB_Chains_04 |
| gate_creak | Gate door drawbridge wooden creaky opens closes hinge.wav | WavJunction.com | https://freesound.org/people/WavJunction.com/sounds/456763/ | AMB_GateCreak_01, AMB_GateCreak_02 |
| market_festival | Voices, footsteps on the sandy road. | hobonski | https://freesound.org/people/hobonski/sounds/703644/ | AMB_MarketFestival |
| market_square | CROWD village square.wav | nicoproson | https://freesound.org/people/nicoproson/sounds/648483/ | AMB_MarketSquare |
| merchant_calls | street vendor shouting not that loud and no mic arabic Gaza 2016.wav | kyles | https://freesound.org/people/kyles/sounds/406578/ | AMB_MerchantCall_01, AMB_MerchantCall_02, AMB_MerchantCall_03, AMB_MerchantCall_04 |
| ox_cart | G52-05-Ox Cart.wav | craigsmith | https://freesound.org/people/craigsmith/sounds/437078/ | AMB_Cart_01, AMB_Cart_02 |
| dogs_distant | Distant Dogs | IENBA | https://freesound.org/people/IENBA/sounds/820267/ | AMB_Dog_01, AMB_Dog_02, AMB_Dog_03 |
| dog_corgi | Dogs Barking in Distance_Rural.wav | rvandemark | https://freesound.org/people/rvandemark/sounds/581478/ | AMB_Dog_04 |
| wind_chimes | Wind chimes 1 | giddster | https://freesound.org/people/giddster/sounds/437337/ | AMB_WindChimes |
| hearth | Hearthfire (Louder) | SilverIllusionist | https://freesound.org/people/SilverIllusionist/sounds/836535/ | AMB_Hearth, EMT_Hearth |
| night_crickets_owl | CricketsandTawnyOwl.wav | raoul_slayer | https://freesound.org/people/raoul_slayer/sounds/203598/ | AMB_NightCrickets |
| fountain | bubbling fountain.wav | cognito perceptu | https://freesound.org/people/cognito%20perceptu/sounds/36086/ | EMT_Fountain |
| fountain_small | Ambiance_Fountain_Small_Loop_Stereo.wav | Nox_Sound | https://freesound.org/people/Nox_Sound/sounds/676173/ | AMB_FountainBed |
| pigeons | pidgeons cooing city park.wav | pawsound | https://freesound.org/people/pawsound/sounds/154865/ | AMB_Pigeons |
| bell_short | R04-59-Short Bell Tolls.wav | craigsmith | https://freesound.org/people/craigsmith/sounds/479985/ | AMB_Bell_01, AMB_Bell_02, AMB_Bell_03, SFX_PrepBell |
| bell_funeral | funeral bell-cloche funèbre.wav | aoristos | https://freesound.org/people/aoristos/sounds/329324/ | AMB_BellDistant_01 |
| flag | A flag flapping in the wind, at the small village of Assem Souk, in the High Atlas (Morocco). | felix.blume | https://freesound.org/people/felix.blume/sounds/154794/ | AMB_Flag |
| blacksmith | The sound environment of a festival of medieval culture. Sounds of hammer hitting an anvil. The work of a blacksmith. | hobonski | https://freesound.org/people/hobonski/sounds/703642/ | AMB_Blacksmith_01 |
| anvil | Hammer and anvil | Duasun | https://freesound.org/people/Duasun/sounds/321889/ | AMB_Anvil_01, AMB_Anvil_02, AMB_Anvil_03, AMB_Anvil_04 |
| guards_march | R23-64-Wagon and Soldiers Marching.wav | craigsmith | https://freesound.org/people/craigsmith/sounds/480671/ | AMB_GuardsMarch_01 |
| knight_walk | Footsteps Knight team - Walk & run, castle hall | Vrymaa | https://freesound.org/people/Vrymaa/sounds/770083/ | FS_Plate_Stone_01, FS_Plate_Stone_02, FS_Plate_Stone_03, FS_Plate_Stone_04, FS_Plate_Stone_05, FS_Plate_Stone_06, FS_Plate_Stone_07, FS_Plate_Stone_08 |
| boots_pavement | Footsteps medieval boots on pavement | YannSauvin | https://freesound.org/people/YannSauvin/sounds/777673/ | FS_Leather_Stone_01, FS_Leather_Stone_02, FS_Leather_Stone_03, FS_Leather_Stone_04, FS_Leather_Stone_05, FS_Leather_Stone_06, FS_Leather_Stone_07, FS_Leather_Stone_08 |
| steps_dirt | Footsteps Dirt 01 | aglinder | https://freesound.org/people/aglinder/sounds/264469/ | FS_Leather_Dirt_01, FS_Leather_Dirt_02, FS_Leather_Dirt_03, FS_Leather_Dirt_04, FS_Leather_Dirt_05, FS_Leather_Dirt_06, FS_Leather_Dirt_07, FS_Leather_Dirt_08 |
| chainmail_grass | Knight Left Footstep Forest/Grass 5 (With Chainmail) | Ali_6868 | https://freesound.org/people/Ali_6868/sounds/384901/ | FS_Plate_Dirt_01 |
| chainmail_gravel | Knight Right Footstep on Gravel 5 (With Chainmail) | Ali_6868 | https://freesound.org/people/Ali_6868/sounds/384890/ | FS_Plate_Dirt_02 |
| armor_chinks | Armor Chinks.WAV | lukabea | https://freesound.org/people/lukabea/sounds/707667/ | FS_ArmorRattle_01, FS_ArmorRattle_02, FS_ArmorRattle_03, FS_ArmorRattle_04 |
| leather_creak | Leather Creak / Stretching | IENBA | https://freesound.org/people/IENBA/sounds/536187/ | FS_LeatherCreak_01, FS_LeatherCreak_02, FS_LeatherCreak_03, FS_LeatherCreak_04 |
| hooves_cobble | clatter of hooves.wav | skitchscharff | https://freesound.org/people/skitchscharff/sounds/204961/ | FS_Hoof_Stone_01, FS_Hoof_Stone_02, FS_Hoof_Stone_03, FS_Hoof_Stone_04, FS_Hoof_Stone_05 |
| hooves_2 | clatter of hooves 2.wav | maciejadach | https://freesound.org/people/maciejadach/sounds/571312/ | FS_Hoof_Stone_06, FS_Hoof_Stone_07, FS_Hoof_Stone_08 |
| horses_dirt | G38-13-Group of Horses Walking.wav | craigsmith | https://freesound.org/people/craigsmith/sounds/437108/ | FS_Hoof_Dirt_01, FS_Hoof_Dirt_02, FS_Hoof_Dirt_03, FS_Hoof_Dirt_04, FS_Hoof_Dirt_05, FS_Hoof_Dirt_06 |
| shimmer | Shimmer | adh.dreaming | https://freesound.org/people/adh.dreaming/sounds/632344/ | FS_Shimmer_01, FS_Shimmer_02 |
| monster_stomp | Big Monster Stomp | Yoyamen1212 | https://freesound.org/people/Yoyamen1212/sounds/812538/ | FS_Beast_Stone_01, FS_Beast_Stone_02, FS_Beast_Stone_03 |
| bass_stomp | Stomping Ground Super Bassy | oscaraudiogeek | https://freesound.org/people/oscaraudiogeek/sounds/334228/ | FS_Beast_Stone_04, FS_Beast_Stone_05, FS_Beast_Stone_06 |
| monster_gravel | Monster footsteps on gravel | AudioPapkin | https://freesound.org/people/AudioPapkin/sounds/712066/ | FS_Beast_Dirt_01, FS_Beast_Dirt_02, FS_Beast_Dirt_03, FS_Beast_Dirt_04, FS_Beast_Dirt_05, FS_Beast_Dirt_06 |
| fire_loop | Crackling Flames (loop) | NickTayloe | https://freesound.org/people/NickTayloe/sounds/813328/ | AUR_FireLoop, EMT_FireCrackle |
| well | water well with knob and rope.wav | Fedor_Ogon | https://freesound.org/people/Fedor_Ogon/sounds/649240/ | EMT_Well |
| war_horn | war horn.wav | adharca | https://freesound.org/people/adharca/sounds/539956/ | SFX_WarHorn |
| war_horn_distant | Distant War Horn.wav | DeVern | https://freesound.org/people/DeVern/sounds/512490/ | SFX_WarHornDistant |
| war_drums | warriordrums.wav | Sclolex | https://freesound.org/people/Sclolex/sounds/209546/ | SFX_WarDrums |
| roar | Scary Monster Roar #2 | NicknameLarry | https://freesound.org/people/NicknameLarry/sounds/489901/ | SFX_PackLeaderRoar |
| roar_growl | Growl and Roar | Jofae | https://freesound.org/people/Jofae/sounds/366837/ | SFX_PackLeaderGrowl, SFX_PackLeaderRoar |
| teleport | Ethereal Teleport | Breviceps | https://freesound.org/people/Breviceps/sounds/466829/ | SFX_TeleportArrive |
| teleport_hum | Humming Magical Orb | SnowFightStudios | https://freesound.org/people/SnowFightStudios/sounds/670641/ | SFX_TeleportChannel |
| sword_draw | Sword draw unsheathe | SamsterBirdies | https://freesound.org/people/SamsterBirdies/sounds/581594/ | SFX_AggroWarning |
| fanfare | Trumpet Fanfare | bevibeldesign | https://freesound.org/people/bevibeldesign/sounds/350428/ | SFX_LevelUp |
| coins | 1_Coins.ogg | jalastram | https://freesound.org/people/jalastram/sounds/223343/ | SFX_CoinsBuy, SFX_LootPickup |
| aura_heartbeat | Heavy Heartbeat | MickBoere | https://freesound.org/people/MickBoere/sounds/276578/ | AUR_Heartbeat |
| aura_snarl | Monster Snarl 4 | pikachu09 | https://freesound.org/people/pikachu09/sounds/204611/ | AUR_Snarl |
| aura_blood_splat | Blood Spatter_Squelch_Near_Mono | _stubb | https://freesound.org/people/_stubb/sounds/406582/ | AUR_BloodSplat_01, AUR_BloodSplat_02, AUR_BloodSplat_03 |
| aura_swing | Knife/sword swing | spycrah | https://freesound.org/people/spycrah/sounds/471097/ | AUR_Swing |
| aura_ice_break | Ice break | humanoide9000 | https://freesound.org/people/humanoide9000/sounds/329744/ | AUR_IceShatter_01, AUR_IceShatter_02, AUR_IceShatter_03 |
| aura_ice_crack | Ice Crack 1 | j_p_higgins | https://freesound.org/people/j_p_higgins/sounds/262635/ | AUR_IceCrack |
| aura_chime | Chimes Sparkle | lukabea | https://freesound.org/people/lukabea/sounds/660493/ | AUR_Chime |
| aura_choir | Short Choir | Breviceps | https://freesound.org/people/Breviceps/sounds/444491/ | AUR_Choir |
| aura_sparkle | Sparkling Star 01.wav | LilMati | https://freesound.org/people/LilMati/sounds/462095/ | AUR_Sparkle |
| aura_clang | heavy sword against sword or shield | marchelonia | https://freesound.org/people/marchelonia/sounds/588278/ | AUR_Clang_01, AUR_Clang_02, AUR_Clang_03 |
| aura_bubbles | bubbles_stereo_02_long.wav | monosfera | https://freesound.org/people/monosfera/sounds/645909/ | AUR_Bubble, AUR_BubblesLoop |
| aura_flame | Flame Burst | magnuswaker | https://freesound.org/people/magnuswaker/sounds/592572/ | AUR_FlameBurst |
| aura_forcefield | Force Field 02.wav | LilMati | https://freesound.org/people/LilMati/sounds/702772/ | AUR_ForceField |
| aura_hourglass | Hourglass - Sand flow light | Vrymaa | https://freesound.org/people/Vrymaa/sounds/825859/ | AUR_Hourglass |
| aura_gong | Deep Gong Tolling.mp3 | Yin_Yang_Jake007 | https://freesound.org/people/Yin_Yang_Jake007/sounds/415200/ | AUR_Gong |
| aura_warcry | Battle Cry | mellotrix9898 | https://freesound.org/people/mellotrix9898/sounds/771323/ | AUR_WarCry |
| aura_whoosh | Whoosh | qubodup | https://freesound.org/people/qubodup/sounds/60013/ | AUR_Whoosh |
| arena_wheat_wind | Wheat in the Wind | bdvictor | https://freesound.org/people/bdvictor/sounds/240914/ | - |
| arena_field | Wheat Field Ambience | florianreichelt | https://freesound.org/people/florianreichelt/sounds/447810/ | - |
| arena_skylark | skylark.wav | squashy555 | https://freesound.org/people/squashy555/sounds/244357/ | - |
| arena_sea | Sound of the Sea | florianreichelt | https://freesound.org/people/florianreichelt/sounds/450752/ | - |
| arena_gulls | Seagull on beach | squashy555 | https://freesound.org/people/squashy555/sounds/353416/ | - |
| arena_desert_wind | desertwind1FINAL.wav | slugzilla | https://freesound.org/people/slugzilla/sounds/112296/ | - |
| arena_canyon_wind | Windy Canyon.wav | ciccarelli | https://freesound.org/people/ciccarelli/sounds/135447/ | - |
| arena_woodland | Quiet Spring Woodland Ambience.wav | ecfike | https://freesound.org/people/ecfike/sounds/160893/ | - |
| arena_dawn_chorus | Dawn Chorus Birdsong | squashy555 | https://freesound.org/people/squashy555/sounds/573080/ | - |
| arena_underwater | Underwater Ambience | Fission9 | https://freesound.org/people/Fission9/sounds/504641/ | - |
| arena_deep_sea | Deep Sea Ambience | jhumbucker | https://freesound.org/people/jhumbucker/sounds/193822/ | - |
| arena_ship_hum | ambient spacecraft hum | AlaskaRobotics | https://freesound.org/people/AlaskaRobotics/sounds/221570/ | - |
| arena_station_drone | Space Station Drone | db3005 | https://freesound.org/people/db3005/sounds/686237/ | - |

## Kenney (www.kenney.nl), CC0 1.0

| Pack | Page | Download |
|---|---|---|
| rpg-audio | https://kenney.nl/assets/rpg-audio | https://kenney.nl/media/pages/assets/rpg-audio/8e99002d76-1677590336/kenney_rpg-audio.zip |
| impact-sounds | https://kenney.nl/assets/impact-sounds | https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip |
| interface-sounds | https://kenney.nl/assets/interface-sounds | https://kenney.nl/media/pages/assets/interface-sounds/fa43c1dd4d-1677589452/kenney_interface-sounds.zip |
| ui-audio | https://kenney.nl/assets/ui-audio | https://kenney.nl/media/pages/assets/ui-audio/490d233f68-1677590494/kenney_ui-audio.zip |

Shipped sounds built (fully or partly) from Kenney packs: AMB_Chop_01, AMB_Door_01, AMB_Door_02, AMB_Door_03, AMB_Door_04, FS_ArmorRattle_05, FS_ArmorRattle_06, FS_ArmorRattle_07, FS_ArmorRattle_08, FS_Cloth_01, FS_Cloth_02, FS_Cloth_03, FS_Cloth_04, FS_Golem_01, FS_Golem_02, FS_Golem_03, FS_Golem_04, FS_Golem_05, FS_Plate_Dirt_03, FS_Plate_Dirt_04, FS_Plate_Dirt_05, FS_Soft_Dirt_01, FS_Soft_Dirt_02, FS_Soft_Dirt_03, FS_Soft_Dirt_04, FS_Soft_Dirt_05, FS_Soft_Stone_01, FS_Soft_Stone_02, FS_Soft_Stone_03, FS_Soft_Stone_04, FS_Soft_Stone_05, FS_Thump_01, FS_Thump_02, FS_Thump_03, FS_Thump_04, FS_Thump_05, SFX_CoinsBuy, SFX_CoinsSell, SFX_LootPickup, UI_Click_01, UI_Click_02, UI_Click_03, UI_Close, UI_Confirm, UI_Hover_01, UI_Hover_02, UI_Hover_03, UI_Hover_04, UI_Open.
