# BODYCAM FEATURE AUDIT

**Project:** bodycam-mini (`C:\Users\Flapjack\Desktop\bodycam-mini`)
**Current architecture:** simulare deterministă cu pas fix de 60 Hz (`World::step_humans`), separată de prezentare (`GameView`) și conectată prin `EventLog`. Bibliotecă statică `bodycam_core` (sim + render + scenariu + runner) și executabile `bodycam_app` (SDL2), `bodycam_exp` (experimente Monte Carlo) și `bodycam_tests` (18 teste, 119 verificări, toate trec).
**Engine:** motor propriu C++17, fără Unreal. Lumea e **2.5D** (grilă de 1 m, pereți de 2,7 m, raycasting), nu 3D poligonal.
**Language:** C++17 (MSVC /W4, OpenMP). Python pentru analize.
**Rendering:** raycaster software HDR pe CPU, la 640×360 intern, scalat de SDL. Personajele și obiectele sunt sprite-uri procedurale 2D. Arma din prim-plan, mâinile, tableta și picioarele sunt cutii 3D proiectate. Post-procesarea e software.
**Networking:** **nefuncțional.** `src/net/socket.*` și `src/net/protocol.*` sunt scrise, dar nu sunt în build. `make_network_session()` întoarce `nullptr`, deci `--host` / `--connect` nu pornesc.
**Animation system:** procedural, fără animații schelet, fără asset-uri și fără motion capture. Pozițiile armei se amestecă prin greutăți, iar brațele folosesc IK pe 2 segmente pe cutii.
**Audio system:** mixer software SDL2. Toate sunetele sunt sintetizate procedural; WAV-urile din `assets/audio/` sunt opționale. Ocluzie prin pereți, reverb Schroeder, auz degradat, respirație sintetizată.
**UI system:** fără HUD clasic. Timestamp-ul e „ars” în imagine, tableta are o textură randată, iar indiciul de interacțiune e un text discret. F1 deschide un overlay de debug.

> **Notă de metodă.** Am verificat codul direct (grep / citire) și am folosit rezultatele testelor automate și ale capturilor de ecran din sesiune. **Nu pot asculta sunetul**, deci calitatea perceptuală audio e marcată ⚠️ UNVERIFIED acolo unde contează. Un element e ✅ doar dacă e funcțional și conectat la gameplay.

---

## REZUMAT

### Pe sistem (39 de secțiuni, de la 2 la 40)
- **IMPLEMENTED:** 12
- **PARTIALLY IMPLEMENTED:** 24
- **NOT IMPLEMENTED:** 3

### Pe element din checklist
664 de elemente verificate:
- **✅ IMPLEMENTED:** 415
- **⚠️ PARTIALLY IMPLEMENTED:** 122
- **❌ NOT IMPLEMENTED:** 127
- **⚠️ UNVERIFIED:** 2 (numărate și ca parțiale)

> Procentul pe elemente (~62% ✅) e mai mare decât acoperirea ponderată (~52%). Checklist-urile de fiziologie au multe elemente mărunte, toate îndeplinite, în timp ce sistemele mari (multiplayer, animație, corpuri 3D) au puține rânduri, dar pondere mare.

---

## 2. Body-Camera System
**Status:** ⚠️ PARTIALLY IMPLEMENTED (nucleul e complet; lipsesc obstrucția camerei, mantle-ul și o profunzime de câmp generală)

**Current implementation:** Camera e un senzor montat pe piept, cu 0,14 m sub ochi și 0,12 m în față. Componentele independente pe arcuri sunt yaw și pitch cu anticipare de viteză (`step_ff`), roll, înălțime și decalaj față/lateral din accelerația corpului. Se adaugă oscilația pașilor, sincronizată cu `steps`/`bob_phase`, inclusiv șchiopătatul cu un picior rănit. Respirația vine din semnalul plămânilor (`phys.breath_signal`), iar la fiecare bătaie de inimă, peste un anumit puls, apare un impuls. Instabilitatea vine din postură, stres și tremur. Impulsurile la recul, lovitură (pe zone), aterizare, explozie, glonț trecut aproape și lovitura în ușă sunt separate. Aplecarea mută fizic poziția (head offset). Camera are și stări de privire în jos la reîncărcare și la tabletă, privire involuntară la panică, cădere când ești incapacitat și la moarte. Post-procesarea aplică distorsiunea, aberația cromatică, vigneta, zgomotul, expunerea, motion blur-ul și rolling shutter-ul.

**Code location:** `src/render/body_camera.cpp` (`BodyCamera::update`, `on_shot`, `on_hit`, `on_land`, `on_explosion`, `on_near_miss`, `on_kick`), `src/render/postfx.cpp` (`PostFx::process`), `src/app/game_view.cpp` (`current_pose`).

**BODYCAM comparison:** Filozofia e aceeași: camera e montată pe corp și are masă. Limita principală e motorul 2.5D. Pitch-ul e simulat prin forfecare verticală și e limitat la ±1,1 rad, iar roll-ul e aplicat pe imagine în post-procesare.

| Element | Status | Notă |
|---|---|---|
| Perspectivă bodycam reală | ✅ | montată pe piept, nu la nivelul ochilor |
| Cameră atașată fizic de corp | ✅ | poziția derivă din corp + `agent_head_offset` |
| Inerție a camerei | ✅ | arcuri pe toate axele |
| Inerție de rotație | ✅ | `yaw_`, `pitch_`, `roll_` (`Spring1::step_ff`) |
| Inerție de poziție | ✅ | `fwd_`, `lat_`, `z_` |
| Mișcare la accelerare | ✅ | `a_fwd`, `a_lat` → decalaj |
| Mișcare la decelerare | ✅ | același mecanism (semn opus) |
| Mișcarea corpului influențează camera | ✅ | |
| Mers | ✅ | oscilație verticală, laterală, roll |
| Sprint | ✅ | amplitudine mai mare |
| Ghemuit | ✅ | înălțimea ochilor + amplitudine redusă |
| Culcat | ✅ | camera la 0,32 m, legănare laterală la târâre |
| Impact la aterizare | ✅ | `on_land` |
| Săritură / mantle | ⚠️ | săritură ✅, mantle ❌ (nu există) |
| În funcție de viteză | ✅ | |
| În funcție de accelerație | ✅ | |
| În funcție de postură | ✅ | |
| În funcție de stamina | ✅ | indirect: adâncimea respirației crește cu oboseala |
| În funcție de stres | ✅ | instabilitate + privire involuntară (`tunnel`) |
| În funcție de respirație | ✅ | `breath_signal` → pitch și z |
| În funcție de puls | ✅ | impuls la fiecare bătaie (`last_beats_`) |
| În funcție de daune | ✅ | `on_hit(zone)`: capul smucit, umărul răsucit, piciorul cedează |
| În funcție de manevrarea armei | ✅ | privire în jos la reîncărcare, cu roll din plan |
| În funcție de recul | ✅ | `on_shot`, scalat cu reculul perceput |
| În funcție de rotația corpului | ✅ | roll la viraj |
| Comportament fisheye / lentilă | ✅ | barrel k1/k2 diferit pe profil |
| Distorsiune de lentilă | ✅ | |
| Aberație cromatică | ✅ | |
| Vignetă | ✅ | |
| Film grain | ✅ | zgomot temporal pe luminanță |
| Zgomot de senzor | ✅ | crește cu amplificarea (EV) |
| Adaptarea expunerii | ✅ | ponderată central, cu protecție pentru zonele luminoase |
| Motion blur | ✅ | direcțional, limitat |
| Profunzime de câmp realistă | ⚠️ | doar la tabletă (`focus_dist = 0.32`) |
| Prevenirea intrării camerei în pereți | ⚠️ | revine pe poziția corpului dacă se ciocnește; fără ajustare lină |
| Comportament la obstrucție | ❌ | nu există (de ex. camera acoperită de perete sau braț) |
| Tranziții între stări | ⚠️ | arcuri pentru ghemuit, culcat, căzut, moarte, tabletă; trecerea la dronă sau RC e o tăietură |

**Missing functionality:** mantle; obstrucția camerei; profunzime de câmp generală (focalizare automată pe distanța de la centru); tranziție lină la dronă/RC (efect de „comutare de feed”); evitarea pereților prin împingerea camerei, nu prin resetare.

**Recommended implementation:** În `BodyCamera::update`, un test de distanță până la perete (raycast scurt din `pose`) care împinge camera înapoi lin printr-un arc. Un `obstruction` (0..1) trimis la post-procesare pentru întunecare și blur. Mantle: o stare nouă în `player_sim` (detectarea unui obstacol jos în față + un arc de înălțime). Focalizare automată în `PostFx`: `focus_dist` = adâncimea filtrată a pixelilor din centru.

---

## 3. True Body / Character System
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** **Jucătorul local** are brațe (antebraț + braț) cu IK pe 2 segmente (`solve_elbow`) spre mâini. Mâinile sunt atașate de punctele armei: mânerul, garda, încărcătorul, închizătorul, buzunarul de încărcătoare sau tableta. Picioarele și centura cu port-încărcătoare (numărul real de încărcătoare) apar când privești în jos și se mișcă după faza pașilor. Torsul nu se vede, pentru că camera e pe vestă. **Ceilalți agenți** sunt siluete procedurale 2D (picioare care pășesc, trunchi, brațe, cap, armă proiectată lateral), cu înclinare la împleticire, culcat, căzut sau aplecat, plus reacții pe zone prin `AgentFx` (lean, dip, head).

**Code location:** `src/render/viewmodel.cpp` (`draw_viewmodel`, `solve_elbow`, `body_to_cam`, `limb`), `src/render/raycaster.cpp` (`human_albedo`, `HumanPose`), `src/app/game_view.cpp` (`agent_fx_`).

**BODYCAM comparison:** BODYCAM are un corp complet 3D cu schelet și animații motion capture. Aici nu există schelet și nici mesh 3D al corpului, iar ceilalți sunt sprite-uri billboard. E o diferență fundamentală de motor.

| Element | Status | Notă |
|---|---|---|
| Personaj complet | ⚠️ | local: brațe + picioare + centură; ceilalți: sprite 2D |
| Tors vizibil | ⚠️ | ceilalți da (sprite); propriul tors nu (camera e pe vestă) |
| Brațe vizibile | ✅ | antebraț + braț, cu IK |
| Mâini vizibile | ✅ | cutii (mănuși) |
| Picioare vizibile | ✅ | când privești în jos (în picioare sau ghemuit); nu și culcat |
| Arma atașată fizic de mâini | ✅ | mâinile urmăresc punctele armei |
| Poziționarea corectă a mâinilor | ⚠️ | puncte fixe pe armă; aproximativ |
| IK | ✅ | 2 segmente, cu vector „pole” |
| IK de mână | ✅ | ținte procedurale |
| IK de braț | ✅ | `solve_elbow` |
| IK de armă | ⚠️ | arma e poziționată procedural, iar mâinile o urmează (nu invers) |
| IK de corp | ❌ | nu există |
| Poziționare procedurală a mâinilor | ✅ | reîncărcare, tabletă, verificarea încărcătorului |
| Animația corpului influențează camera | ✅ | pași → cameră |
| Camera influențează orientarea corpului | ⚠️ | corpul urmează privirea direct; nu există întârziere a torsului |
| Arma influențează mișcarea corpului | ✅ | `movement_penalty`, greutatea → viteză și accelerație |
| Sincronizare corp/armă | ✅ | aceeași sursă: `aim_yaw` / `aim_pitch` |
| Reacții la daune | ✅ | stance + `AgentFx` + cameră |
| Reacții la recul | ⚠️ | local: mâini și cameră; sprite-urile celorlalți nu reacționează |
| Reacții la mișcare | ✅ | legănarea picioarelor, arma |
| Reacții la sprint | ✅ | arma „low ready”, ruliu |

