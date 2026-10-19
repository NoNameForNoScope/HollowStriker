# Progres

## Sunet (infundare, respiratie) + imagine (claritate, tableta)

**Sunet**
- Propagare prin materiale: fiecare tip de perete are transmisie si filtru propriu (beton ~450 Hz, lemn ~900 Hz, rigips ~1.2 kHz, geam ~2.5 kHz, usa dupa cat e deschisa).
- Difractie: camp de propagare din pozitia ascultatorului; sunetul ocoleste prin usi / ferestre sparte / ziduri scunde si vine din directia deschiderii. Usile inchise si geamurile intregi lasa sa treaca sunet atenuat (punctul slab al peretelui).
- Filtrare pe voce cu doi poli (12 dB/octava) in loc de unul: prin perete ramane doar basul.
- Auzul dupa explozii / focuri: trece-jos Butterworth de ordinul 4 + adancitura la 4 kHz, fara distorsiune; tiuit aproape pur (~4.15 kHz) care deriva lent.
- Respiratia: sintetizator respiratie cu respiratie (`src/audio/breath_synth.*`): zgomot roz prin formanti (nas in repaus, gura la efort), sasait la buze, filtrul microfonului de pe piept, "huh" glotal la epuizare, tremur si inspir sacadat la stres. Fiziologia are acum pauza dupa expir (lunga in repaus, aproape disparuta la efort).
- Bug: sunetele `tablet_notify` / `tablet_confirm` contineau NaN (`exp(-0/0)`), care otravea reverberatia si amutea tot jocul dupa prima notificare. Reparat + plasa de siguranta in mixer.
- `--record-audio out.wav` inregistreaza mixajul final (diagnostic).

**Imagine**
- Rezolutie implicita 960x540, compresie video oprita implicit, zgomot / ghosting / distorsiune reduse.
- Profunzimea de camp doar cu tableta ridicata (obiectiv larg cu focus fix: altfel totul clar).
- Tableta: aproape tot ecranul; textura UI esantionata "sharp bilinear" (litere nete si egale); notificarea pe banda opaca.
- Performanta (960x540): viewmodel 22 ms -> 2 ms (rasterizare in benzi paralele, ecranul tabletei in paralel), postfx ~16 -> ~7 ms (tone mapping tabelat, un singur hash pentru zgomot, esantionare pe 3 canale deodata). Smoke test: 20 -> ~52 FPS.

**Teste**: 221 verificari (respiratii distincte, liniste in pauze, spectru; ocluzie beton/lemn, difractie prin usa, usi inchise in zid gros).

## Simulare si modelare: harti, statistici, poligon, experimente

### Fluxul datelor
```
sisteme de simulare (arma, balistica, daune, fiziologie, poligon, runde)
   -> evenimente (SHOT, HIT, KILL, DOWNED, RELOAD_*, DETECT, NEAR_MISS, TARGET_HIT, RANGE, ROUND_END ...)
   -> StatisticsSystem (src/stats/statistics_system.*): inregistrari + esantioane la 0.5 s
   -> DataStorage (CSV in data/, scris doar la sfarsit de meci / sesiune)
   -> analytics.* (functii pure: medii, IC95, regresie + p, Wilson, Welch, pereche)
   -> tableta (pagina STATISTICI) - doar citeste
```
- Fiecare foc are un numar (`Event::shot`): loviturile, uciderile si impacturile pe tinte se leaga de focul care le-a produs; un foc e "ratat" abia cand glontul lui nu mai zboara.
- Meci = runde pana la 3 victorii (`assets/config/stats.ini`); la final se salveaza meciul, rundele, focurile, reincarcarile, daunele primite, angajamentele si seriile de timp.
- O eliminare = inamic ucis sau incapacitat de jucator (o singura data pe inamic).
- Capturile, smoke test-ul si testele NU scriu in `data/` (doar citire / directoare temporare).

