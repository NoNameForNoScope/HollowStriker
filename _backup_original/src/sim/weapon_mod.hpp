#ifndef BODYCAM_WEAPON_MOD_HPP
#define BODYCAM_WEAPON_MOD_HPP

#include <string>
#include <vector>

namespace bodycam {
enum class AttachmentType { Optic, Barrel, Magazine, Stock };

struct WeaponAttachment {
    std::string name;
    AttachmentType type;
    double recoilModifier;
};

struct ModularWeapon {
    std::string modelName = "MK18 Tactic";
    double baseRecoil = 1.0;
    int maxAmmo = 30;
    int currentAmmo = 30;
    std::vector<WeaponAttachment> attachments;

    double getTotalRecoil() const {
        double rec = baseRecoil;
        for (const auto& att : attachments) rec *= att.recoilModifier;
        return rec;
    }
};
}
#endif