**Missing functionality:** corp 3D cu schelet pentru ceilalți jucători; torsul propriu; picioarele când stai culcat; IK de corp și coloană; reculul vizibil la alți agenți; întârzierea torsului față de cap.

**Recommended implementation:** În limitele 2.5D: siluetele să primească brațe articulate (cutii proiectate ca la viewmodel) și un mini-schelet procedural (șold, genunchi, umăr) desenat ca segmente 3D proiectate în loc de sprite plat. Pe termen lung, un renderer poligonal (rasterizare de triunghiuri cu z-buffer peste raycaster) pentru corpuri.

---

## 4. Weapon Handling
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Orientarea armei (`w_yaw`, `w_pitch`) e un arc în **simulare**, cu rigiditatea în funcție de greutate, stamina, brațul rănit, ochire și poziția culcat, plus anticipare 0,85 și limită de ±0,35 rad. **Gloanțele pleacă pe direcția armei**: `aim_yaw` = armă + legănare + tremur + recul. Legănarea procedurală vine din respirația reală, pași, oboseală, stres, rana la braț, efort și aplecare, cu zgomot neted în loc de sinusoidă pură. Pozițiile se amestecă: șold, ochire, sprint, reîncărcare (înclinare din plan), verificarea încărcătorului, tabletă, aplecare, braț rănit și schimbarea armei. Viewmodel-ul are inerție de poziție față de rotația camerei (`lag_x`, `lag_y`) și smucituri (`jolt`).

**Code location:** `src/sim/player_sim.cpp` (`update_weapon_dynamics`, `aim_yaw`, `aim_pitch`), `src/render/viewmodel.cpp` (`ViewmodelState::update`, `draw_viewmodel`), `src/sim/world.cpp` (`human_weapon`).

**BODYCAM comparison:** Conceptul (poziționare procedurală, inerție, legănare contextuală) e aproape de BODYCAM la nivel de **logică**. Ca **fidelitate vizuală** e departe: arma e făcută din cutii, iar BODYCAM folosește animații contextuale motion capture.

| Element | Status | Notă |
|---|---|---|
| Inerția armei | ✅ | în simulare (afectează tragerea) |
| Inerție de rotație | ✅ | |
| Inerție de poziție | ⚠️ | doar din rotația camerei; nu din accelerația corpului |
| Legănare | ✅ | |
| Legănare procedurală | ✅ | |
| De la respirație | ✅ | |
| De la stres | ✅ | |
| De la oboseală | ✅ | |
| De la mișcare | ✅ | |
| De la postură | ✅ | ghemuit 0,75, culcat 0,45 |
| Instabilitate din răni | ✅ | `arm_injury` |
| Poziționare dinamică a armei | ✅ | |
| Poziționare dinamică a prinderii | ⚠️ | doar mâna stângă se mută la reîncărcare/tabletă |
| Manevrare specifică armei | ✅ | date INI |
| Greutatea influențează manevrarea | ✅ | |
| Lungimea influențează manevrarea | ❌ | `WeaponData::length` există, dar nu e folosit nicăieri |
| Masa influențează manevrarea | ✅ | (= greutate) |
| Accelerația influențează manevrarea | ⚠️ | prin inerția de rotație; nu din accelerația liniară |
| Stabilizare după mișcare | ✅ | arc subamortizat |
| Revenire | ✅ | `recoil_recovery`, depinde de stamina și braț |
| Inspectarea armei | ❌ | există doar verificarea încărcătorului |
| Inspectarea încărcătorului | ✅ | T: încărcătorul tras, cartușele vizibile |
| Verificarea încărcătorului | ✅ | + estimare pe tabletă |
| Re-prindere (regrip) | ❌ | |
| Animații de tranziție | ✅ | amestec lin (sprint, reîncărcare, tabletă) |
| Animații de schimbare a armei | ⚠️ | arma coboară și urcă; fără mișcare spre toc |
| Scoaterea armei | ⚠️ | timp din date ✅; vizual doar ridicare |
| Punerea în toc | ⚠️ | idem |
| Manevrarea echipamentului | ⚠️ | tableta ✅; grenada și bandajul fără animație |

**Missing functionality:** lungimea armei (coliziunea țevii cu pereții → arma ridicată sau retrasă lângă perete); inspectarea completă; regrip; animații reale de toc/scoatere; inerție de poziție din accelerație.

**Recommended implementation:** În `update_weapon_dynamics`: raycast pe lungimea armei → `wall_block` (0..1) care împinge arma înapoi sau o ridică (și blochează tragerea dacă e complet obstrucționată). Un `regrip` declanșat după sprint, salt sau lovitură (ofset tranzitoriu al mâinii stângi). Traiectorii pentru toc și scoatere ca Bezier spre șold (`body_to_cam`).

---

## 5. Recoil System
**Status:** ✅ IMPLEMENTED (cu lipsuri minore)

**Current implementation:** Reculul are mai multe componente:
1. **recul mecanic în simulare** (`kick_pitch`/`kick_yaw`, cu recuperare) care schimbă direcția gloanțelor;
2. **recul al camerei** (`BodyCamera::on_shot`, scalat cu `perceived_recoil`);
3. **mâinile și arma** (arcurile `back` și `jolt`, plus ridicarea țevii);
4. **brațele**, care urmează mâinile prin IK;
5. **răspunsul la stres** (`physiology_on_shot`);
6. **reculul permanent** adăugat privirii (`take_view_punch_*`).

Factorii de recul sunt postura, mișcarea, stamina, greutatea, prinderea (foregrip, braț rănit sau o singură mână), căldura și stresul. Componenta orizontală combină un model fix (`recoil_pattern`) cu o parte aleatoare seed-uită.

**Code location:** `src/sim/world.cpp` (`fire_human`, blocul „Recul multi-factor”), `src/render/viewmodel.cpp` (`on_shot`), `src/render/body_camera.cpp` (`on_shot`), `src/app/game_view.cpp` (`local_fire_effects`).

**BODYCAM comparison:** Structural e apropiat. Diferența e fidelitatea animației (cutii, fără animație procedurală de umăr și coloană).

| Element | Status | Notă |
|---|---|---|
| Lovitura armei | ✅ | |
| Recul al camerei | ✅ | |
| Recul al mâinilor | ✅ | |
| Recul al brațelor | ✅ | prin IK |
| Recul procedural | ✅ | |
| Recuperarea reculului | ✅ | |
| Vertical | ✅ | |
| Orizontal | ✅ | |
| Aleator în limite controlate | ✅ | `u01` seed-uit + model |
| Specific armei | ✅ | |
| În funcție de calibru | ⚠️ | valori pe armă în INI; nu sunt derivate din energie sau calibru |
| În funcție de greutatea armei | ✅ | `weight_k` |
| În funcție de postură | ✅ | |
| În funcție de mișcare | ✅ | |
| În funcție de stres | ✅ | |
| În funcție de oboseală | ✅ | |
| Afectează mâinile, nu doar camera | ✅ | |
| Afectează animația | ⚠️ | doar arcuri de translație și rotație |
| Afectează poziționarea armei | ✅ | |

**Missing functionality:** reculul derivat din impulsul glonțului (masă × viteză); animație de umăr sau coloană.

**Recommended implementation:** `recoil_pitch_base = k * bullet_mass * muzzle_velocity / weight` în `WeaponData::derive`, cu INI-ul ca multiplicator. Un arc de umăr în viewmodel care deplasează P spre spate și în sus.

---

## 6. Reload System
**Status:** ✅ IMPLEMENTED (logica e completă; fidelitatea vizuală e scăzută)

**Current implementation:** Fiecare reîncărcare e un **plan** (`ReloadPlan`) generat din starea armei și a corpului, în 7 stiluri: Normal, Fast, Tactical, Emergency, Panic, Injured, Stressed, plus Shell (pușcă) și Clear (blocaj).

Stilul se alege după stres, brațul rănit, o amenințare recentă și dacă încărcătorul e gol. Fiecare etapă (Lower, MagOut, Fetch, MagIn, Bolt, Raise, Shell, Clear) are o durată cu variație marginită (8–18%), multiplicată de motricitatea fină, încărcătură și mișcare.

Planul mai include parametri vizuali marginiți (înclinare, unghiul încărcătorului, curbura și dezordinea mâinii, mișcarea camerei), o pauză de ezitare și o ratare la introducere (`fumble`, la panică).

**Starea logică** se schimbă la sfârșitul etapelor:
- încărcătorul scos: e păstrat cu cartușele lui, sau aruncat pe jos ca obiect de ridicat;
- încărcătorul introdus: se ia cel mai plin;
- închizătorul: cartușul intră pe țeavă.

Dacă e întreruptă (sprint, schimbarea armei, tabletă, interacțiune, lovitură puternică, apăsarea trăgaciului când există un cartuș pe țeavă), reîncărcarea păstrează starea. La reluare sare peste etapele deja făcute (testat: `test_reload_interrupt_resume`).

Viewmodel-ul animează procedural fiecare etapă: mâna merge la încărcător, apoi încărcătorul cade sau e păstrat, mâna face o curbă Bezier spre port-încărcătoare, încărcătorul vine de jos la unghiul din plan, apoi închizătorul și smucitura.

**Code location:** `src/sim/reload.cpp` (`make_reload_plan`), `src/sim/world.cpp` (`start_reload`, `advance_reload`, `begin_stage`, `interrupt_reload`), `src/render/viewmodel.cpp` (pozițiile mâinii stângi, `mag_drop`), `src/app/game_view.cpp` (sunetul pe etapa încheiată).

**BODYCAM comparison:** Combinația cerută (animație de bază + specific armei + strat de stres + plasare procedurală a mâinii + rotație + variație de timp + postură + stare) e implementată **procedural**. Rezultatul vizual e însă cu cutii, nu cu animații reale.