### Fisierele din data/
| fisier | o linie = | coloane importante |
|---|---|---|
| matches.csv | un meci | harta, rezultat (W/L/D/A), ucideri, morti, asistente, focuri, lovite, dauna, puls/stres/stamina/oboseala (medie, min, max), distante, timpi, angajamente, reactie, daune pe zone |
| rounds.csv | o runda | castigator, durata, ucideri, focuri, puls, stres |
| shots.csv | un foc | context (meci / mod de poligon), arma, lovit, zona, dauna, distanta, abatere, puls, stres, stamina, oboseala, tremur, rana la brat, sprint recent |
| reloads.csv | o reincarcare | durata, intrerupta, puls, stres, oboseala, motricitate |
| damage.csv | o lovitura primita | zona corpului, dauna, sursa, distanta |
| engagements.csv | un angajament | durata, distante, focuri, lovite, ucideri, timp de reactie |
| samples.csv | un esantion (0.5 s) | puls, stres, stamina, oboseala, efort, tremur, viteza, sub foc, trage, sprint |
| range_sessions.csv | o sesiune de poligon | mod, arma, distanta, precizie, cap/corp, reactie (medie, mediana, SD), timp total, precizie pe distante, repaus vs dupa sprint |
| experiments.csv | o proba experimentala | experiment, factor, nivel, replicare, samanta, precizie, abatere, instabilitate, tremur, reincarcare, puls |

### Hartile (assets/maps/catalog.ini, generate de scripts/gen_maps.py)
| MapId | harta | caracter | distanta posibila medie (masurata) |
|---|---|---|---|
| 1 | URBAN | strazi, blocuri cu holuri, magazine, masini | ~17 m |
| 2 | INDUSTRIAL | hale cu rafturi, curte de containere | ~20 m |
| 3 | REZIDENTIAL | case cu camere mici, gradini cu garduri | ~16.5 m |
| 4 | POLIGON | culoare 10/25/50/100 m, tinte fixe / mobile / reactive | - |
| 5 | TACTIC MARE | sat, complex industrial, camp, cladire fortificata, 4 obiective | ~37 m |
| 6 | COMPLEX | harta initiala | - |

Fiecare harta este masurata la pornire: dimensiune, suprafata accesibila, camere, usi, obiective, inamici,
vizibilitate (fractia perechilor de puncte cu linie de vedere) si distributia distantelor de angajare posibile.
Distanta de angajare REALA se masoara separat, din joc. Pornire: `bodycam_app --map urban`, sau tableta -> MECI -> MECI NOU PE.

### Poligonul (tableta -> POLIGON)
Moduri: LIBER, PRECIZIE (N focuri), REACTIE (tinta aleatoare, timp aparitie -> foc), VITEZA (6 tinte, cronometru),
DISTANTA (aceeasi arma la 10/25/50/100 m), STRES (tragere in repaus -> sprint real pana la puls +45 BPM si stamina <= 45% -> tragere).
Tinte din otel cu zona cap / corp, urmele reale ale impacturilor, sunet de otel. Arma se schimba direct din pagina.

### Experimente controlate (tableta -> STATISTICI -> EXPERIMENTE, sau `bodycam_exp --experiment <cheie|all>`)
Un tragator sintetic foloseste exact modelul jucatorului; un singur factor variaza, restul e fix, replicarile folosesc
numere aleatoare comune (CRN). Exemple de rezultate (6 replicari):
- stres 0 -> 1: precizia scade (r = -0.62, p < 0.001)
- puls 70 -> 180: instabilitatea armei creste (r = 0.96)
- oboseala 0 -> 1: reincarcarea 2.2 s -> 2.8 s (r = 0.99)
- rana la brat 0 -> 0.9: instabilitatea creste de ~2.8 ori
- distanta 10 -> 100 m: precizia scade (r = -0.90)

### Intrebarile proiectului si unde se gasesc raspunsurile
| intrebare | raspuns |
|---|---|
| Stresul scade precizia? | PERFORMANTA -> STRES -> PRECIZIE (focurile reale) + experimentul stres_precizie |
| Pulsul creste instabilitatea armei? | experimentul puls_stabilitate; PERFORMANTA -> PULS -> PRECIZIE |
| Sprintul inainte de tragere reduce precizia? | PERFORMANTA -> SPRINT RECENT; proba STRES din poligon (comparatie pereche); experimentul sprint_precizie |
| Ce harta produce cel mai mult stres / are cea mai buna rata de victorie? | HARTI (tabel + grafic + comparatie cu test Welch) |
| Ce arma e cea mai precisa? | ARME (IC95 Wilson) + experimentul arme_precizie |
| Distanta influenteaza precizia? | PERFORMANTA -> DISTANTA; poligon DISTANTA; experimentul distanta_precizie |
| Oboseala creste timpul de reincarcare? | PERFORMANTA -> OBOSEALA -> REINCARCARE; experimentul oboseala_reincarcare |
| Rana la brat creste instabilitatea? | PERFORMANTA -> BRAT RANIT; experimentul brat_stabilitate |
| Stresul creste timpul de reactie? | PERFORMANTA -> STRES -> REACTIE (angajamente + poligon REACTIE) |

