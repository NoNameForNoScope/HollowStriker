# BODYCAM-MINI — Arhitectura

Motor propriu C++17 + SDL2 (fereastra, input, audio). Fara Unreal: conceptele din
documentul de design (componente UE) sunt mapate pe module C++ simple.

## Straturi

```
                    bodycam_app (SDL2)                     bodycam_exp / bodycam_tests
   +--------------------------------------------------+    (fara SDL, doar nucleul)
   | app/main.cpp        input, fereastra, moduri      |
   | app/app_session     Local / Host / Client         |
   | app/net_session     sesiuni de retea              |
   | app/game_view       prezentare: evenimente->      |
   |                     camera, FX, audio, randare    |
   | audio/audio_engine  mixer, sinteza, spatializare  |
   +--------------------------------------------------+
                         | foloseste
   +--------------------------------------------------+
   | bodycam_core (biblioteca statica, determinista)   |
   |  core/   rng, hash, ini, csv, events, profiler    |
   |  sim/    World::step (60 Hz, pas fix)             |
   |  render/ raycaster HDR, camera corp, arma, post   |
   |  game/   scenario (configurarea rundei)           |
   |  net/    transport UDP, protocol, server, client  |
   |  experiment/ runner Monte Carlo                   |
   +--------------------------------------------------+
```

Regula: **simularea (sim/) nu stie nimic de randare, audio sau retea.** Comunica
prin `EventLog` (evenimente) si prin starea agentilor. Prezentarea (GameView)
citeste starea si consuma evenimentele; nu modifica simularea.

## Simularea (sim/)

| Modul | Rol | Echivalent din design |
|---|---|---|
| `world.hpp/.cpp` | pasul autoritar: perceptie, jucatori, arme, pawn-uri, obiecte, daune, starea jocului | GameMode + GameState |
| `agent.hpp` | starea unui corp: miscare, postura, arma, inventar, pawn | ABodycamCharacter |
| `player_sim.cpp` | miscarea fizica a jucatorului (pura, folosita si de predictia clientului) | UBodyMovementComponent |
| `physiology.cpp` | stres, adrenalina, puls, respiratie, tremur, control motor fin | UStressComponent, UBreathingComponent, UHeartRateComponent |
| `health.hpp` | sanatate localizata pe 6 zone, sangerare, incapacitare | UHealthComponent |
| `inventory.hpp` | arme, incarcatoare fizice, echipament, greutate | UInventoryComponent, UEquipmentComponent |
| `weapon_data.cpp` | date de arma din INI + atasamente | UWeaponDataAsset |
| `reload.cpp` | planuri de reincarcare (stiluri + variatie controlata), stari reluabile | UWeaponReloadComponent |
| `ballistics.cpp` | trasare cu penetrare, ricoseu, zone anatomice | UWeaponBallisticsComponent |
| `projectiles.cpp` | proiectile cu viteza, gravitatie, rezistenta aerului | proiectil fizic |
| `interaction.cpp` | cadru generic de interactiuni (usi, intrerupatoare, terminal, dulapuri, obiecte, resuscitare) | IInteractable |
| `physics_stance.hpp` | reactii fizice: tresarire / impleticire / cadere | ragdoll hibrid |
| `pawns.cpp` | drona FPV (stabilizat/acro), vehicul RC | ADronePawn, ARemoteVehiclePawn |
| `props.cpp` | obiecte fizice cu somn, grenade | Chaos (simplificat) |
| `lightmap.cpp` | iluminare precalculata (soare, cer, lampi, grupuri de lampi, avarie) | Lumen (simplificat) |
| `bot_ai.cpp` | FSM: patrulare, investigare, angajare, urmarire, reincarcare | AI |
| `game_state.hpp` | runda, scor, timp, obiective | AGameState |

Toate valorile reglabile stau in `assets/config/*.ini`, `assets/weapons/*.ini` si
`assets/scenarios/*.ini` (date, nu cod).

## Prezentarea (render/, audio/, app/game_view)

| Modul | Rol |
|---|---|
| `render/body_camera.cpp` | camera montata pe corp: arcuri, respiratie, puls, pasi, recul, impact, lean |
| `render/viewmodel.cpp` | arma, maini, antebrate, corpul propriu, tableta fizica, IK procedural |
| `render/raycaster.cpp` | lumea in HDR: pereti, usi rotative, ferestre, sprite-uri, particule, lumini dinamice, volumetrie |
| `render/postfx.cpp` | camera fizica: expunere, bloom, blur, rolling shutter, lentila, DoF, zgomot, compresie, OSD |
| `render/tablet_ui.cpp` | interfata tabletei randata intr-o textura de ecran |
| `audio/audio_engine.cpp` | voci spatializate, reverb, auz degradat, sinteza de respiratie/puls, voce radio |
| `app/game_view.cpp` | leaga evenimentele de camera/FX/audio, starea auzului |

## Retea (net/)

Server autoritar (host = server + jucator local). Clientul trimite intrari
(cu redundanta), serverul simuleaza si trimite instantanee cuantizate (30 Hz) +
evenimente fiabile. Clientul prezice miscarea proprie si reconciliaza, interpoleaza
ceilalti agenti; serverul compenseaza latenta (rewind) la tragere. Vocea (radio /
proximitate) trece prin server.

## Determinism

`World::step` este determinist pentru acelasi seed (RNG pe hash, fara stare
globala). Testele verifica asta. Efectele pur cosmetice (particule, sunete,
leganarea vizuala) nu influenteaza simularea.