| Element | Status | Notă |
|---|---|---|
| Reîncărcare tactică | ✅ | încărcătorul parțial e păstrat (testat) |
| Reîncărcare pe gol | ✅ | etapa Bolt (testat) |
| De urgență | ✅ | încărcătorul cade |
| Sub stres | ✅ | Stressed / Panic |
| Cu rană | ✅ | Injured (×1,6, altă poziție) |
| Epuizat | ✅ | prin `fine_motor` și stamina (fără stil separat) |
| În mișcare | ✅ | ×1,06 |
| Ghemuit | ⚠️ | permisă; fără poziție specifică ghemuit |
| În picioare | ✅ | |
| Specifică armei | ⚠️ | pușca cartuș cu cartuș ✅; pistolul și pușca automată au aceleași etape, cu geometrie diferită |
| Comportament specific încărcătorului | ✅ | cartușe și uzură per încărcător |
| Animația de scoatere | ✅ | procedurală |
| Animația de introducere | ✅ | procedurală |
| Păstrarea încărcătorului | ✅ | |
| Aruncarea încărcătorului | ✅ | devine obiect de ridicat |
| Verificarea încărcătorului | ✅ | |
| Starea camerei (cartuș pe țeavă) | ✅ | |
| Manevrarea închizătorului / culisei | ✅ | culisa pistolului rămâne deschisă pe gol |
| Întreruperea | ✅ | |
| Continuarea după întrerupere | ✅ | testat |
| Anularea | ✅ | |
| Poziționare procedurală | ✅ | |
| Alinierea procedurală a mâinii | ⚠️ | ținte fixe + zgomot |
| Rotația procedurală a armei | ✅ | |
| Translația procedurală a armei | ✅ | |
| Timp variabil | ✅ | |
| Timp dependent de stres | ✅ | |
| Timp dependent de oboseală | ✅ | |
| Timp dependent de rană | ✅ | |
| Variante diferite | ✅ | 7 stiluri + variație |
| Aleator controlat | ✅ | seed pe agent și contor |
| Rezultate diferite | ⚠️ | păstrat/aruncat, ratarea introducerii; nu există încărcător introdus greșit care să ducă la blocaj |
| Amestecarea animațiilor | ✅ | `reload_w` lin |
| Tratarea eșecului | ⚠️ | ratarea la introducere ✅; fără feedback vizual de „fără încărcătoare” |
| Regrip în timpul reîncărcării | ❌ | |

**Missing functionality:** regrip; poziție specifică ghemuit sau culcat (arma sprijinită pe sol sau genunchi); eșecuri cu consecințe (încărcător nefixat care cade la primul foc); fidelitatea animației.

**Recommended implementation:** În `ReloadPlan`: `seat_fail` (probabilitate din stres), iar `fire_human` verifică `mag_seated` și poate pierde încărcătorul. Ținte de mână dependente de postură în `draw_viewmodel` (prone: arma pe o parte, încărcătorul de pe sol).

---

## 7. Stress System
**Status:** ✅ IMPLEMENTED

**Current implementation:** Stresul e continuu, cu o componentă acută și oboseala de luptă (cronică), saturare pe stimuli, revenire dependentă de puls și niveluri CALM…EXTREME cu histerezis. Stimulii sunt aplicați la sfârșitul pasului (independent de ordine). Pragurile și magnitudinile sunt în `assets/config/physiology.ini`.

**Code location:** `src/sim/physiology.cpp` (`update_physiology`, `physiology_stimulus`, `physiology_on_shot`), `src/sim/world.cpp` (`stimulus`, `stimulus_area`, `notify_team_loss`, `update_body_state`, `perceive`), `src/sim/interaction.cpp` (kick → `s_sudden`).

**BODYCAM comparison:** BODYCAM nu documentează public un model de stres la fel de explicit. Sistemul de aici e mai „simulativ” și cerința e acoperită.

| Element | Status | Notă |
|---|---|---|
| Valoare de stres | ✅ | 0..1 |
| Acumulare | ✅ | |
| Revenire | ✅ | |
| Praguri | ✅ | cu histerezis |
| Stres de luptă | ✅ | `r_combat` + `time_since_threat` |
| Foc primit | ✅ | NEAR_MISS / impact aproape |
| Glonț trecut aproape | ✅ | |
| Rană | ✅ | |
| Explozie | ✅ | |
| Moartea unui coechipier | ✅ | `notify_team_loss` (LOS și distanță) |
| Proximitatea inamicului | ⚠️ | doar la detecție (×1,5 sub 8 m); nu continuu |
| Foc susținut | ✅ | `fire_heat` |
| Sănătate scăzută | ✅ | |
| Oboseală | ✅ | |
| Mediu | ⚠️ | întuneric + sunete puternice; nimic altceva |
| Întuneric | ✅ | redus de lanternă |
| Recuperare | ✅ | |
| Persistență | ✅ | componenta cronică |
| Afectează animațiile | ⚠️ | planul de reîncărcare și camera; nu alte animații |
| Afectează manevrarea armei | ✅ | |
| Afectează reîncărcarea | ✅ | |
| Afectează respirația | ✅ | |
| Afectează pulsul | ✅ | |
| Afectează camera | ✅ | |
| Afectează auzul | ✅ | tunelul adaugă atenuare |
| Afectează precizia | ✅ | jucător: tremur/legănare; boți: `psi` |
| Afectează mișcarea | ⚠️ | doar refacerea staminei; viteza nu |
| Afectează interacțiunile | ✅ | `fine_motor` → timpii de interacțiune |

**Missing functionality:** stres continuu la proximitatea inamicului; mai multe surse de mediu (alarme, flăcări, cadavre); efect direct asupra mișcării (pași nesiguri, împiedicare).

**Recommended implementation:** În `update_body_state`, un termen continuu `Σ(1 - d/10)` peste inamicii vizibili. Sub stres extrem, în `simulate_player_movement`, un zgomot de direcție mic și o probabilitate de împiedicare la sprint.

---

## 8. Breathing System
**Status:** ✅ IMPLEMENTED (audio ⚠️ UNVERIFIED perceptual)

**Current implementation:** Respirația face parte din `Physiology` (echivalentul UBreathingComponent): ritm 13–44/min care urmează efortul, stresul, stamina, pulsul, focul susținut și rănile, cu interpolare asimetrică. Adâncimea și controlul scad sub stres. Faza e modulată de zgomot neted (nu e o sinusoidă perfectă; testul verifică neperiodicitatea), iar inspirul e mai scurt decât expirul. `breath_signal` merge la cameră și la armă, `breath_flow` la audio.

Audio e un **sintetizator continuu** cu 6 straturi (nazal, gură, raspy, vocal la epuizare, tremolo de stres) cu greutăți interpolate. Nu sunt eșantioane care se repetă în buclă.

**Code location:** `src/sim/physiology.cpp`, `src/audio/audio_engine.cpp` (`breath_sample`, `set_breath`), `src/app/game_view.cpp` (`update_audio_state`).

**BODYCAM comparison:** Peste cerința „să nu fie o buclă de sunet”. Calitatea sunetului sintetizat nu e verificată.

| Element | Status | Notă |
|---|---|---|
| Componentă de respirație | ✅ | |
| În repaus | ✅ | |
| În mișcare | ✅ | |
| La sprint | ✅ | |
| În luptă | ✅ | prin stres și adrenalină |
| Sub stres | ✅ | |
| Cu rană | ✅ | |
| La oboseală | ✅ | |
| Respirație grea | ✅ | |
| Recuperare | ✅ | |
| Variație | ✅ | |
| Procedurală | ✅ | |
| Afectează camera | ✅ | |
| Afectează arma | ✅ | |
| Afectează stresul | ❌ | nu există efect invers (respirația controlată nu reduce stresul) |
| Afectează stamina | ⚠️ | indirect, prin puls (`hr_n` în refacere) |
| Audio de respirație | ⚠️ UNVERIFIED | conectat ✅; calitatea neverificată |
| Mai multe straturi | ✅ | 6 |
| Crossfade audio | ✅ | greutăți continue |
| Intensitate dinamică | ✅ | |

**Missing functionality:** efectul respirației asupra stresului (o mecanică de „respirație controlată” sau „ține-ți respirația”); verificarea perceptuală a sintezei.

**Recommended implementation:** O tastă de ținut respirația la ochire: reduce legănarea 2–3 s, crește CO₂ (o variabilă nouă) și consumă stamina; expirul controlat reduce stresul acut.

---

## 9. Heart Rate
**Status:** ✅ IMPLEMENTED

**Current implementation:** Pulsul (68–182 BPM, configurabil) urmărește o țintă derivată din efort, adrenalină, stres, răni și focul susținut. Urcă rapid (0,55/s) și coboară lent (0,07/s). Bătăile sunt numărate (`beats`) și declanșează sunet și un impuls al camerei.

**Code location:** `src/sim/physiology.cpp` (blocul „Puls”), `src/app/game_view.cpp` (inima în `update_audio_state`), `src/render/body_camera.cpp` (`last_beats_`).

| Element | Status | Notă |
|---|---|---|
| Simularea pulsului | ✅ | |
| Puls de repaus | ✅ | |
| Puls de luptă | ✅ | |
| Din stres | ✅ | |
| Din daune | ✅ | |
| Din sprint | ✅ | |
| Din oboseală | ✅ | prin efort |
| Recuperare | ✅ | |
| Afectează respirația | ✅ | |
| Afectează audio | ✅ | 3 straturi de bătăi |
| Afectează camera | ✅ | |
| Afectează tremurul | ⚠️ | indirect (adrenalină și stres); pulsul nu e o intrare directă |

**Missing functionality:** pulsul ca intrare directă în tremur.

**Recommended implementation:** În `update_physiology`, `amp_t += cfg.tremor_hr * smoothstep(110, 170, HR)`.

---

## 10. Hand Tremor
**Status:** ✅ IMPLEMENTED

**Current implementation:** Tremurul are trei componente de zgomot neted (1,1–1,3 Hz, ~5 Hz, ~11 Hz), cu frecvența modulată de adrenalină. Amplitudinea vine din bază, stres², oboseală, focul susținut, rana la braț și adrenalină. Tremurul e **inclus în direcția glonțului** (`aim_yaw`/`aim_pitch`) și apare în viewmodel ca micro-translație a armei, pe care mâinile o urmează prin IK.

**Code location:** `src/sim/physiology.cpp` (`tremor_*`), `src/sim/player_sim.cpp` (`aim_yaw`), `src/render/viewmodel.cpp` (`tr = a.phys.tremor_amp * 2.0`).

| Element | Status | Notă |
|---|---|---|
| Tremur procedural | ✅ | |
| Din stres | ✅ | |
| Din oboseală | ✅ | |
| Din daune | ✅ | |
| Din foc susținut | ✅ | |
| Din stamina scăzută | ✅ | |
| Din puls ridicat | ⚠️ | prin adrenalină (proxy) |
| Frecvență joasă | ✅ | |
| Micro-tremur de frecvență înaltă | ✅ | |
| Amplitudine variabilă | ✅ | |
| Frecvență variabilă | ✅ | |
| Afectează arma | ✅ | |
| Afectează IK-ul mâinilor | ✅ | |
| Afectează ținta | ✅ | |

**Missing functionality:** o mică parte din tremur intră și în instabilitatea camerei (`instab += tremor_amp*40` în `body_camera.cpp`). Cerința spune că ecranul nu trebuie să tremure, așa că acest termen ar trebui redus sau eliminat.

**Recommended implementation:** Scoate `ph.tremor_amp * 40.0` din `instab` și păstrează doar postura și stresul.

---

## 11. Hearing / Audio Stress
**Status:** ⚠️ PARTIALLY IMPLEMENTED (degradarea auzului ✅; vertical și whiz ❌; calitatea UNVERIFIED)

**Current implementation:** `HearingModel` acumulează expunerea: focul propriu (mai puternic în interior), focuri apropiate, explozii (cu LOS), glonț trecut aproape și lovituri. Recuperarea e exponențială și mai lentă după o expunere mare. Mixer-ul aplică trece-jos 18 kHz → 550 Hz, volum mai mic, îngustarea stereo, distorsiune ușoară și un țiuit continuu. Sunetele propriului corp merg pe un bus intern, nefiltrat. „Crack”-ul vine din evenimentul NEAR_MISS și e suprimat pentru armele subsonice (MP5SD). Întârzierea de propagare e d/343.