Fiecare grafic afiseaza N, intervalul de incredere si concluzia testului ("semnificativ", "fara efect" sau "date insuficiente").

**Teste**: 337 verificari (catalog si runde pe toate hartile; zone cap/corp/ratat; sesiunile reactie, stres, viteza;
meci real -> inregistrari -> CSV -> citire de o instanta noua; functiile statistice; migrarea CSV; oboseala; experimente).

## Arme, atasamente, personaje, maini, masina RC, drona, materiale

### Arme (date + calcul centralizat)
- O arma = un fisier `assets/weapons/<id>.ini`: model (pusca, pistol-mitraliera, pistol, pusca cu pompa, pusca cu luneta),
  moduri de foc (`fire_modes = semi, burst, auto, bolt`), recul (primul foc, acumulare, resetare, spate / rotatie / corp),
  puncte de prindere `[sockets]` (maner, mana stanga, incarcator, inchizator, maner de armare, fereastra, gura tevii, optica,
  tragaci) si forma `[visual]`. Arma noua: M24 (repetitie, luneta 4x, inchizator 0.95 s).
- Calculul e intr-un singur loc (`src/sim/weapon_stats.*`, coeficientii in `assets/config/handling.ini`):
  dispersia = (baza + miscare + caldura + recul) x postura x stres x oboseala x puls x brat ranit; reculul si revenirea la fel.
- Selectorul (tasta N): AUTOMAT / SEMI / RAFALA dupa arma; clic de selector, parghia de pe arma arata modul (fara text pe ecran).
- Repetitie: dupa fiecare foc manerul se ridica, inchizatorul se trage si se impinge (animat), cu sunete separate.
- Botii folosesc acelasi sistem: gloante reale, incarcatoare, reincarcare din rezerva, inchizator, rafale scurte.

### Atasamente (`assets/config/attachments.ini`)
8 tipuri / locasuri (OPTICA, GURA TEVII, SUB TEAVA, INCARCATOR, PAT, LANTERNA, LASER, TEAVA), un atasament pe locas,
compatibilitate pe model. 17 atasamente: red dot, holografic, luneta 4x, amortizor, compensator, frana de gura, parascantei,
maner vertical, maner inclinat, lanterna, laser, incarcator marit, toba, pat greu, pat usor, teava lunga, teava scurta.
Fiecare modifica valori (dispersie, recul, revenire, miscare, viteza glontului, stabilitate, timp de ochire, inaltimea
opticii, marire, distanta ochiului) si forma armei; manerele muta mana stanga, tevile muta gura tevii.
Ochirea se aliniaza pe optica montata; luneta are imagine marita reala (a doua randare a scenei) cu reticul.

### Personaje (`assets/config/characters.ini`)
- Profile: culori (piele, bluza, pantaloni, vesta, casca, manusi, bocanci, rucsac), casca (balistica / sapca / caciula /
  fara), vesta (port-placi / chest rig / fara), manusi (intregi / fara degete / fara), rucsac, ceas, barba, ochelari,
  inaltime, latime, arma + atasamente, arma secundara, indemanare.
- Personajele poarta arma reala (acelasi constructor de plasa ca la jucator, cu atasamente); mainile stau pe punctele de
  prindere ale armei (si culcat, si la alte inaltimi). Fata: ochi, nas, urechi, gura.
- Mainile din prim-plan vin din profilul jucatorului (maneci, manusi, ceas, bandaj cand bratul e ranit).

### Materiale
13 materiale (polimer, otel, aluminiu, cauciuc, sticla, vopsea, textil, piele, piele naturala, lemn, plastic, alama):
luciu, stralucire, metalic, contur - folosite la arma din prim-plan, la personaje si la gadgeturi.

