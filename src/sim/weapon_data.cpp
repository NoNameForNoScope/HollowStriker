#include "weapon_data.hpp"
#include <filesystem>
#include "weapon_mod.hpp"
#include "../core/ini.hpp"
#include <sstream>

namespace bodycam {

namespace {
double get_d(const IniParser& ini, const char* key, double def) {
    std::string v = ini.get("weapon", key, "");
    if (v.empty()) return def;
    try { return std::stod(v); } catch (...) { return def; }
}
int get_i(const IniParser& ini, const char* key, int def) {
    std::string v = ini.get("weapon", key, "");
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}
bool get_b(const IniParser& ini, const char* key, bool def) {
    std::string v = ini.get("weapon", key, "");
    if (v.empty()) return def;
    return v == "true" || v == "1" || v == "yes";
}
}

void WeaponData::derive() {
    muzzle_energy = 0.5 * bullet_mass * muzzle_velocity * muzzle_velocity * pellets;
}

const char* fire_mode_name(FireMode m) {
    switch (m) {
        case FireMode::Semi: return "SEMI";
        case FireMode::Burst: return "RAFALA";
        case FireMode::Auto: return "AUTOMAT";
        case FireMode::Bolt: return "REPETITIE";
    }
    return "?";
}

void default_layout(WeaponModel m, WeaponSockets& s, WeaponVisual& v) {
    s = WeaponSockets{};
    v = WeaponVisual{};
    switch (m) {
        case WeaponModel::Smg:
            v.handguard_z = 0.26; v.handguard_len = 0.08; v.stock = "smg"; v.mag_tilt = 0.05;
            s.support = {-0.01, 0.075, 0.28};
            s.muzzle = {0, 0.042, 0.41};
            break;
        case WeaponModel::Pistol:
            v.receiver = "pistol"; v.stock = "none"; v.mag_shape = "pistol"; v.mag_tilt = -0.25; v.barrel_len = 0.0;
            s.grip = {0, 0.1, 0.0}; s.grip_tilt = -0.25;
            s.support = {0.0, 0.1, 0.0};
            s.mag = {0, 0.11, 0.0};
            s.port = {0.015, 0.02, 0.1};
            s.muzzle = {0, 0.028, 0.2};
            s.optic = {0, 0.014, 0.08};
            s.bolt = {0, 0.028, 0.02};
            s.charging = {0, 0.028, 0.02};
            break;
        case WeaponModel::Shotgun:
            v.receiver = "pump"; v.stock = "fixed"; v.mag_shape = "internal"; v.handguard_z = 0.34; v.handguard_len = 0.07;
            s.support = {-0.01, 0.075, 0.34};
            s.muzzle = {0, 0.04, 0.66};
            s.port = {0.02, 0.05, 0.06};
            s.optic = {0, 0.034, 0.06};
            break;
        case WeaponModel::Sniper:
            v.receiver = "bolt"; v.stock = "fixed"; v.mag_shape = "straight"; v.mag_tilt = 0.0;
            v.handguard_z = 0.36; v.handguard_len = 0.16; v.barrel_len = 0.32;
            s.grip = {0, 0.11, 0.0}; s.grip_tilt = -0.38;
            s.support = {-0.01, 0.07, 0.38};
            s.mag = {0, 0.095, 0.12};
            s.bolt = {0.034, 0.03, 0.05};
            s.charging = {0.034, 0.03, 0.05};
            s.port = {0.02, 0.028, 0.1};
            s.muzzle = {0, 0.035, 0.84};
            s.optic = {0, 0.006, 0.1};
            break;
        default:
            s.support = {-0.01, 0.075, 0.35};
            s.muzzle = {0, 0.042, 0.53};
            break;
    }
}

bool WeaponData::load(const std::string& path, const std::vector<WeaponAttachment>* catalog, bool apply) {
    IniParser ini;
    if (!ini.load(path)) return false;
    id = ini.get("weapon", "id", id);
    name = ini.get("weapon", "name", name);
    caliber = ini.get("weapon", "caliber", caliber);
    std::string m = ini.get("weapon", "model", "rifle");
    model = (m == "smg") ? WeaponModel::Smg : (m == "pistol") ? WeaponModel::Pistol
          : (m == "shotgun") ? WeaponModel::Shotgun : (m == "sniper") ? WeaponModel::Sniper : WeaponModel::Rifle;
    sound_profile = ini.get("weapon", "sound_profile", m);

    struct { const char* k; double* d; } dk[] = {
        {"rpm", &rpm}, {"bullet_mass", &bullet_mass}, {"muzzle_velocity", &muzzle_velocity},
        {"drag_cd", &drag_cd}, {"bullet_area", &bullet_area}, {"penetration_mult", &penetration_mult},
        {"damage", &damage}, {"range", &range}, {"falloff_start", &falloff_start}, {"falloff_end", &falloff_end},
        {"falloff_min", &falloff_min}, {"spread_hip", &spread_hip}, {"spread_ads", &spread_ads},
        {"spread_move", &spread_move}, {"spread_heat", &spread_heat}, {"pellet_spread", &pellet_spread},
        {"recoil_pitch", &recoil_pitch}, {"recoil_yaw", &recoil_yaw}, {"recoil_recovery", &recoil_recovery},
        {"recoil_permanent", &recoil_permanent}, {"camera_kick", &camera_kick}, {"recoil_pattern", &recoil_pattern},
        {"recoil_first", &recoil_first}, {"recoil_accum", &recoil_accum}, {"recoil_reset", &recoil_reset},
        {"recoil_back", &recoil_back}, {"recoil_rot", &recoil_rot}, {"recoil_body", &recoil_body},
        {"weight", &weight}, {"length", &length}, {"ads_time", &ads_time}, {"draw_time", &draw_time},
        {"holster_time", &holster_time}, {"sway_scale", &sway_scale}, {"movement_penalty", &movement_penalty},
        {"stamina_penalty", &stamina_penalty}, {"reload_tactical", &reload_tactical}, {"reload_empty", &reload_empty},
        {"shell_time", &shell_time}, {"clear_time", &clear_time}, {"mag_weight", &mag_weight},
        {"mag_weight_empty", &mag_weight_empty}, {"heat_per_shot", &heat_per_shot}, {"cool_rate", &cool_rate},
        {"smoke_heat", &smoke_heat}, {"glow_heat", &glow_heat}, {"malfunction_base", &malfunction_base},
        {"malfunction_heat", &malfunction_heat}, {"sound_radius", &sound_radius}, {"loudness", &loudness},
        {"flash_intensity", &flash_intensity}, {"muzzle_smoke", &muzzle_smoke}, {"eject_speed", &eject_speed},
        {"bolt_time", &bolt_time},
    };
    for (auto& e : dk) *e.d = get_d(ini, e.k, *e.d);
    mag_size = get_i(ini, "mag_size", mag_size);
    pellets = get_i(ini, "pellets", pellets);
    reserve_mags = get_i(ini, "reserve_mags", reserve_mags);
    burst_count = get_i(ini, "burst_count", burst_count);
    recoil_accum_max = get_i(ini, "recoil_accum_max", recoil_accum_max);
    casing = get_i(ini, "casing", model == WeaponModel::Pistol ? 1 : (model == WeaponModel::Shotgun ? 2 : 0));
    automatic = get_b(ini, "automatic", automatic);
    shell_reload = get_b(ini, "shell_reload", shell_reload);
    open_bolt_lock = get_b(ini, "bolt_lock", !shell_reload);
    projectile = get_b(ini, "projectile", projectile);
    // compatibilitate: draw/holster din switch_time
    double sw = get_d(ini, "switch_time", -1.0);
    if (sw > 0.0 && ini.get("weapon", "draw_time", "").empty()) { draw_time = sw * 0.55; holster_time = sw * 0.45; }

    auto split = [](const std::string& str) {
        std::vector<std::string> out;
        std::stringstream ss(str);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t\r") + 1);
            if (!tok.empty()) out.push_back(tok);
        }
        return out;
    };
    // modurile de foc (implicit: din `automatic` / categorie)
    fire_modes.clear();
    for (const auto& t : split(ini.get("weapon", "fire_modes", ""))) {
        if (t == "semi") fire_modes.push_back(FireMode::Semi);
        else if (t == "burst") fire_modes.push_back(FireMode::Burst);
        else if (t == "auto") fire_modes.push_back(FireMode::Auto);
        else if (t == "bolt") fire_modes.push_back(FireMode::Bolt);
    }
    if (fire_modes.empty()) {
        if (bolt_time > 0.0) fire_modes = {FireMode::Bolt};
        else if (automatic) fire_modes = {FireMode::Auto, FireMode::Semi};
        else fire_modes = {FireMode::Semi};
    }
    automatic = has_mode(FireMode::Auto);