**Code location:** `src/app/game_view.hpp` (`HearingModel`), `src/app/game_view.cpp` (`update_audio_state`, `expose`), `src/audio/audio_engine.cpp` (`set_hearing`, `mix`), `src/sim/world.cpp` (NEAR_MISS în `step_projectile`), `src/sim/bot_ai.cpp`.

**BODYCAM comparison:** Degradarea auzului e acoperită. Lipsesc audio vertical, „whiz”-ul subsonic, propagarea undei de șoc (crack-ul ajunge înaintea detunăturii) și VOIP-ul (vezi 32).

| Element | Status | Notă |
|---|---|---|
| Răspuns la focuri | ✅ | |
| Degradare la foc susținut | ✅ | |
| Degradare la explozie | ✅ | |
| Înfundare temporară | ✅ | |
| Filtru trece-jos | ✅ | |
| Atenuarea frecvențelor înalte | ✅ | |
| Atenuarea volumului | ✅ | |
| Țiuit | ✅ | |
| Recuperarea după țiuit | ✅ | |
| Schimbări stereo | ✅ | |
| Audio dependent de distanță | ✅ | |
| Audio direcțional | ✅ | panoramare stereo (fără HRTF) |
| Ocluzie | ✅ | numărul de pereți → atenuare + trece-jos |
| Reverb de mediu | ✅ | Schroeder |
| Audio dependent de încăpere | ⚠️ | reverb global, din poziția ascultătorului |
| Poziționare verticală | ❌ | motor 2.5D |
| Whiz de glonț | ❌ | |
| Crack de glonț | ✅ | |
| Feedback la foc primit | ✅ | crack + cameră + stres |
| Sunet de foc specific armei | ✅ | 4 profiluri |
| Sunet specific amortizorului | ✅ | |

**Missing functionality:** whiz subsonic; unda de șoc (crack înaintea detunăturii, în funcție de unghi); audio vertical; reverb pe încăperea sursei; verificare perceptuală.

**Recommended implementation:** Ai spus că audio nu e prioritar acum. Dacă revii la el: în `GameView` la NEAR_MISS, `Crack` dacă v > 343, altfel `Whiz`, iar detunătura e întârziată cu (d − d_crack)/343. Reverb per voce, cu `room_size` calculat la poziția sursei.

---

## 12. Spatial Audio
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** `SpatialAudioPipeline` calculează atenuarea 1/(1+(d/3)^1.3), numărul de pereți pe linia de vedere (ușile închise blochează, ferestrele nu), un trece-jos dependent de pereți și de distanță, umbra capului pentru sursele din spate, panoramare equal-power, întârziere și reverb send. `update_environment` estimează mărimea încăperii din 16 raze.

**Code location:** `src/sim/spatial_audio_engine.hpp`, `src/audio/audio_engine.cpp` (`spatialize`, `update_environment`, `mix`).

| Element | Status | Notă |
|---|---|---|
| Audio pozițional 3D | ⚠️ | 2D (plan orizontal) |
| Atenuare cu distanța | ✅ | |
| Ocluzie | ✅ | |
| Obstrucție | ⚠️ | aceeași cu ocluzia; fără difracție pe la colțuri |
| Reverb | ✅ | |
| Acustica încăperii | ⚠️ | global, după ascultător |
| Diferențe interior/exterior | ✅ | reverb + ambianță |
| Poziționare verticală | ❌ | |
| Sunet dependent de material | ⚠️ | impacturile pe material ✅; atenuarea prin perete nu depinde de material |
| Sunet dependent de uși | ✅ | ușă închisă = perete |
| Sunet dependent de pereți | ✅ | număr de pereți |
| Poziționarea pașilor | ✅ | |
| Atenuarea pașilor | ✅ | |
| Tipuri de suprafață la pași | ✅ | |
| Propagarea focurilor | ✅ | întârziere + ocluzie |
| Propagarea undei de șoc | ❌ | |
| Whiz | ❌ | |
| Crack | ✅ | |

**Missing functionality:** difracție (sunetul ocolește prin uși deschise); atenuare dependentă de material; elevație.

**Recommended implementation:** Prioritate scăzută, conform cererii tale. Difracție: BFS pe celule de la sursă la ascultător, folosit când linia de vedere e blocată; drumul prin ușă dă direcția și o atenuare mai mică.

---

## 13. Footsteps
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Pașii sunt declanșați pe distanță (`stride` în funcție de postură și răni), sincronizați cu oscilația camerei și cu animația picioarelor. Suprafața vine din celula hărții. Sunetul are variație de înălțime și volum, iar raza de auz pentru AI depinde de postură și încărcătură.

**Code location:** `src/sim/player_sim.cpp` (blocul „Pasi”), `src/sim/bot_ai.cpp` (`bot_footsteps`), `src/sim/surface.hpp` (`floor_surface`), `src/app/game_view.cpp` (`step_sfx`).

| Element | Status | Notă |
|---|---|---|
| Pași dependenți de suprafață | ✅ | |
| Beton | ✅ | |
| Metal | ⚠️ | sunetul există, dar nicio podea nu e de metal (`floor_surface` nu are caracter pentru metal) |
| Lemn | ✅ | |
| Pământ | ✅ | |
| Iarbă | ✅ | |
| Piatră | ⚠️ | piatra există doar ca perete; fără podea de piatră și fără sunet propriu |
| Sticlă (cioburi) | ❌ | |
| Pași în interior | ✅ | |
| Pași în exterior | ✅ | |
| Pași ghemuit | ✅ | mai încet |
| Pași la sprint | ✅ | |
| Pași la mers | ✅ | |
| Direcționalitatea pașilor | ✅ | |
| Ocluzia pașilor | ✅ | |
| Poziționarea verticală | ❌ | |
| Variație | ✅ | |
| Cooldown | ✅ | pe distanță |
| Sincronizarea cu animația | ✅ | |

**Missing functionality:** podele de metal și de piatră; cioburi de sticlă pe jos după spargere; un strat de echipament („zornăitul” mai puternic la încărcătură mare); poziționare verticală.

**Recommended implementation:** Caractere noi `m` (metal) și `s` (piatră) în `floor_surface`. După `WINDOW_BREAK`, celulele vecine primesc un strat de „cioburi” (bitmap în `Map`) care schimbă sunetul pașilor.

---

## 14. Weapon Audio
**Status:** ⚠️ PARTIALLY IMPLEMENTED (calitatea UNVERIFIED)

| Element | Status | Notă |
|---|---|---|
| Sunete de foc specifice armei | ✅ | rifle / smg / pistol / shotgun |
| Foc în interior | ⚠️ | același eșantion; diferă doar reverb-ul |
| Foc în exterior | ⚠️ | idem |
| Variație cu distanța | ✅ | strat „distant” peste 25 m |
| Ecou | ✅ | inclus în stratul „distant” + reverb |
| Reverb | ✅ | |
| Amortizor | ✅ | |
| Sunete mecanice | ✅ | mecanism + închizător |
| Sunete de reîncărcare | ✅ | pe etapa încheiată |
| Sunete de încărcător | ✅ | MagOut / MagIn / MagDrop |
| Închizător / culisă | ✅ | |
| Schimbarea armei | ✅ | |
| Inspectarea armei | ❌ | |
| Foley de manevrare | ⚠️ | haine la săritură și reîncărcare; fără foley continuu |
| Tuburi de cartuș | ✅ | la fiecare ricoșeu fizic |
| Impacturi | ✅ | pe material |

**Code location:** `src/audio/audio_engine.cpp` (`synthesize`, `gunshot`), `src/app/game_view.cpp` (`gun_sfx`, `local_fire_effects`, cazurile `RELOAD_STAGE`/`RELOAD_END`).

**Missing functionality:** variante pentru interior și exterior; foley de echipament legat de mișcare; sunete de inspectare. **Nu e prioritar, conform cererii tale.**

---

## 15. Weapon Customization
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Atașamentele sunt **data-driven** (`attachments = ...` în INI) și modifică reculul (ambele axe cu același multiplicator), raza sunetului, flacăra, timpul de ochire, legănarea, greutatea și capacitatea încărcătorului. Echipamentul e: arma principală (`--weapon` sau prima din listă) + pistol; grenade și bandaje în inventar. **Nu există UI de echipare în joc.**

**Code location:** `src/sim/weapon_mod.hpp` (`attachment_catalog`, hardcodat), `src/sim/weapon_data.cpp` (`apply_attachments`), `assets/weapons/*.ini`, `src/game/scenario.cpp` (`add_human_player`).

| Element | Status | Notă |
|---|---|---|
| Sistem de echipament | ⚠️ | doar din linia de comandă + INI |
| Slot principal | ✅ | |
| Slot secundar | ✅ | |
| Slot de corp la corp | ❌ | |
| Slot de exploziv | ⚠️ | grenadele sunt un contor, nu un slot |
| Slot de gadget | ⚠️ | drona și RC-ul sunt mereu disponibile |
| Atașamente | ✅ | |
| Atașamente de țeavă | ⚠️ | doar ca tip (amortizor, compensator) |
| Atașamente de gură | ✅ | |
| Amortizor | ✅ | statistici + vizual |
| Flash hider | ❌ | |
| Compensator | ⚠️ | statistici ✅, vizual ❌ |
| Țeavă scurtă | ❌ | |
| Țeavă lungă | ❌ | |
| Optici | ⚠️ | doar red dot |
| Optici cu mărire | ❌ | |
| Optici înclinate (canted) | ❌ | |
| Mânere | ✅ | foregrip: statistici + vizual + `grip_k` |
| Paturi | ⚠️ | statistici; fără vizual |
| Încărcătoare | ✅ | extmag (+15) |
| Statistici pe atașament | ✅ | |
| Viteza de ochire | ✅ | |
| Viteza de schimbare a armei | ❌ | |
| Viteza de reîncărcare | ❌ | |
| Recul orizontal | ⚠️ | un singur multiplicator pentru ambele axe |
| Recul vertical | ⚠️ | idem |
| Dispersie | ❌ | |
| Lovitura armei (camera kick) | ❌ | |
| Capacitatea încărcătorului | ✅ | |
| Schimbarea aspectului | ⚠️ | reddot, amortizor, lanternă, foregrip; restul nu |
| Schimbarea manevrării | ✅ | greutate, legănare |

**Recommended implementation:** Mută catalogul în `assets/attachments/*.ini`, cu câmpuri separate (`recoil_v`, `recoil_h`, `spread`, `switch_mod`, `reload_mod`, `kick`, `length_add`). Adaugă o pagină „ECHIPAMENT” pe tabletă (în afara rundei sau la spawn) care modifică `WeaponData` pe slot.

---

## 16. Optics
**Status:** ⚠️ PARTIALLY IMPLEMENTED (doar red dot)

**Current implementation:** Optica red dot e o cutie cu ramă și sticlă nuanțată. Punctul e proiectat pe **direcția reală a țevii** (inerție, tremur, recul incluse) și e vizibil la ochire.

**Code location:** `src/render/viewmodel.cpp` (`kind == 1` în `draw_box`, blocul „Punctul rosu”).