### Masina RC si drona (`assets/config/gadgets.ini`)
- Toate valorile din fisier: viteza, acceleratie, franare, mers inapoi, viraj, ampatament, aderenta, suspensie (arc +
  amortizor), roti, baterie (minute, consum la ralanti, celule), camera (inaltime, inclinare, FOV, rezolutie), structura,
  pragul si dauna la impact, dauna la glont, fum.
- Suspensia: botul se ridica la accelerare, se lasa la franare, caroseria se inclina in viraje, se infige la impact.
- Modele 3D (nu mai sunt sprite-uri): masina - 4 roti cu crampoane si jante care se rotesc, rotile din fata vireaza,
  brate + amortizoare, cusca, camera pe catarg, antena care se apleaca, faruri / stopuri (franare), incarcatura;
  drona - cadru X din carbon, motoare, elice cu 3 pale care se rotesc in sensuri opuse, camera inclinata, baterie,
  antena video, LED-uri. Avariat: fum; distrus: carbonizat, roata / brat lipsa, fara lumini.
- Tableta (pilotare): telemetrie din simulare - drona: mod, gaz, tensiune 4S, structura, busola, orizont artificial
  aliniat cu orizontul real, altitudine, viteza verticala si orizontala, distanta; masina: treapta, tractiune, km/h,
  indicator de viraj, tangaj / ruliu, odometru, distanta, incarcatura, baterie scazuta.

### Resurse externe (`ExternalAssets/`)
- `manifest.ini` + verificare in cod (`src/core/asset_manifest.cpp`): un fisier se incarca doar cu sursa, licenta permisa
  (CC0 / CC-BY cu autor / MIT / Apache / BSD), aprobarea utilizatorului si fisierul pe disc. NC, ND, editorial,
  necunoscut, extras din jocuri: refuzat (jocul ramane pe modelul procedural).
- Incarcator OBJ + MTL (`src/render/obj_model.*`); o arma poate folosi un model extern (`[visual] model = ...`),
  atasamentele si mainile raman pe punctele de prindere.

### Audit: IMPLEMENTAT vs EXISTA DAR NU E INTEGRAT
| element | stare |
|---|---|
| cadrul de arme, moduri de foc, recul, dispersie, calcul centralizat | IMPLEMENTAT (jucator + boti) |
| atasamente pe locasuri cu modificatori si forma | IMPLEMENTAT |
| puncte de prindere pentru IK (prim-plan + personaje) | IMPLEMENTAT |
| ochire dupa optica, luneta cu imagine marita | IMPLEMENTAT |
| maini din profilul personajului | IMPLEMENTAT |
| profile de personaje + aspect + arma reala la boti | IMPLEMENTAT |
| materiale | IMPLEMENTAT |
| masina RC + drona: date, fizica, modele 3D, avarii, telemetrie | IMPLEMENTAT |
| verificarea licentei + incarcator OBJ/MTL + modele externe pe arme | IMPLEMENTAT (vezi mai jos: pachetul Quaternius CC0 pe cele 5 arme) |
| modele externe pentru personaje | NU EXISTA: motorul nu are piele pe schelet (skinning); personajele raman procedurale |
| texturi din MTL (map_Kd) | NU EXISTA: doar culoarea materialului |
| gadgeturi folosite de boti | NU EXISTA |
| parghia selectorului pe pistol / pusca cu pompa / repetitie | NU EXISTA (doar AR / pistol-mitraliera) |

**Teste**: 486 verificari (atasamente, dispersie / recul, profiluri de recul, moduri de foc, botii cu gloante reale si
inchizator, gadgeturi din fisier: suspensie / roti / baterie / impact / drona, mainile pe punctele de prindere la
inaltimi diferite, licente refuzate / acceptate, OBJ/MTL, arma cu model extern si cu model refuzat).

## Stres, adrenalina, tremur: refacerea modelului fiziologic