    // punctele de prindere si forma: implicit pe categorie, suprascrise din [sockets] / [visual]
    default_layout(model, sockets, visual);
    auto sock = [&](const char* key, WSocket& out) {
        std::string v = ini.get("sockets", key, "");
        if (v.empty()) return;
        std::stringstream ss(v);
        ss >> out.x >> out.y >> out.z;
    };
    sock("grip", sockets.grip); sock("support", sockets.support); sock("mag", sockets.mag); sock("bolt", sockets.bolt);
    sock("charging", sockets.charging); sock("port", sockets.port); sock("muzzle", sockets.muzzle); sock("optic", sockets.optic);
    sock("trigger", sockets.trigger);
    sockets.grip_tilt = ini.get_d("sockets", "grip_tilt", sockets.grip_tilt);
    visual.receiver = ini.get("visual", "receiver", visual.receiver);
    visual.stock = ini.get("visual", "stock", visual.stock);
    visual.mag_shape = ini.get("visual", "mag_shape", visual.mag_shape);
    visual.handguard_z = ini.get_d("visual", "handguard_z", visual.handguard_z);
    visual.handguard_len = ini.get_d("visual", "handguard_len", visual.handguard_len);
    visual.barrel_len = ini.get_d("visual", "barrel_len", visual.barrel_len);
    visual.iron_height = ini.get_d("visual", "iron_height", visual.iron_height);
    visual.mag_tilt = ini.get_d("visual", "mag_tilt", visual.mag_tilt);
    {
        // model extern: calea e relativa la fisierul armei
        std::string mdl = ini.get("visual", "model", "");
        if (!mdl.empty()) {
            std::filesystem::path mp(mdl);
            if (mp.is_relative()) mp = std::filesystem::path(path).parent_path() / mp;
            std::error_code ec;
            visual.model = std::filesystem::weakly_canonical(mp, ec).string();
            if (ec) visual.model = mp.string();
        }
        const double deg = 3.14159265358979 / 180.0;
        visual.model_scale = ini.get_d("visual", "model_scale", visual.model_scale);
        {
            std::stringstream as(ini.get("visual", "model_anchor", ""));
            double a = 0;
            for (int k = 0; k < 3 && (as >> a); ++k) visual.model_anchor[k] = a;
        }
        visual.model_forward = ini.get("visual", "model_forward", visual.model_forward);
        visual.model_up = ini.get("visual", "model_up", visual.model_up);
        {
            // "mag x0 y0 x1 y1; stock x0 y0 x1 y1; ..."
            std::stringstream ps(ini.get("visual", "model_parts", ""));
            std::string item;
            while (std::getline(ps, item, ';')) {
                std::stringstream is(item);
                WeaponVisual::ModelPart mp;
                if (is >> mp.kind >> mp.x0 >> mp.y0 >> mp.x1 >> mp.y1) {
                    if (!mp.kind.empty() && mp.kind.back() == '@') { mp.kind.pop_back(); mp.per_tri = true; }
                    visual.model_parts.push_back(mp);
                }
            }
        }
        visual.rail_y = ini.get_d("visual", "rail_y", visual.rail_y);
        visual.rail_radius = ini.get_d("visual", "rail_radius", visual.rail_radius);
        visual.model_yaw = ini.get_d("visual", "model_yaw", visual.model_yaw / deg) * deg;
        visual.model_pitch = ini.get_d("visual", "model_pitch", visual.model_pitch / deg) * deg;
        visual.model_roll = ini.get_d("visual", "model_roll", visual.model_roll / deg) * deg;
        std::string hm = ini.get("visual", "model_has_mag", visual.model_has_mag ? "yes" : "no");
        visual.model_has_mag = hm == "yes" || hm == "true" || hm == "1" || hm == "da";
    }

