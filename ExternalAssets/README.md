# Resurse externe (ExternalAssets)

Acest folder ține modelele, texturile și sunetele luate din afara proiectului. Toate
modelele folosite acum în joc sunt **procedurale** (construite din cod: arme, atașamente,
personaje, mașina RC, drona), deci folderul e gol.

## Reguli

1. Nicio resursă nu se descarcă fără aprobarea utilizatorului. Pentru fiecare resursă se
   prezintă: numele fișierului, sursa (pagina), mărimea și licența.
2. Sunt permise doar licențele: **CC0 / domeniu public**, **CC-BY** (cu autorul trecut),
   **MIT**, **Apache-2.0**, **BSD**, **zlib**.
3. Sunt refuzate: licențe necomerciale (NC), fără derivate (ND), share-alike (SA, se discută
   înainte), „editorial”, licențe necunoscute și orice resursă extrasă din jocuri comerciale
   (BODYCAM, Ready or Not etc.).
4. Fiecare resursă are o intrare în `manifest.ini`. Motorul verifică la încărcare
   (`src/core/asset_manifest.cpp`) că intrarea există, că licența e permisă, că are sursă, că
   are autor când licența cere atribuire și că `approved = yes`. Altfel modelul e refuzat, iar
   jocul folosește modelul procedural.

## Structură

| Folder        | Conținut                                    |
|---------------|---------------------------------------------|
| `Weapons/`    | arme și atașamente (OBJ + MTL)              |
| `Characters/` | personaje, mâini, echipament                |
| `Vehicles/`   | mașina RC                                   |
| `Drones/`     | drona FPV                                   |
| `Materials/`  | texturi (CC0)                               |
| `Audio/`      | sunete (WAV)                                |

## Folosirea unui model de armă

În fișierul armei (`assets/weapons/<arma>.ini`), secțiunea `[visual]`:

```ini
model = ../../ExternalAssets/Weapons/exemplu/rifle.obj
model_scale = 0.01        ; unitățile modelului -> metri
model_x = 0.0             ; originea modelului față de mâner (m)
model_y = 0.0
model_z = 0.0
model_yaw = 180           ; grade: aliniază țeava pe +z (înainte)
model_has_mag = no        ; încărcătorul rămâne cel animat de motor
```

Punctele de prindere (`[sockets]`: mâner, mâna stângă, gura țevii, optica) rămân în INI,
deci mâinile, reculul, atașamentele și luneta funcționează la fel și pe modelul extern.