### De ce tremura personajul prea tare (cauzele gasite in codul vechi)
| cauza | unde | efect |
|---|---|---|
| stresul si adrenalina sareau instant la fiecare stimul (fara tinta, fara intarziere) | `physiology_stimulus` | o explozie = +0.55 stres si +0.5 adrenalina in acelasi cadru |
| adrenalina era practic o copie a stresului (castig 0.9 pe acelasi impuls) | `physiology_stimulus` | nicio dinamica proprie, nicio descarcare |
| tremurul aduna 9 termeni fara plafon | `update_physiology` | pana la ~0.022 rad (1.2 grade) la arma |
| benzi rapide pana la ~15 Hz (10.5 Hz x 1.4 cu adrenalina) | tremor_yaw / pitch | vibratie, nu tremur fiziologic |
| unghiul de tremur folosit ca deplasare a mainilor (x2) | viewmodel `tr = tremor_amp * 2` | pana la ~4 cm de agitatie in prim-plan |
| cel putin 7 surse de zgomot independente (arma, maini, tableta, camera, leganare, tresariri, pasi) | viewmodel, body_camera, player_sim | miscari necorelate = haos |
| camera primea stresul direct (instabilitate + "priviri involuntare" 0.05 rad + bataile inimii) | body_camera | ecranul se scutura cu stresul |
| leganarea armei se inmultea inca o data cu stresul (x2.5) | `update_weapon_dynamics` | dublare peste tremur |
| revenire fara context (lupta / pericol / zona sigura) si fara memorie | `update_physiology` | stres blocat sau resetat brusc |

### Arhitectura noua (`src/sim/physiology.*`, totul in `assets/config/physiology.ini`)
```
stimul -> impuls pe tip de eveniment (stres, adrenalina, pericol, obisnuire)
  -> memoria stresului (se aduna, cu saturare si obisnuire la stimuli repetati)
  -> stres tinta = 1 - exp(-raspuns x (memorie + surse persistente))   [platou, nu prag]
  -> stres curent: doua trepte de intarziere (activare ~0.35 s, crestere ~1.2 s)
  -> adrenalina: curba proprie fata de stres + valuri de la evenimente bruste, eliberare intarziata,
     scadere lenta (~40 s), mai lenta in lupta; varful episodului este retinut
  -> puls (tinta din efort + curbe stres / adrenalina + rani + descarcare) -> respiratie (frecventa,
     adancime, neregularitate) -> tensiune musculara -> motricitate fina
  -> lantul corpului: trunchi -> umeri -> brate -> maini -> arma (aceleasi surse, intarzieri mici,
     fiecare nivel adauga doar putin); camera de pe piept primeste ~8% din trunchi, nimic din maini
```
- Tremurul are trei benzi cu contributii marginite (saturare lina): lenta (deriva, oboseala, puls,
  descarcare), medie ~4-6 Hz (tensiune, brat ranit, apnee), rapida ~9-10 Hz (adrenalina, atenuata de masa armei).
- Revenirea depinde de context: LUPTA (aproape deloc), PERICOL APROAPE (lenta), PERICOL TRECUT (moderata),
  ZONA SIGURA (rapida); dupa lupta lunga revenirea e mai lenta; inima care bate tare intarzie calmarea;
  respiratia tactica o accelereaza (si in lupta).
- Descarcarea de adrenalina: porneste la ~8 s dupa ultimul pericol daca varful de adrenalina a trecut de 0.45;
  dureaza 25-90 s (mai mult dupa lupta lunga); intensitatea creste pe masura ce adrenalina scade si se stinge
  la final. Efecte: oboseala resimtita, respiratie grea, puls ridicat, refacerea staminei mai lenta,
  motricitate fina mai slaba, leganare lenta - fara tremur violent (arma nu devine mai instabila decat in lupta).
- Oboseala resimtita: adrenalina o ascunde in lupta, descarcarea o scoate la iveala.
- Starea fiziologica: CALM, ALERT, STRESAT, STRES MARE, STRES EXTREM, VARF DE ADRENALINA, PERICOL INDEPARTAT,
  DESCARCARE DE ADRENALINA, REVENIRE (dedesubt totul este continuu; benzile de stres au histerezis).
- Evenimente cu impulsuri proprii: sunet mic, foc departe, foc aproape, glont pe langa, impact aproape,
  inamic aparut, rana usoara, rana grava, explozie, coechipier cazut, blocaj, foc propriu, eveniment brusc.
  Focul echipei conteaza mai putin (x0.35); amortizorul micsoreaza raza.