    attachments = split(ini.get("weapon", "attachments", ""));
    derive();
    base_id = id;
    if (apply) apply_attachments(catalog);
    return true;
}

bool WeaponData::has_mode(FireMode m) const {
    for (FireMode f : fire_modes) if (f == m) return true;
    return false;
}

const WeaponAttachment* WeaponData::mounted_in(AttachmentType t) const {
    for (const auto& a : mounted) if (a.type == t) return &a;
    return nullptr;
}

void WeaponData::apply_attachments(const std::vector<WeaponAttachment>* catalog) {
    has_reddot = has_holo = has_scope = has_suppressor = has_compensator = has_brake = has_flashhider = false;
    has_light = has_laser = has_foregrip = has_angledgrip = has_extmag = has_drum = has_stock = has_light_stock = false;
    has_long_barrel = has_short_barrel = has_bipod = false;
    sight_height = 0.0;
    magnification = 1.0;
    eye_relief = 0.0;
    spread_attach_mul = recoil_attach_mul = recovery_attach_mul = move_attach_mul = 1.0;
    mounted.clear();
    std::vector<WeaponAttachment> fallback;
    if (!catalog) { fallback = attachment_catalog(); catalog = &fallback; }
    for (const auto& a : attachments) {
        const WeaponAttachment* c = nullptr;
        for (const auto& x : *catalog) if (x.name == a) { c = &x; break; }
        if (!c || !c->fits_model(model_name())) continue;
        if (mounted_in(c->type)) continue;             // un singur atasament pe locas
        mounted.push_back(*c);
        recoil_pitch *= c->recoilModifier;
        recoil_yaw *= c->recoilH >= 0.0 ? c->recoilH : c->recoilModifier;
        recoil_attach_mul *= c->recoilModifier;
        sound_radius *= c->soundModifier;
        loudness *= (c->soundModifier < 1.0 ? 0.35 : c->soundModifier);
        flash_intensity *= c->flashModifier;
        ads_time *= c->adsModifier;
        sway_scale *= c->swayModifier;
        spread_hip *= c->spread;
        spread_ads *= c->spread;
        spread_attach_mul *= c->spread;
        camera_kick *= c->kick;
        draw_time *= c->switchTime;
        holster_time *= c->switchTime;
        reload_tactical *= c->reloadTime;
        reload_empty *= c->reloadTime;
        weight += c->weightAdd;
        length += c->lengthAdd;
        mag_size += c->magBonus;
        movement_penalty *= c->moveModifier;
        move_attach_mul *= c->moveModifier;
        muzzle_velocity *= c->velocityModifier;
        recoil_recovery *= c->stability;
        recovery_attach_mul *= c->stability;
        if (c->type == AttachmentType::Optic) {
            sight_height = c->sightHeight;
            magnification = c->magnification;
            eye_relief = c->eyeRelief;
        }
        switch (c->visual) {
            case AttachmentVisual::RedDot: has_reddot = true; break;
            case AttachmentVisual::Holo: has_holo = true; break;
            case AttachmentVisual::Scope: has_scope = true; break;
            case AttachmentVisual::Suppressor: has_suppressor = true; muzzle_smoke *= 1.6; break;
            case AttachmentVisual::Compensator: has_compensator = true; break;
            case AttachmentVisual::Brake: has_brake = true; break;
            case AttachmentVisual::FlashHider: has_flashhider = true; break;
            case AttachmentVisual::VerticalGrip: has_foregrip = true; break;
            case AttachmentVisual::AngledGrip: has_angledgrip = true; break;
            case AttachmentVisual::ExtMag: has_extmag = true; break;
            case AttachmentVisual::Drum: has_drum = true; break;
            case AttachmentVisual::HeavyStock: has_stock = true; break;
            case AttachmentVisual::LightStock: has_light_stock = true; break;
            case AttachmentVisual::Light: has_light = true; break;
            case AttachmentVisual::Laser: has_laser = true; break;
            case AttachmentVisual::LongBarrel: has_long_barrel = true; break;
            case AttachmentVisual::ShortBarrel: has_short_barrel = true; break;
            case AttachmentVisual::Bipod: has_bipod = true; break;
            default: break;
        }
        // atasamentele care muta mainile / lungesc arma (IK si desenul le folosesc)
        if (c->type == AttachmentType::Barrel) {
            visual.length_scale *= 1.0 + c->lengthAdd / std::max(0.2, length - c->lengthAdd);
            sockets.muzzle.z += c->lengthAdd;
        }
        // manerele se prind sub garda reala a armei (axa + raza), nu la o inaltime fixa
        const double hg_bottom = visual.rail_y + visual.rail_radius;
        if (c->visual == AttachmentVisual::VerticalGrip) sockets.support = {0.0, hg_bottom + 0.066, visual.handguard_z + 0.02};
        if (c->visual == AttachmentVisual::AngledGrip) sockets.support = {-0.005, hg_bottom + 0.026, visual.handguard_z - 0.05};
    }
    derive();
}

