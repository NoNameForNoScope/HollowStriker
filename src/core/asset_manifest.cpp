#include "asset_manifest.hpp"
#include "ini.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace bodycam {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string norm_path(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.rfind("./", 0) == 0) s = s.substr(2);
    return lower(s);
}
}

bool AssetManifest::license_allowed(const std::string& license, bool& needs_attribution, std::string& why) {
    std::string l = lower(license);
    l.erase(std::remove_if(l.begin(), l.end(), [](char c) { return c == ' ' || c == '_'; }), l.end());
    needs_attribution = false;
    if (l.empty() || l == "unknown" || l == "?") { why = "licenta necunoscuta"; return false; }
    // interzise explicit: necomerciale, fara derivate, editoriale, extrase din jocuri
    const char* banned[] = {"nc", "noncommercial", "-nd", "noderiv", "editorial", "ripped", "extracted", "proprietary", "allrightsreserved"};
    for (const char* b : banned)
        if (l.find(b) != std::string::npos) { why = std::string("licenta nepermisa (") + license + ")"; return false; }
    if (l == "cc0" || l == "cc0-1.0" || l == "cc0 1.0" || l == "publicdomain" || l == "pd" || l == "unlicense") return true;
    if (l.rfind("cc-by-", 0) == 0 || l == "cc-by" || l.rfind("ccby", 0) == 0) {
        if (l.find("sa") != std::string::npos) { why = "CC-BY-SA: share-alike (de discutat inainte de folosire)"; return false; }
        needs_attribution = true;
        return true;
    }
    if (l == "mit" || l == "apache-2.0" || l == "apache2" || l == "bsd" || l == "bsd-2-clause" || l == "bsd-3-clause" || l == "zlib") {
        needs_attribution = true;
        return true;
    }
    why = std::string("licenta neverificata (") + license + ")";
    return false;
}

bool AssetManifest::load(const std::string& manifest_path) {
    IniParser ini;
    if (!ini.load(manifest_path)) return false;
    root = std::filesystem::path(manifest_path).parent_path().string();
    assets.clear();
    for (const std::string& sec : ini.sections()) {
        ExternalAsset a;
        a.id = sec;
        a.file = ini.get(sec, "file");
        if (a.file.empty()) continue;
        a.title = ini.get(sec, "title");
        a.author = ini.get(sec, "author");
        a.source_url = ini.get(sec, "source");
        a.license = ini.get(sec, "license");
        a.license_url = ini.get(sec, "license_url");
        a.downloaded = ini.get(sec, "downloaded");
        a.notes = ini.get(sec, "notes");
        a.size_bytes = static_cast<long long>(ini.get_d(sec, "size_bytes", 0.0));
        std::string ap = lower(ini.get(sec, "approved", "no"));
        a.approved = ap == "yes" || ap == "da" || ap == "true" || ap == "1";
        assets.push_back(a);
    }
    return true;
}

const ExternalAsset* AssetManifest::find_file(const std::string& rel_file) const {
    std::string r = norm_path(rel_file);
    for (const auto& a : assets) {
        std::string f = norm_path(a.file);
        // o intrare poate acoperi un fisier exact sau un folder intreg (ex. "Weapons/kit/")
        if (f == r || (!f.empty() && f.back() == '/' && r.rfind(f, 0) == 0)) return &a;
    }
    return nullptr;
}

std::string AssetManifest::check(const std::string& rel_file) const {
    const ExternalAsset* a = find_file(rel_file);
    if (!a) return "lipseste din manifest";
    if (!a->approved) return "descarcarea nu a fost aprobata";
    if (a->source_url.empty()) return "lipseste sursa";
    bool attr = false;
    std::string why;
    if (!license_allowed(a->license, attr, why)) return why;
    if (attr && a->author.empty()) return "licenta cere atribuire, dar autorul lipseste";
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(root) / rel_file, ec)) return "fisierul nu exista";
    return "";
}

std::string verify_external_file(const std::string& path, std::string* rel_out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    for (fs::path d = p.parent_path(); !d.empty(); d = d.parent_path()) {
        fs::path man = d / "manifest.ini";
        if (fs::exists(man, ec)) {
            AssetManifest m;
            if (!m.load(man.string())) return "manifest ilizibil";
            std::string rel = fs::relative(p, d, ec).generic_string();
            if (rel_out) *rel_out = rel;
            return m.check(rel);
        }
        if (d == d.root_path()) break;
    }
    return "fara manifest (ExternalAssets/manifest.ini)";
}

}