- Panoul fiziologiei: F11 in joc (`--physdebug` la capturi) - valoare curenta / tinta pentru stres,
  adrenalina, puls, respiratie, tensiune, plus memorie, val de adrenalina, varf, descarcare, tremur pe benzi,
  miscarea camerei, stabilitatea armei, leganare, motricitate, recul perceput, tunel, starea si contextul.
- Cronologie pentru grafice: `bodycam_exp --physio-trace fisier.csv`.

### Cele 8 scenarii (teste automate, valori reale)
| test | rezultat |
|---|---|
| 1 repaus | stres 0, adrenalina 0, puls 68, tremur 0.38 mrad, stabilitate 0.94, CALM |
| 2 un foc aproape | stresul nu sare (dupa un cadru < 0.01), varf 0.12, adrenalina 0.06, tremur +0.05 mrad |
| 3 mai multe focuri | stres 0.06 -> 0.16 -> 0.32 -> 0.42 -> 0.50, adrenalina 0.35, puls 69 -> 90, respiratie 13 -> 17 |
| 4 rana | stres +0.21, adrenalina 0.22, puls 69 -> 83, tremur 0.38 -> 0.77 mrad |
| 5 lupta 3 minute | stres 0.78 -> 0.91, adrenalina 0.65 -> 0.98, puls 155, oboseala creste, stabilitate 0.60 -> 0.49 |
| 6 lupta se termina | stresul ramane, descarcarea porneste la 8 s, la varf: respiratie 25/min, oboseala resimtita 0.20 |
| 7 perioada sigura | adrenalina 0.56 -> 0.31 -> 0.16 -> 0.09, puls 120 -> 102 -> 82 -> 74, stabilitatea revine la 0.92 |
| 8 a doua lupta | porneste de la stres 0.90 / puls 145; dupa 20 s: 0.91 / 152 fata de 0.70 / 126 de la zero |
| stres maxim | tremurul armei max 2.35 mrad (0.13 grade), camera ~7x mai putin, motricitate 0.71, salt pe cadru < 0.3 mrad |

Experimentul controlat STRES -> PRECIZIE ramane semnificativ (73% -> 42%, p = 0.004): stresul ingreuneaza, nu blocheaza.

**Teste**: 522 verificari.

### Modele externe pe arme (pachetul Quaternius, CC0)
- Descarcat cu aprobarea utilizatorului: `Ultimate Gun Pack by Quaternius.zip` (7 MB) de pe
  https://quaternius.itch.io/50-lowpoly-guns; licenta CC0 1.0 confirmata in descrierea autorului si in `License.txt`.
  Pastrate doar OBJ + MTL + licenta in `ExternalAssets/Weapons/Quaternius_UltimateGuns/` (intrare in `manifest.ini`, cu sha256).
- MK18 <- AssaultRifle2_2, MP5 <- SubmachineGun_5, G17 <- Pistol_4, M870 <- Shotgun_1, M24 <- SniperRifle_2.
- Fiecare arma: `[visual] model`, scala (lungimea reala a armei), ancora (punctul de deasupra manerului),
  punctele de prindere `[sockets]` masurate pe model, si piesele modelului separate dupa piesele conectate:
  incarcatorul (animat la reincarcare), patul (ascuns la ochire), catarea din spate / luneta proprie (ascunse
  cand e montata optica noastra), pompa (M870), inchizatorul (G17), manerul inchizatorului (M24).
- Atasamentele (red dot, luneta 4x cu imagine marita, amortizor, lanterna, manere) se monteaza pe aceleasi
  puncte; dispozitivul de la gura tevii se adauga doar cand e montat ca atasament.
- Culorile MTL sunt liniare (export Blender). Modelul procedural ramane ca rezerva: daca fisierul lipseste
  sau licenta nu trece verificarea, jocul foloseste automat constructorul procedural.
- Capturi de comparatie: `--procedural` (armele fara modele externe), `--reload-at N`.

### Completari dupa auditul final (specificatia de arme / personaje / gadgeturi)
- Botii nu mai primeau incarcatoare din nimic: cand arma principala ramane fara rezerve trec pe arma
  secundara (timpii de punere in toc / scoatere ai armelor), iar fara nicio munitie nu mai trag.