const char* WeaponData::model_name() const {
    switch (model) {
        case WeaponModel::Smg: return "smg";
        case WeaponModel::Pistol: return "pistol";
        case WeaponModel::Shotgun: return "shotgun";
        case WeaponModel::Sniper: return "sniper";
        default: return "rifle";
    }
}

AttachmentVisual attachment_visual_from(const std::string& s) {
    for (int i = 0; i < static_cast<int>(AttachmentVisual::Count); ++i)
        if (s == attachment_visual_name(static_cast<AttachmentVisual>(i))) return static_cast<AttachmentVisual>(i);
    return AttachmentVisual::None;
}

const char* attachment_visual_name(AttachmentVisual v) {
    switch (v) {
        case AttachmentVisual::RedDot: return "reddot";
        case AttachmentVisual::Holo: return "holo";
        case AttachmentVisual::Scope: return "scope";
        case AttachmentVisual::Suppressor: return "suppressor";
        case AttachmentVisual::Compensator: return "compensator";
        case AttachmentVisual::Brake: return "brake";
        case AttachmentVisual::FlashHider: return "flashhider";
        case AttachmentVisual::VerticalGrip: return "vgrip";
        case AttachmentVisual::AngledGrip: return "agrip";
        case AttachmentVisual::ExtMag: return "extmag";
        case AttachmentVisual::Drum: return "drum";
        case AttachmentVisual::HeavyStock: return "heavystock";
        case AttachmentVisual::LightStock: return "lightstock";
        case AttachmentVisual::Light: return "light";
        case AttachmentVisual::Laser: return "laser";
        case AttachmentVisual::LongBarrel: return "longbarrel";
        case AttachmentVisual::ShortBarrel: return "shortbarrel";
        case AttachmentVisual::Bipod: return "bipod";
        default: return "none";
    }
}