| Element | Status | Notă |
|---|---|---|
| Red dot | ✅ | |
| Holografic | ❌ | |
| Lunetă cu mărire | ❌ | |
| Picture-in-picture | ❌ | |
| Reticule | ⚠️ | doar punct |
| Optici înclinate | ❌ | |
| Comutarea opticii înclinate | ❌ | |
| Shader de sticlă | ⚠️ | nuanță simplă |
| Reflexii pe sticlă | ❌ | |
| Murdărie pe sticlă | ❌ | |
| Imperfecțiuni ale lentilei | ❌ | |
| Distorsiune pe margini | ❌ | |
| Distorsiunea reticulului | ❌ | |
| Profunzime de câmp în lunetă | ❌ | |
| Autofocus în lunetă | ❌ | |
| Alinierea corectă cu ochiul | ⚠️ | aliniată cu camera de piept |

**Recommended implementation:** Luneta PiP: un `render_scene` la rezoluție mică cu FOV îngust (infrastructura există deja pentru feed-ul tabletei), mapat pe lentilă ca textura tabletei, cu vignetă circulară și umbră de paralaxă. Murdăria și reflexiile se refolosesc din `smudge`/`scratch`.

---

## 17. Ballistics
**Status:** ✅ IMPLEMENTED

**Current implementation:** Proiectilele fizice au viteză, frânare în aer (`drag_k` din masă, Cd și secțiune) cu integrare implicită, gravitație și 2 subpași pe tick. Fiecare segment e trasat cu `trace_bullet`: penetrare pe material (J/m × grosime; ușă 6 cm, sticlă 1,2 cm), ricoșeu după unghiul de incidență, impact în podea și tavan, corpuri cilindrice cu zone (cap, trunchi, brațe și picioare stânga/dreapta) și pierdere de energie. Dauna e proporțională cu energia la impact, plus o atenuare explicită (alicele). Hitscan e folosit pentru modelul statistic validat al boților (E1).

**Code location:** `src/sim/ballistics.cpp` (`trace_bullet`), `src/sim/world.cpp` (`spawn_projectile`, `step_projectile`, `apply_impacts`), `src/sim/surface.hpp`.

| Element | Status | Notă |
|---|---|---|
| Simulare de proiectil | ✅ | testat (timp de zbor) |
| Hitscan unde e potrivit | ✅ | |
| Viteza la gură | ✅ | |
| Gravitație | ✅ | |
| Rezistența aerului | ✅ | |
| Bătaie | ✅ | |
| Scăderea daunei cu distanța | ✅ | |
| Penetrare pe material | ✅ | |
| Unghiul de impact | ✅ | |
| Ricoșeu | ⚠️ | glonțul se oprește; nu continuă pe direcția reflectată |
| Pierdere de energie | ✅ | |
| Impact dependent de suprafață | ✅ | |
| Detectarea loviturilor pe personaje | ✅ | |
| Direcția loviturii | ✅ | |

**Missing functionality:** traiectoria după ricoșeu; geometrie verticală (mobilier, scări) dincolo de pereți, podea și tavan, limitată de 2.5D.

---

## 18. Damage System
**Status:** ✅ IMPLEMENTED

**Current implementation:** Sănătatea e pe 6 zone, configurabile în `assets/config/health.ini`. HP-ul global scade, iar loviturile în membre contează ×0,6. Rănile produc sângerare, care se coagulează lent. Incapacitarea are timer de sângerare până la moarte și poate fi întreruptă de resuscitare (ținut apăsat) sau de moarte la o lovitură în plus. O lovitură letală în cap ucide direct. Brațul rănit înseamnă tremur, motricitate fină scăzută, recul mai mare, reîncărcare în stilul Injured și, cu brațul dezactivat, o singură mână (ochire și recul ×1,6–2). Piciorul rănit înseamnă viteză −45%, fără sprint, fără săritură și șchiopătat.

**Code location:** `src/sim/health.hpp`, `src/sim/world.cpp` (`resolve_hits`, `go_down`, `kill`, `update_body_state`), `src/sim/interaction.cpp` (Revive, `update_bandage`).

| Element | Status | Notă |
|---|---|---|
| Sănătate | ✅ | |
| Daună la cap | ✅ | |
| Daună la trunchi | ✅ | |
| Daună la braț | ✅ | |
| Daună la picior | ✅ | |
| Daună direcțională | ✅ | |
| Reacții la daune | ✅ | |
| Brațul rănit afectează ținta | ✅ | |
| Brațul rănit afectează reîncărcarea | ✅ | testat |
| Piciorul rănit afectează mișcarea | ✅ | testat |
| Piciorul rănit afectează stamina | ⚠️ | prin accelerație și viteză; consumul de stamina nu crește |
| Daune severe | ✅ | sângerare |
| Incapacitare | ✅ | testat |
| Moarte | ✅ | |
| Reacția fizică a corpului | ⚠️ | stance + înclinarea sprite-ului; fără ragdoll fizic |

---

## 19. Movement
**Status:** ✅ IMPLEMENTED (fără mantle)

**Code location:** `src/sim/player_sim.cpp` (`simulate_player_movement`).

| Element | Status | Notă |
|---|---|---|
| Mers | ✅ | |
| Sprint | ✅ | |
| Ghemuit | ✅ | |
| Culcat | ✅ | testat |
| Săritură | ✅ | |
| Mantle | ❌ | |
| Aplecare | ✅ | |
| Accelerație | ✅ | |
| Decelerație | ✅ | |
| Inerția mișcării | ✅ | testat |
| Stamina | ✅ | |
| Oboseală | ✅ | |
| Zgomotul mișcării | ✅ | raza de auz pentru AI |
| Mișcarea afectează arma | ✅ | |
| Mișcarea afectează camera | ✅ | |
| Mișcarea afectează respirația | ✅ | |
| Mișcarea afectează pulsul | ✅ | |

**Missing functionality:** mantle (obstacole joase); obstacolele joase nici nu există în harta 2.5D (necesită o înălțime pe celulă).

---

## 20. Leaning
**Status:** ✅ IMPLEMENTED

**Current implementation:** Aplecarea deplasează capul și corpul lateral cu până la 0,38 m (`agent_head_offset`), cu verificare de coliziune care reduce aplecarea lângă pereți (testat). Ea mută originea glonțului, hitbox-ul pentru ceilalți (`collect_targets`), punctul folosit în percepție, camera (poziție + roll 0,2) și arma (roll + legănare).

| Element | Status | Notă |
|---|---|---|
| Aplecare la stânga | ✅ | |
| Aplecare la dreapta | ✅ | |
| Corpul chiar se apleacă | ✅ | poziție + hitbox |
| Camera urmează corpul | ✅ | |
| Arma urmează corpul | ✅ | |
| Ajustare IK | ⚠️ | mâinile urmează arma; fără coloană |
| Aplecare care ține cont de coliziuni | ✅ | |
| Animația aplecării | ⚠️ | interpolare lină; fără animație de corp |
| Restricții la mișcare | ✅ | nu la sprint, redus culcat |

---

## 21. Stamina / Fatigue
**Status:** ✅ IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| Valoare de stamina | ✅ | |
| Consum la sprint | ✅ | × încărcătură × arma |
| Refacerea staminei | ✅ | depinde de puls și stres |
| Oboseală | ✅ | `exhausted` + oboseala de luptă |
| Oboseala afectează respirația | ✅ | |
| Oboseala afectează stresul | ✅ | |
| Oboseala afectează ținta | ✅ | |
| Oboseala afectează reculul | ✅ | |
| Oboseala afectează reîncărcarea | ✅ | |
| Oboseala afectează mișcarea | ✅ | |
| Refacerea după oboseală | ✅ | |

---

## 22. Tablet
**Status:** ⚠️ PARTIALLY IMPLEMENTED (obiectul fizic și jocul ✅; meniurile de meta-joc ❌)

**Current implementation:** Tableta e o cutie 3D ținută în mâna stângă, cu inerție, respirație și tremur. Ecranul e mapat în perspectivă (intersecție rază–plan per pixel) cu o textură UI de 256×168, amprente, zgârieturi, reflexii și strălucire automată. Degetul drept atinge ecranul la poziția cursorului, iar brațele sunt rezolvate prin IK. Ridicarea și coborârea depind de motricitatea fină. Arma coboară, camera privește în jos, iar fundalul se estompează. **Jocul nu e pus pe pauză**: corpul rămâne vulnerabil și se mișcă mai încet.

Paginile sunt:
- **HARTĂ:** zoom, rotire, echipa, uși, obiective, marcaje și ping-uri, inamicii de la terminal;
- **ECHIPĂ:** starea fiecăruia, silueta zonelor rănite, puls, stres, muniție estimată, greutate;
- **MISIUNE:** obiective, curent, terminal, uși, timp, scor;
- **CAMERĂ:** feed live de la dronă, RC sau bodycam-ul unui coechipier.

**Code location:** `src/render/tablet_ui.cpp` (`tablet_ui`), `src/render/viewmodel.cpp` (blocul „Tableta”), `src/app/game_view.cpp`, `src/sim/world.cpp` (`a.tablet`), `src/sim/interaction.cpp` (`update_markers`).

| Element | Status | Notă |
|---|---|---|
| Model fizic | ⚠️ | cutie |
| Ecran (mesh) | ✅ | |
| Materialul ecranului | ✅ | amprente, zgârieturi, reflexie |
| Afișaj emisiv | ✅ | |
| Luminozitatea ecranului | ✅ | |
| Interacțiune fizică cu mâna | ⚠️ | atingere cu degetul; fără gesturi |
| IK de mână | ✅ | |
| Animația de deschidere | ✅ | |
| Animația de închidere | ✅ | |
| Tranziția camerei | ✅ | |
| Arma coborâtă | ✅ | |
| Starea de interacțiune | ✅ | cursor |
| Nu pune jocul pe pauză | ✅ | |
| Jucătorul rămâne vulnerabil | ✅ | |
| UI pe tabletă | ✅ | |
| Echipament (loadout) | ❌ | |
| Configurarea meciului | ❌ | |
| Setări | ❌ | |
| Social | ❌ | |
| Tabelă de scor | ⚠️ | scor pe echipe; fără tabelă pe jucător |
| Hartă | ✅ | |
| Obiective | ✅ | |
| Informații tactice | ✅ | |
| Interfață de gadgeturi | ⚠️ | doar vizualizare |
| Interfața dronei | ⚠️ | doar feed |
| Interfața RC | ⚠️ | doar feed |
| Mod accesibil pe tot ecranul | ❌ | |

**Recommended implementation:** Pagini noi în `tablet_ui.cpp`: ECHIPAMENT, SETĂRI, SCOR (pe agent: eliminări, căderi, morți). Comenzi către simulare prin `TabletCommand` (de ex. „trimite drona la marcaj”). Modul pe tot ecranul randează textura tabletei 1:1 peste imagine.

---

## 23. FPV Drone
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Drona are fizică de tracțiune (vector din pitch, roll, yaw), rezistența aerului și coliziuni cu ricoșeu și daune. Modurile sunt **Angle** (stabilizat, menține altitudinea; echivalentul „Casual”) și **Acro** (rate mode, fără auto-nivelare; testat). Bateria durează 3 min. Camera FPV e înclinată în sus, cu profil analogic: zgomot ×1,8, distorsiune puternică și rupturi de semnal în funcție de distanță, pereți și baterie. OSD-ul arată modul, tensiunea, altitudinea, viteza și calitatea legăturii. Sunetul e o buclă cu pitch după tracțiune. Drona poate fi doborâtă cu focuri sau cu o grenadă. Fără pilot, se nivelează și coboară.

**Code location:** `src/sim/pawns.cpp` (`step_drone`), `src/sim/world.cpp` (`toggle_pawn`, `update_pawns`), `src/render/body_camera.cpp` (`drone_pose`), `src/app/game_view.cpp` (OSD).

