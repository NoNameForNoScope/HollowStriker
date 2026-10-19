#ifndef BODYCAM_WEAPON_MOD_HPP
#define BODYCAM_WEAPON_MOD_HPP

#include <string>
#include <vector>

namespace bodycam {

// Locasurile unei arme. Fiecare locas primeste cel mult un atasament.
// (Ordinea e folosita pe tableta; tipurile noi se adauga la final.)
enum class AttachmentType { Optic = 0, Muzzle, Underbarrel, Magazine, Stock, Side, Laser, Barrel, Count };

// Forma vizuala a unui atasament: sistemul de desen al armei stie sa construiasca fiecare
// forma; un atasament nou din INI alege una dintre ele (nu exista cod pe nume de atasament).
enum class AttachmentVisual {
    None = 0, RedDot, Holo, Scope, Suppressor, Compensator, Brake, FlashHider,
    VerticalGrip, AngledGrip, ExtMag, Drum, HeavyStock, LightStock, Light, Laser, LongBarrel, ShortBarrel, Bipod, Count
};
AttachmentVisual attachment_visual_from(const std::string& s);
const char* attachment_visual_name(AttachmentVisual v);

// Un atasament modifica datele armei multiplicativ (sau aditiv: greutate, lungime,
// incarcator). Catalogul vine din assets/config/attachments.ini; cel de mai jos
// e doar rezerva daca fisierul lipseste. Modificatorii neimpliciti sunt doar cei relevanti.
struct WeaponAttachment {
    std::string name;
    AttachmentType type = AttachmentType::Optic;
    double recoilModifier = 1.0;   // vertical
    double soundModifier = 1.0;    // raza de auz a focului
    double flashModifier = 1.0;    // intensitatea flamei de la gura tevii
    double adsModifier = 1.0;      // timpul de ochire
    double swayModifier = 1.0;
    double weightAdd = 0.0;
    int magBonus = 0;
    // extinse (din INI)
    std::string label;
    std::string desc;
    std::vector<std::string> fits;     // modele compatibile: rifle, smg, pistol, shotgun, sniper
    double recoilH = -1.0;             // orizontal (< 0 = la fel ca vertical)
    double spread = 1.0;
    double kick = 1.0;                 // lovitura camerei
    double switchTime = 1.0;           // scoatere / punere in toc
    double reloadTime = 1.0;
    double lengthAdd = 0.0;
    AttachmentVisual visual = AttachmentVisual::None;
    double moveModifier = 1.0;         // viteza de deplasare
    double velocityModifier = 1.0;     // viteza la gura tevii (teava)
    double stability = 1.0;            // revenirea dupa recul (> 1 = mai rapida)
    double sightHeight = 0.0;          // m: cat de sus e linia de ochire peste sina (optica)
    double magnification = 1.0;        // marirea opticii
    double eyeRelief = 0.0;            // m: ochiul sta mai in spate (luneta)
    // Model extern (ExternalAssets, licenta verificata la incarcare). Gol = forma procedurala.
    // Axele modelului: +x spre gura tevii, +y in sus. model_anchor = punctul modelului pus pe locas.
    std::string model;
    std::string model_pistol;          // varianta pentru pistol (ex. amortizor scurt), daca exista
    double model_pistol_scale = 0.0;   // scala variantei pentru pistol (0 = aceeasi)
    double model_scale = 1.0;
    double model_anchor[3] = {0.0, 0.0, 0.0};

    bool fits_model(const std::string& m) const {
        if (fits.empty()) return true;
        for (const auto& f : fits) if (f == m) return true;
        return false;
    }
};

const char* attachment_type_name(AttachmentType t);

// Catalogul de rezerva (folosit doar daca assets/config/attachments.ini lipseste)
std::vector<WeaponAttachment> attachment_catalog();

// Incarca assets/config/attachments.ini (o sectiune pe atasament). Intoarce rezerva daca lipseste.
std::vector<WeaponAttachment> load_attachment_db(const std::string& path);

}
#endif