const char* attachment_type_name(AttachmentType t) {
    switch (t) {
        case AttachmentType::Optic: return "OPTICA";
        case AttachmentType::Muzzle: return "GURA TEVII";
        case AttachmentType::Underbarrel: return "SUB TEAVA";
        case AttachmentType::Magazine: return "INCARCATOR";
        case AttachmentType::Stock: return "PAT";
        case AttachmentType::Side: return "LANTERNA";
        case AttachmentType::Laser: return "LASER";
        case AttachmentType::Barrel: return "TEAVA";
        default: return "?";
    }
}

std::vector<WeaponAttachment> attachment_catalog() {
    auto mk = [](const char* n, AttachmentType t, AttachmentVisual v) {
        WeaponAttachment a;
        a.name = n; a.label = n; a.type = t; a.visual = v;
        return a;
    };
    std::vector<WeaponAttachment> c;
    WeaponAttachment r = mk("reddot", AttachmentType::Optic, AttachmentVisual::RedDot);
    r.adsModifier = 0.9; r.weightAdd = 0.1; r.sightHeight = 0.02; c.push_back(r);
    WeaponAttachment l = mk("light", AttachmentType::Side, AttachmentVisual::Light);
    l.weightAdd = 0.1; c.push_back(l);
    WeaponAttachment f = mk("foregrip", AttachmentType::Underbarrel, AttachmentVisual::VerticalGrip);
    f.recoilModifier = 0.8; f.recoilH = 0.85; f.swayModifier = 0.85; f.weightAdd = 0.15; c.push_back(f);
    return c;
}