| Element | Status | Notă |
|---|---|---|
| Dronă fizică | ✅ | |
| Camera dronei | ✅ | |
| Feed video FPV | ✅ | și pe tabletă |
| Mișcarea dronei | ✅ | |
| Fizica dronei | ✅ | |
| Bateria dronei | ✅ | |
| Rechemarea dronei | ⚠️ | V întoarce controlul; drona rămâne și aterizează |
| Întoarcerea la jucător | ❌ | |
| Mini-hartă pentru dronă | ⚠️ | punct pe tabletă; nu în OSD |
| Mod Casual | ✅ | Angle |
| Mod Acro | ✅ | |
| Sensibilitatea dronei | ❌ | constantă în cod |
| Profiluri de control | ❌ | |
| Audio dronă | ✅ | |
| Distrugere / prăbușire | ✅ | |
| Recuperarea dronei | ❌ | nu poate fi ridicată și refolosită |
| Sincronizare în rețea | ❌ | |

---

## 24. RC Car
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Model bicicletă cu derapaj la depășirea aderenței, accelerație și frână diferite, înainte și înapoi. Camera e la 0,2 m, cu ruliu și tangaj de suspensie. Vehiculul împinge obiectele fizice, are baterie și poate fi distrus. Fără control, se oprește și rămâne pe loc.

**Code location:** `src/sim/pawns.cpp` (`step_vehicle`), `src/render/body_camera.cpp` (`vehicle_pose`).

| Element | Status | Notă |
|---|---|---|
| Mașină RC fizică | ✅ | |
| Camera RC | ✅ | |
| Înainte | ✅ | |
| Înapoi | ✅ | |
| Direcție | ✅ | |
| Feed de jos | ✅ | |
| Mini-hartă | ⚠️ | pe tabletă |
| Mod de așteptare | ⚠️ | implicit, la eliberare |
| Rechemare | ❌ | |
| Încărcătură explozivă | ❌ | |
| Detonare | ❌ | |
| Distrugere | ✅ | |
| Sincronizare în rețea | ❌ | |

**Recommended implementation:** Explozivul: `Vehicle.charge = true` și o tastă care apelează `explode()` (deja existent pentru grenade) la poziția vehiculului. Rechemarea: drum BFS (`compute_bfs_path`) până la proprietar, cu pilot automat.

---

## 25. Interaction System
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Cadrul generic `Interactable` (tip, poziție, timp de ținut, stare, cameră, încuietoare, obiect ridicat, țintă) e folosit prin `find_interactable` (distanță + con + LOS), `update_interaction` (instant / ținut / întrerupt la mișcare sau lovitură, timpul împărțit la `fine_motor`) și `complete_interaction`. Toate se sincronizează prin evenimente cu sunet.

**Code location:** `src/sim/interaction.hpp/.cpp`.

| Element | Status | Notă |
|---|---|---|
| Interfață generică | ✅ | |
| Uși | ✅ | |
| Sertare | ❌ | există dulapuri, nu sertare |
| Întrerupătoare | ✅ | lumini pe încăpere, tablou electric |
| Obiective | ⚠️ | terminal ✅; zona se capturează pe suprafață, nu prin interacțiune |
| Calculatoare | ✅ | terminal (8 s) |
| Tabletă | ⚠️ | sistem separat, nu `Interactable` |
| Vehicule | ⚠️ | prin taste (V, B), nu prin interacțiune |
| Gadgeturi | ⚠️ | idem |
| Arme | ❌ | `PickupKind::Weapon` există, dar nu e tratat |
| Interacțiune ținută | ✅ | |
| Interacțiune instantanee | ✅ | |
| Anularea interacțiunii | ✅ | |
| Interacțiune cu animație | ❌ | fără mâini sau corp |
| Sunete de interacțiune | ✅ | |

**Recommended implementation:** Tratează `PickupKind::Weapon` (schimbă slotul, iar arma veche cade ca obiect de ridicat). Mâna stângă a viewmodel-ului întinsă spre `it.x/y/z` cât timp `interact_progress > 0`.

---

## 26. Door System
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Panoul ușii e un segment care se rotește pe balama (raycast segment–rază) și se deschide departe de jucător. Ținut apăsat, ușa se deschide încet și rămâne întredeschisă. Ușile încuiate (`X`) se sparg din lovituri cu piciorul (șansă din stamina și picior), cu glonț lângă clanță sau cu grenada. Ușa e penetrabilă (lemn, 6 cm). Închisă, blochează LOS-ul, lumina și sunetul, iar iluminarea se recalculează la schimbare.

| Element | Status | Notă |
|---|---|---|
| Deschidere | ✅ | |
| Închidere | ✅ | nu peste cineva din prag |
| Împingere | ✅ | |
| Tragere | ❌ | mereu împingere |
| Încuiată | ✅ | |
| Descuiată | ✅ | |
| Parțial deschisă | ✅ | |
| Deschisă forțat | ✅ | testat |
| Coliziune fizică | ⚠️ | pe celulă (sub 60% deschidere blochează); panoul deschis nu are coliziune |
| Interacțiunea cu gloanțele | ✅ | |
| Sunetul ușii | ✅ | |
| Sunetul ușii prin pereți | ✅ | ocluzie |

---

## 27. Environment
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Lightmap-ul pe celule e recalculat la schimbările de uși, lămpi și curent. Lumina vine de la soare, cer, lămpi, lumina de zi care intră pe uși și ferestre, plus lumini roșii de avarie. Umbrele de soare sunt exacte față de pereți (DDA). Luminile dinamice sunt flăcări și lanterne, cu ocluzie pe celulă. Materialele procedurale sunt beton, tencuială cu lambriu, lemn, containere ruginite, rafturi, rigips, piatră, ferestre, asfalt, pământ, iarbă și mochetă, toate cu imperfecțiuni. Mai există găuri de glonț, praf, așchii, scântei, fum, ferestre care se sparg, praf volumetric, adaptarea expunerii și ocluzie ambientală.

| Element | Status | Notă |
|---|---|---|
| Iluminare dinamică | ⚠️ | lightmap reconstruit + lumini punctuale și spot |
| Iluminare globală (echivalent Lumen) | ⚠️ | ricoșeu aproximativ pe celule |
| Umbre dinamice | ⚠️ | soarele față de pereți ✅; personajele și obiectele nu fac umbră |
| Materiale realiste | ✅ | procedurale |
| Imperfecțiuni ale materialelor | ✅ | |
| Distrugere unde e cazul | ⚠️ | doar ferestrele |
| Decaluri de glonț | ✅ | |
| Găuri de glonț | ✅ | |
| Resturi | ✅ | particule |
| Sticlă | ✅ | |
| Praf | ✅ | |
| Fum | ✅ | |
| Particule de mediu | ⚠️ | doar praf volumetric |
| Vreme | ❌ | |
| Adaptarea expunerii interior/exterior | ✅ | |

**Limitare de motor:** fără geometrie 3D, mobilier sau niveluri pe verticală (2.5D).

---

## 28. Flashlight
**Status:** ⚠️ PARTIALLY IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| Lanternă fizică | ⚠️ | cutie pe armă |
| Iluminare dinamică | ✅ | spot cu con |
| Umbre | ⚠️ | ocluzie pe celulă; fără umbre de personaje |
| Atenuarea fasciculului | ✅ | |
| Baterie | ✅ | 15 min, pâlpâie la final |
| Comutare | ✅ | L |
| Montată pe armă | ✅ | urmează direcția reală a țevii |
| Laser | ❌ | |
| Lumina afectează mediul | ✅ | + volumetrie |
| Lumina dezvăluie poziția | ✅ | `visibility_of` → detecția AI |

---

## 29. Grenades / Throwables
**Status:** ⚠️ PARTIALLY IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| Grenadă de fragmentare | ✅ | |
| Grenadă flash | ❌ | |
| Traiectorie fizică | ✅ | ricoșeu pe pereți, podea, tavan |
| Arcul aruncării | ⚠️ | fără previzualizare; unghi fix +0,25 |
| Ținerea grenadei (cook) | ❌ | |
| Explozie | ✅ | |
| Fragmentare | ⚠️ | daună radială cu LOS; fără schije |
| Efect de orbire | ⚠️ | alb la explozie apropiată |
| Efect audio | ✅ | |
| Fum, dacă e cazul | ❌ | |
| Reacția auzului | ✅ | |
| Reacția ecranului și a camerei | ✅ | |
| Interacțiunea cu mediul | ✅ | uși, ferestre, obiecte, drone |

---

## 30. Game Modes
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** Două moduri, ambele **PvE contra boților**:
- **Elimination:** echipa Blue elimină suspecții;
- **Objective:** echipa Blue capturează o zonă timp de 8 s (în stilul hardpoint, o singură zonă).

Rundele au limită de timp, scor pe echipe, restart după 6 s și spawn din markerii hărții. Opțional, poți adăuga coechipieri AI.

| Mod | Status | Notă |
|---|---|---|
| Deathmatch | ❌ | |
| Team Deathmatch | ❌ | fără mai mulți jucători |
| Wingman 2v2 | ❌ | |
| Bombă plantare/dezamorsare | ❌ | |
| Hardpoint | ⚠️ | zonă fixă, contra AI |
| Gun Game | ❌ | |
| Versus | ⚠️ | runde pe eliminare, contra AI |
| Zombies / co-op PvE | ⚠️ | PvE cu coechipieri AI; fără co-op uman |

| Aspect | Status |
|---|---|
| Fluxul meciului | ⚠️ runde automate |
| Spawn | ✅ |
| Obiective | ✅ |
| Scor | ⚠️ doar pe echipe |
| Logica rundelor | ✅ |
| Condiții de victorie | ✅ |
| Logica echipelor | ✅ |
| Respawn | ❌ |
| Spectatori | ❌ |
| Tabelă de scor | ⚠️ |

---

## 31. Multiplayer
**Status:** ❌ NOT IMPLEMENTED (există infrastructură parțială)

**Ce există cu adevărat:**
- `World::step_humans` acceptă oricâți jucători umani;
- simularea e deterministă și separată de prezentare;
- `simulate_player_movement` e o funcție pură, gândită pentru predicție;
- istoricul pozițiilor (40 de tick-uri) și `collect_targets(rewind_tick)` există, dar **nu sunt folosite**: niciun client nu setează `view_tick`;
- `src/net/socket.*` (UDP, loopback, latență simulată) și `src/net/protocol.*` (intrări, evenimente, instantanee cuantizate, μ-law) sunt **scrise, dar nu sunt în `CMakeLists.txt` și n-au fost compilate**;
- `make_network_session()` întoarce `nullptr`.

| Element | Status |
|---|---|
| Arhitectură client/server | ❌ |
| Autoritatea serverului | ⚠️ (simularea e autoritară local; fără server de rețea) |
| Sănătate replicată | ❌ |
| Daune replicate | ❌ |
| Starea armei replicată | ❌ |
| Muniție replicată | ❌ |
| Starea reîncărcării replicată | ❌ |
| Inventar replicat | ❌ |
| Gadgeturi replicate | ❌ |
| Obiective replicate | ❌ |
| Mișcarea jucătorului replicată | ❌ |
| Morți replicate | ❌ |
| Arme căzute replicate | ❌ |
| Chat vocal | ❌ |
| Voce de proximitate | ❌ |
| Voce de echipă | ❌ |
| Migrarea host-ului | ❌ |
| Recuperarea conexiunii | ❌ |
| Restricții de intrare în meci | ❌ |
| Predicție de rețea | ❌ (funcția pură există) |
| Compensarea lag-ului | ⚠️ (rewind implementat, neconectat) |
| Validare pe server | ❌ |