- Reincarcarea depinde si de postura (culcat +18%, ghemuit +3%); pragurile stilurilor urmeaza benzile
  de stres: PANICA doar la supraincarcare (> 0.93), lupta grea obisnuita = reincarcare rapida;
  incarcatorul nefixat ramane rar (~9% la stres foarte mare).
- Masina RC si drona: sensibilitatea comenzilor si curba expo in `gadgets.ini`.

**Teste**: 535 verificari.

## Arma in prim-plan, accesoriile din pachet, campania pe harti, pagina STAMINA

### Arma in prim-plan (ce era gresit si ce s-a schimbat)
| problema | cauza | rezolvare |
|---|---|---|
| arma mica si departe la sold | camera are 110 grade; arma era desenata cu acelasi camp vizual | camp vizual separat pentru arma si maini (`CAMP VIZUAL ARMA`, implicit 74); picioarele / tableta / reincarcarea folosesc campul camerei |
| la ochire, un bloc negru in centru | ochiul la doar 2 cm deasupra corpului armei; sticla red dot-ului desenata opac | linia de ochire la inaltimea unui montaj real (red dot 3.8 cm, holografic 3.4 cm), tub deschis cu margini vizibile, sticla nu se mai deseneaza la ochire |
| mana dreapta uriasa la ochire (M870, M24) | manerul patului e chiar sub ochi; policele urca in linia de ochire | policele trece pe stanga; la ochire mana si incheietura coboara pe maner (`ads_hand_drop`, `ads_wrist_drop`) |
| mana stanga "in cupa" sub maner | prindere de garda folosita si pentru manerul vertical | prindere proprie pentru manerul vertical; manerul se prinde sub garda reala a modelului |
| catarea din fata in mijlocul opticii | piesa lipita de teava in model | piese clasificate pe triunghi (`hide_optic@`), catarea se ascunde cand e montata optica |
| pistolul prea aproape | 33 cm de ochi | bratele intinse, ~55 cm |
- Toate pozitiile sunt in `assets/config/viewmodel.ini` (sold, ochire, umeri, coate, camp vizual la ochire), pe tip de arma.
- Capturi: `--vm-orbit <grade>` (vedere laterala de depanare), `--wfov <grade>`; capturile citesc acum `settings.ini` (doar citire).

### Accesoriile din pachetul Quaternius (CC0)
- Atasamentele au `model / model_scale / model_anchor` in `attachments.ini` (licenta verificata la incarcare, altfel forma procedurala):
  luneta 4x (Scope_1), amortizor (Silencer_2; pistol: Silencer_1), lanterna (Flashlight), maner vertical (Grip), bipied (Bipod, nou).
- Bipiedul (M24, pusti): strans de-a lungul tevii, desfacut culcat; efect real doar culcat (`handling.ini [bipod]`: dispersie x0.75, recul x0.6, leganare x0.35).

### Campania pe harti (`bodycam_exp --campaign N --data <dir>`)
- Operatorul (agentul 0) e condus de AI-ul botilor, cu aceeasi fiziologie si acelasi sistem de statistici; meciurile se salveaza ca cele jucate.
- Echipe egale ca numar (cati inamici are harta). Coechipierii AI alearga spre zgomot / obiectiv (stamina si oboseala cu aceiasi parametri ca la jucator)
  si patrula lor trece si prin pozitiile pazite de inamici; un punct de patrula inaccesibil se sare dupa 45 s.
- Setul generat: `results/campanie/` (100 de meciuri + toate experimentele + cronologia fiziologiei). `bodycam_app --data results/campanie` il arata pe tableta.

### Statistici
- Pagina noua STAMINA: stamina si oboseala in timp, momentele de alergare, consum, refacere pe secunda, revenirea 40% -> 90%, medii pe meciurile salvate.
- STRES: timpul in cele 6 niveluri ale modelului (aceleasi praguri ca `physiology.ini`) + curba adrenalinei.
- Fara prescurtari: OBOS, DIST, ANGAJ., REINC, PREC, SD, MED, K+/K-, IC95, MRAD, ALT, VZ inlocuite cu cuvinte intregi.
- Experimentul PRECIZIA ARMELOR include si M24.

**Teste**: 567 verificari.