std::vector<WeaponAttachment> load_attachment_db(const std::string& path) {
    IniParser ini;
    if (!ini.load(path) || ini.sections().empty()) return attachment_catalog();
    std::vector<WeaponAttachment> out;
    for (const auto& sec : ini.sections()) {
        WeaponAttachment a;
        a.name = sec;
        a.label = ini.get(sec, "label", sec);
        a.desc = ini.get(sec, "desc", "");
        std::string t = ini.get(sec, "type", "optic");
        a.type = t == "muzzle" ? AttachmentType::Muzzle : t == "underbarrel" ? AttachmentType::Underbarrel
               : t == "magazine" ? AttachmentType::Magazine : t == "stock" ? AttachmentType::Stock
               : (t == "side" || t == "light") ? AttachmentType::Side : t == "laser" ? AttachmentType::Laser
               : t == "barrel" ? AttachmentType::Barrel : AttachmentType::Optic;
        a.visual = attachment_visual_from(ini.get(sec, "visual", sec));
        std::stringstream ss(ini.get(sec, "fits", ""));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t\r") + 1);
            if (!tok.empty()) a.fits.push_back(tok);
        }
        a.recoilModifier = ini.get_d(sec, "recoil_v", 1.0);
        a.recoilH = ini.get_d(sec, "recoil_h", -1.0);
        a.soundModifier = ini.get_d(sec, "sound", 1.0);
        a.flashModifier = ini.get_d(sec, "flash", 1.0);
        a.adsModifier = ini.get_d(sec, "ads", 1.0);
        a.swayModifier = ini.get_d(sec, "sway", 1.0);
        a.spread = ini.get_d(sec, "spread", 1.0);
        a.kick = ini.get_d(sec, "kick", 1.0);
        a.switchTime = ini.get_d(sec, "switch", 1.0);
        a.reloadTime = ini.get_d(sec, "reload", 1.0);
        a.weightAdd = ini.get_d(sec, "weight", 0.0);
        a.lengthAdd = ini.get_d(sec, "length", 0.0);
        a.magBonus = static_cast<int>(ini.get_d(sec, "mag", 0.0));
        a.moveModifier = ini.get_d(sec, "move", 1.0);
        a.velocityModifier = ini.get_d(sec, "velocity", 1.0);
        a.stability = ini.get_d(sec, "stability", 1.0);
        a.sightHeight = ini.get_d(sec, "sight_height", a.type == AttachmentType::Optic ? 0.02 : 0.0);
        a.magnification = ini.get_d(sec, "magnification", 1.0);
        a.eyeRelief = ini.get_d(sec, "eye_relief", 0.0);
        // modelul extern: calea e relativa la fisierul atasamentelor
        auto resolve = [&](const std::string& m) {
            if (m.empty()) return m;
            std::filesystem::path mp(m);
            if (mp.is_relative()) mp = std::filesystem::path(path).parent_path() / mp;
            return mp.lexically_normal().generic_string();
        };
        a.model = resolve(ini.get(sec, "model", ""));
        a.model_pistol = resolve(ini.get(sec, "model_pistol", ""));
        a.model_scale = ini.get_d(sec, "model_scale", 1.0);
        a.model_pistol_scale = ini.get_d(sec, "model_pistol_scale", 0.0);
        {
            std::stringstream ms(ini.get(sec, "model_anchor", "0 0 0"));
            ms >> a.model_anchor[0] >> a.model_anchor[1] >> a.model_anchor[2];
        }
        out.push_back(a);
    }
    return out;
}

WeaponData make_default_rifle() {
    WeaponData w;
    default_layout(w.model, w.sockets, w.visual);
    w.attachments = {"reddot", "light"};
    w.derive();
    w.apply_attachments();
    return w;
}

WeaponData make_default_pistol() {
    WeaponData w;
    w.id = "g17"; w.name = "G17"; w.model = WeaponModel::Pistol; w.caliber = "9x19";
    w.rpm = 450; w.mag_size = 17; w.automatic = false;
    w.bullet_mass = 0.0075; w.muzzle_velocity = 370.0; w.bullet_area = 6.4e-5;
    w.damage = 24; w.range = 120;
    w.spread_hip = 0.016; w.spread_ads = 0.004; w.spread_move = 0.004;
    w.recoil_pitch = 0.030; w.recoil_yaw = 0.010; w.recoil_recovery = 9.0;
    w.weight = 0.9; w.length = 0.2; w.ads_time = 0.18; w.draw_time = 0.3; w.holster_time = 0.25;
    w.reload_tactical = 1.6; w.reload_empty = 1.9; w.mag_weight = 0.28; w.mag_weight_empty = 0.07;
    w.heat_per_shot = 0.008; w.casing = 1;
    w.sound_radius = 22; w.loudness = 0.8; w.flash_intensity = 0.7; w.reserve_mags = 3;
    w.fire_modes = {FireMode::Semi};
    default_layout(w.model, w.sockets, w.visual);
    w.attachments = {"light"};
    w.derive();
    w.apply_attachments();
    return w;
}

}