**Recommended implementation:** `GameServer` (cozi de intrări per client, instantanee la 30 Hz, evenimente fiabile cu id), `ClientCore` (predicție + reîncărcarea intrărilor neconfirmate, buffer de interpolare la 100 ms, `view_tick`) și `HostSession`/`ClientSession` în `net_session.cpp`. Apoi fișierele `src/net/*` intră în CMake, cu `ws2_32` și teste loopback cu latență.

---

## 32. Voice Chat
**Status:** ❌ NOT IMPLEMENTED

Ce există e neconectat: în `AudioEngine` există `push_voice` (buffer de jitter + filtru de stație + clic de squelch) și `start_capture`/`drain_capture` (microfon SDL la 16 kHz). Tastele Alt și N setează `input.ptt_*`, care **nu sunt citite nicăieri**. Codarea μ-law e în `protocol.cpp`, necompilat. **Ai spus că audio nu e necesar acum**, deci recomand ca vocea să rămână pe ultimul loc.

| Element | Status |
|---|---|
| VOIP | ❌ |
| Push-to-talk | ❌ |
| Microfon deschis | ❌ |
| Comunicare pe echipă | ❌ |
| Voce de proximitate | ❌ |
| Filtru de stație radio | ❌ (DSP scris, nefolosit) |
| Compresie | ❌ |
| Poziționare spațială | ❌ |
| Animație de vorbire | ❌ |
| Sincronizare în rețea | ❌ |

---

## 33. Inventory / Progression
**Status:** ⚠️ PARTIALLY IMPLEMENTED (inventarul ✅; progresia ❌)

| Element | Status | Notă |
|---|---|---|
| Inventar | ✅ | arme, încărcătoare individuale, grenade, bandaje, tabletă, baterie, greutate |
| Echipament | ⚠️ | linia de comandă |
| Deblocarea armelor | ❌ | |
| Deblocarea atașamentelor | ❌ | |
| Monedă / puncte | ❌ | |
| Economie în stil Reissad | ❌ | |
| Raritatea armelor | ❌ | |
| Progresie | ❌ | |
| Rang | ❌ | |
| Arhitectură ELO / ranked | ❌ | |
| Prezentarea obiectelor | ❌ | |

---

## 34. Hub / Shooting Range
**Status:** ❌ NOT IMPLEMENTED

`assets/maps/range.txt` există doar pentru experimente. Nu există un hub persistent, meniuri fizice sau o zonă de testat arme. Jocul pornește direct în rundă.

| Element | Status |
|---|---|
| Hub persistent de tir | ❌ |
| Mediu fizic în loc de meniuri | ❌ |
| Testarea armelor | ❌ |
| Interacțiunea cu echipamentul | ❌ |
| Interacțiunea cu tableta | ❌ |
| Acces la setări | ❌ |
| Testarea gadgeturilor | ❌ |
| Fluxul hartă / meci | ❌ |
| Mediu interactiv | ❌ |

---

## 35. HUD / Immersion
**Status:** ✅ IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| HUD minimal | ✅ | |
| Fără crosshair permanent | ✅ | (OSD-ul dronei are „+”, ca o dronă reală) |
| Fără contor permanent de muniție | ✅ | |
| Verificarea fizică a încărcătorului | ✅ | |
| Informații fizice despre armă | ✅ | culisa deschisă, încărcătorul vizibil, țeava roșie, port-încărcătoare |
| UI diegetic | ✅ | tabletă, timestamp ars în imagine |
| Informații pe tabletă | ✅ | |
| Obiective diegetice | ⚠️ | terminalul are progres pe monitor; zona nu e marcată în lume |
| Feedback fizic la interacțiune | ⚠️ | indiciu text discret (nediegetic) + sunet |

---

## 36. Animation System
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Current implementation:** **Nu există sistem de animație cu schelet sau asset-uri.** Totul e procedural: greutăți de amestec (`sprint_w`, `reload_w`, `inspect_w`, `tablet_w`, `low_w`), arcuri (`back`, `pump`, `jolt`, `lag_x/y`), IK pe 2 segmente, traiectorii Bezier și zgomot neted. Reîncărcarea e condusă de mașina de stări din simulare. Ceilalți agenți sunt siluete 2D cu animație de mers și înclinări.

| Element | Status | Notă |
|---|---|---|
| Animație pe straturi | ⚠️ | amestec procedural pe greutăți |
| Animații contextuale | ✅ | |
| Animație procedurală | ✅ | |
| Animație specifică armei | ⚠️ | geometrie și câteva etape diferite |
| Strat de stres | ✅ | `hand_lag`, înclinare, cameră |
| Strat de regrip | ❌ | |
| Variații la reîncărcare | ✅ | |
| Bucle de anulare | ⚠️ | revenire lină; fără animație dedicată |
| Animații de inspectare | ⚠️ | doar încărcătorul |
| Animații de verificare a încărcătorului | ✅ | |
| Animații de echipament | ⚠️ | tabletă ✅; grenadă și bandaj ❌ |
| Tranziții între arme | ⚠️ | coborâre și ridicare |
| Animații pe postură | ⚠️ | camera; arma nu se schimbă pentru culcat |
| Animații de rănire | ⚠️ | camera + sprite-urile celorlalți |
| Animații de interacțiune | ❌ | |
| Amestecarea IK | ✅ | |
| Ofseturi procedurale | ✅ | |
| Mașină de stări a animației | ⚠️ | doar pentru reîncărcare |

**BODYCAM comparison:** Aici e diferența cea mai mare de **fidelitate**: BODYCAM combină animații motion capture cu straturi procedurale. Aici e doar stratul procedural, pe cutii.

---

## 37. Physicality
**Status:** ⚠️ PARTIALLY IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| Arme | ❌ | nu cad și nu sunt obiecte fizice |
| Încărcătoare | ⚠️ | cad ca obiect static de ridicat; nu cad fizic |
| Tuburi de cartuș | ✅ | gravitație, ricoșeu, rostogolire, sunet |
| Uși | ⚠️ | animate, nu fizice |
| Grenade | ✅ | |
| Mașina RC | ✅ | |
| Drona | ✅ | |
| Corpuri | ⚠️ | cădere scriptată, fără ragdoll |
| Resturi | ✅ | particule cu gravitație |
| Sticlă | ⚠️ | particule; cioburile nu rămân pe jos |
| Obiecte interactive | ✅ | lăzi, butoaie, conserve: impuls, frecare, somn |

---

## 38. Performance
**Status:** ⚠️ PARTIALLY IMPLEMENTED

**Măsurat** (smoke test, 640×360, fereastră ascunsă): ~70 FPS, cu vsync limitat la ~60–70. Scena ia 3,4 ms (max 5,4), post-procesarea 6,0 ms (max 9,3), volumetria 0,5 ms, simularea 0,04 ms pe pas.

| Element | Status | Notă |
|---|---|---|
| Fără Tick inutil | ⚠️ | obiectele „dorm” ✅; restul se actualizează la fiecare pas |
| Sisteme bazate pe evenimente | ✅ | `EventLog` |
| Pooling de obiecte | ⚠️ | tuburile ✅ |
| Pooling de particule | ❌ | `vector::erase` + plafon |
| Pooling de tuburi | ✅ | 160 |
| Rețea eficientă | ❌ | nu există rețea |
| LOD | ❌ | |
| Optimizarea animației | ❌ | |
| Optimizarea audio | ⚠️ | 112 voci, cu înlocuirea celor vechi |
| Optimizare GPU | ❌ | randarea e pe CPU |
| Optimizare CPU | ✅ | OpenMP, volumetrie la 1/4 rezoluție, profiler |
| Gestionarea memoriei | ⚠️ | |
| Fără alocări per cadru | ❌ | `World::step_humans` copiază toți agenții (`prev`) la fiecare pas; `collect_targets` alocă la fiecare segment de proiectil; listele de lumini și sprite-uri se alocă la fiecare cadru |
| Fără blocaje evidente | ⚠️ UNVERIFIED | doar la 640×360; scalarea la 1080p e neverificată (post-procesarea ar urca la ~50 ms) |

---

## 39. Code Architecture
**Status:** ⚠️ PARTIALLY IMPLEMENTED

| Sistem din design | Echivalent în proiect |
|---|---|
| Player | `Agent` (`agent.hpp`) + `player_sim.cpp` |
| Camera | `BodyCamera` |
| Stress / Breathing / HeartRate | `Physiology` (un singur modul) |
| Stamina | `player_sim.cpp` |
| Health | `BodyHealth` (`health.hpp`) |
| Weapon | `WeaponData` + `WeaponSlot` + `World::human_weapon` |
| Recoil | în `World::fire_human` + viewmodel + cameră |
| Reload | `reload.cpp` + `World::advance_reload` |
| Ballistics | `ballistics.cpp` + proiectile în `world.cpp` |
| Inventory / Equipment | `Inventory`, `WeaponSlot` |
| Interaction | `interaction.cpp` |
| Tablet | `tablet_ui.cpp` + viewmodel |
| Drone / RC car | `pawns.cpp` |
| Audio | `audio_engine.cpp`, `spatial_audio_engine.hpp` |
| GameMode | `GameState` + `World::update_game_state` + `scenario.cpp` |
| Networking | `src/net/*` (neconectat) |
| Animation | `viewmodel.cpp` (procedural) |

| Criteriu | Status | Notă |
|---|---|---|
| Modular | ✅ | sim / render / audio / app separate |
| Reutilizabil | ⚠️ | |
| Data-driven | ✅ | |
| Configurabil | ✅ | |
| Pregătit pentru rețea | ⚠️ | determinism + funcții pure; fără rețea |
| Testabil | ✅ | 18 teste fără interfață grafică, determinism verificat |
| Ușor de întreținut | ⚠️ | `World` are prea multe responsabilități (~1500 de linii pe 3 fișiere), iar `Agent` e un agregat mare |
| Fără cuplare inutilă | ⚠️ | prezentarea citește direct câmpurile din `Agent` |

---

## 40. Data-Driven Design
**Status:** ⚠️ PARTIALLY IMPLEMENTED

| Element | Status | Notă |
|---|---|---|
| Data Assets | ✅ | fișiere INI |
| Data Tables | ⚠️ | câte un INI pe obiect; fără tabele |
| Structuri de configurare | ✅ | |
| Configurarea armelor | ✅ | `assets/weapons/*.ini` (~50 de câmpuri) |
| Configurarea atașamentelor | ⚠️ | catalog hardcodat în `weapon_mod.hpp` |
| Configurare audio | ❌ | sinteza e hardcodată; doar înlocuire prin WAV |
| Configurarea stresului | ✅ | `assets/config/physiology.ini` |
| Configurarea animației | ❌ | ofseturile viewmodel-ului sunt în cod |
| Configurarea mișcării | ⚠️ | `Params` în cod; doar runner-ul suprascrie din INI |
| Parametri de gameplay | ⚠️ | scenarii INI pentru experimente; jocul interactiv folosește valorile din cod |

---

## 41. FINAL BODYCAM COMPARISON

| BODYCAM feature | Current project | Status | Quality | Missing work |
|---|---|---|---|---|
| Cameră de corp | cameră pe arcuri, montată pe piept, cu toate influențele fiziologice | ⚠️ | 75% | obstrucție, DoF general, mantle |
| Corp real / personaj | brațe cu IK, picioare; ceilalți sunt sprite-uri 2D | ⚠️ | 30% | corpuri 3D cu schelet |
| Manevrarea armei | inerție în simulare, legănare procedurală bogată | ⚠️ | 60% | lungimea armei, regrip, toc, fidelitate |
| Recul | multi-component, multi-factor | ✅ | 70% | derivat din calibru, animație |
| Reîncărcare procedurală | 7 stiluri, stare reluabilă, încărcătoare fizice | ✅ | 65% (logică 90% / vizual 35%) | regrip, poziții pe postură, fidelitate |
| Stres | continuu, histerezis, cronic | ✅ | 85% | proximitate continuă, efect asupra mișcării |
| Respirație | procedurală + sinteză pe straturi | ✅ | 75% | feedback asupra stresului, verificare audio |
| Puls | complet | ✅ | 85% | intrare în tremur |
| Tremurul mâinilor | multi-frecvență, în tragere | ✅ | 80% | scos din cameră |
| Degradarea auzului | completă | ⚠️ | 60% | neverificat perceptual |
| Audio spațial | 2D, ocluzie, reverb | ⚠️ | 45% | vertical, difracție, whiz |
| Balistică | proiectile fizice + penetrare | ✅ | 80% | ricoșeu care continuă |
| Daune / reacții | 6 zone, sângerare, incapacitare | ✅ | 75% | ragdoll |
| Mișcare | completă, fără mantle | ✅ | 75% | mantle |
| Aplecare | fizică, ține cont de coliziuni | ✅ | 70% | animație de corp |
| Tabletă | obiect fizic + 4 pagini | ⚠️ | 60% | meniuri de meta-joc |
| Dronă FPV | fizică + 2 moduri + feed | ⚠️ | 55% | rechemare, recuperare, profiluri, rețea |
| Mașină RC | fizică + feed | ⚠️ | 45% | exploziv, rechemare, rețea |
| Interacțiuni | cadru generic + uși | ⚠️ | 60% | arme, animații |
| Uși | balama, încuiate, forțate | ⚠️ | 75% | tragere, coliziunea panoului |
| Customizare | atașamente data-driven | ⚠️ | 35% | UI, optici, statistici separate |
| Optici | red dot | ⚠️ | 15% | PiP, holografic, canted |
| Moduri de joc | 2 moduri PvE | ⚠️ | 15% | modurile multiplayer |
| Multiplayer | infrastructură neconectată | ❌ | 3% | tot |
| Voce | DSP neconectat | ❌ | 0% | tot (deprioritizat de tine) |
| Hub / tir | — | ❌ | 0% | tot |
| Progresie | — | ❌ | 0% | tot |
| UI diegetic | tabletă, OSD, verificarea încărcătorului | ✅ | 75% | marcaje în lume |
| Animație | procedurală, fără schelet | ⚠️ | 30% | asset-uri / sistem de schelet |
| Mediu / iluminare | lightmap + umbre de soare + volumetrie, 2.5D | ⚠️ | 45% | 3D, GI, umbrele personajelor |

### BODYCAM SYSTEM COVERAGE: **~52%**

Ponderile (suma 100) sunt pe calitatea fiecărui sistem:
- cameră de corp 10 × 75%;
- manevrarea armei 9 × 60%;
- animație procedurală 8 × 30%;
- reîncărcare 9 × 65%;
- stres 6 × 85%;
- respirație și puls 5 × 80%;
- audio 7 × 50%;
- multiplayer 10 × 3%;
- tabletă 6 × 60%;
- gadgeturi 5 × 50%;
- interacțiuni 5 × 65%;
- daune 6 × 75%;
- mișcare 6 × 75%;
- restul (balistică, mediu, moduri, customizare, meta) 8 × 30%.

Totalul ≈ 52%.

---

## 42. CRITICAL MISSING SYSTEMS

1. **CRITICAL – Multiplayer funcțional.** BODYCAM e un joc multiplayer. Fără server, client, predicție și replicare, modurile de joc, vocea și compensarea lag-ului nu au sens. Infrastructura (determinism, intrare pentru mai mulți jucători, rewind, protocol) e pe jumătate făcută.
2. **CRITICAL – Corpuri 3D cu schelet pentru ceilalți jucători.** Sprite-urile 2D strică senzația de realism mai mult decât orice altceva, iar reacțiile la lovituri, aplecarea și culcatul celorlalți rămân aproximări.
3. **CRITICAL – Fidelitatea animației din prim-plan.** Logica (reîncărcare, recul, IK) e bună, dar geometria din cutii nu arată ca o armă ținută de mâini reale. Ar fi nevoie de mesh-uri reale pentru armă și mâini, cu degete.
4. **HIGH – Moduri de joc reale** (TDM, Wingman 2v2, Hardpoint, Gun Game). Fluxul de meci al BODYCAM depinde de ele.
5. **HIGH – Optici** (PiP cu mărire, holografic, canted). Fac parte centrală din sistemul de arme și atașamente.
6. **HIGH – Echipament și customizare în joc** (pagina de echipament de pe tabletă, catalog de atașamente în date, statistici separate).
7. **MEDIUM – Coliziunea armei cu pereții (lungimea armei) și regrip.** Sunt specifice manevrării realiste și lipsesc complet.
8. **MEDIUM – Grenadă flash, cook și arc de aruncare.** Sunt utilitare tactice de bază.
9. **MEDIUM – Hub / tir.** Înlocuiește meniurile și e locul unde se testează armele.
10. **LOW – Progresie, economie, rang.** Sunt meta-joc, fără efect asupra simulării.

---

## 43. NEXT IMPLEMENTATION ROADMAP

| Fază | Sisteme exacte | Dependențe | Fișiere de modificat | Fișiere noi | Dificultate | Efort relativ |
|---|---|---|---|---|---|---|
| **1. Simularea jucătorului** | coliziunea armei cu pereții, mantle (înălțime pe celulă), proximitate în stres, pulsul în tremur, respirația ținută | — | `player_sim.cpp`, `physiology.cpp`, `map.hpp/.cpp`, `agent.hpp` | — | medie | 2 |
| **2. Camera de corp** | obstrucție, DoF cu autofocus, scoaterea tremurului din cameră, tranziție de feed la gadgeturi | 1 | `body_camera.cpp`, `postfx.cpp`, `game_view.cpp` | — | mică | 1 |
| **3. Manevrarea armei** | regrip, traiectorii pentru toc și scoatere, inspectare, animații pentru grenadă și bandaj, **mesh-uri reale pentru armă și mâini** | 1 | `viewmodel.cpp/.hpp`, `weapon_data.*` | `render/mesh.hpp/.cpp` (OBJ + rasterizare de triunghiuri cu z-buffer), `assets/models/*` | **mare** | 5 |
| **4. Reîncărcare procedurală** | poziții pentru ghemuit și culcat, încărcător nefixat, eșecuri, regrip în timpul reîncărcării | 3 | `reload.cpp`, `world.cpp`, `viewmodel.cpp` | — | medie | 2 |
| **5. Stres / respirație / puls** | efectul respirației asupra stresului, efectul stresului asupra mișcării | 1 | `physiology.cpp`, `player_sim.cpp` | — | mică | 1 |
| **6. Audio** (**deprioritizat la cererea ta**) | whiz/crack + undă de șoc, difracție, voce | 10 | `audio_engine.cpp`, `game_view.cpp` | — | medie | 2 |
| **7. Tabletă** | pagini ECHIPAMENT, SETĂRI, SCOR; comenzi pentru gadgeturi; mod pe tot ecranul | 3, 8 | `tablet_ui.cpp`, `game_view.cpp`, `agent.hpp` | `assets/attachments/*.ini` | medie | 2 |
| **8. Dronă / RC** | rechemare și întoarcere (BFS), recuperare, sensibilitate și profiluri, exploziv pe RC | — | `pawns.cpp`, `world.cpp`, `main.cpp` | `assets/config/gadgets.ini` | medie | 2 |
| **9. Daune / reacții fizice** | ragdoll simplificat (lanț de 3–4 segmente cu constrângeri) pentru cădere și moarte; corpuri 3D cu schelet procedural | 3 (rasterizare de mesh) | `raycaster.cpp`, `physics_stance.hpp` | `render/character.cpp`, `sim/ragdoll.cpp` | **mare** | 4 |
| **10. Multiplayer** | `GameServer`, `ClientCore`, Host/Client session, reconciliere, interpolare, lag compensation conectat, validare; moduri TDM, Wingman, Hardpoint, Gun Game | 1 | `net_session.cpp`, `CMakeLists.txt` (+ws2_32), `world.cpp` (validare), `scenario.cpp` (moduri) | `net/server.*`, `net/client.*`, `game/modes.cpp` | **mare** | 5 |
| **11. Mediu / interacțiune** | tragerea ușilor, coliziunea panoului, sertare, ridicarea armelor, grenadă flash, cook, arc, podele de metal și piatră, cioburi pe jos | 1 | `interaction.cpp`, `map.cpp`, `world.cpp`, `surface.hpp` | — | medie | 3 |
| **12. Finisare / optimizare** | fără alocări per cadru (buffere reutilizate pentru `prev`, ținte, lumini), pool de particule, post-procesare la rezoluție mai mare, profilare la 1080p, catalog de atașamente și ofseturile viewmodel-ului mutate în date | toate | `world.cpp`, `fx.cpp`, `postfx.cpp`, `raycaster.cpp` | — | medie | 2 |

---

## OVERALL RESULT

```
BODYCAM COVERAGE:        ~52%
FULLY IMPLEMENTED:       415 elemente  (12 sisteme)
PARTIALLY IMPLEMENTED:   122 elemente  (24 de sisteme)
NOT IMPLEMENTED:         127 elemente  (3 sisteme)
UNVERIFIED:                2 elemente  (+ calitatea perceptuală audio în general)

REALISM SCORE:          48/100
FUNCTIONALITY SCORE:    62/100
ANIMATION SCORE:        28/100
WEAPON SYSTEM SCORE:    66/100
AUDIO SCORE:            50/100  (perceptual neverificat)
MULTIPLAYER SCORE:       5/100
IMMERSION SCORE:        58/100
```
(numărătoarea e pe elementele din checklist-uri)

### Cele 5 lucruri cu cel mai mare impact
1. **Multiplayer funcțional** (server autoritar + predicție + interpolare + lag compensation conectat). Deblochează moduri, echipe și tot ce e „BODYCAM ca joc”.
2. **Rasterizare de mesh-uri 3D peste raycaster** (z-buffer partajat): armă și mâini reale în prim-plan, corpuri cu schelet procedural pentru ceilalți.
3. **Coliziunea armei cu pereții + regrip + traiectorii de toc și scoatere.** Ultimele piese ale manevrării „fizice” a armei.
4. **Optici reale** (PiP cu mărire, holografic, canted) **+ echipament pe tabletă**, cu catalogul de atașamente mutat în date.
5. **Moduri de joc** (Team Deathmatch, Wingman 2v2, Hardpoint) pe baza fluxului de runde existent, după punctul 1.
