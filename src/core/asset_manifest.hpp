#ifndef BODYCAM_ASSET_MANIFEST_HPP
#define BODYCAM_ASSET_MANIFEST_HPP

// Evidenta resurselor externe (ExternalAssets/manifest.ini). Un model / textura / sunet
// descarcat din afara proiectului se incarca DOAR daca:
//   - are o intrare in manifest (sursa, autor, licenta, data, marime),
//   - licenta e permisa (CC0 / domeniu public / CC-BY / MIT / Apache / BSD; CC-BY cere autorul),
//   - utilizatorul a aprobat descarcarea (approved = yes),
//   - fisierul exista pe disc.
// Licentele necomerciale, "editorial", necunoscute sau resursele extrase din jocuri comerciale
// sunt refuzate. Motivul refuzului se poate afisa (consola / tableta).
#include <string>
#include <vector>

namespace bodycam {

struct ExternalAsset {
    std::string id;            // sectiunea din manifest
    std::string file;          // calea relativa la folderul ExternalAssets
    std::string title, author, source_url, license, license_url, downloaded, notes;
    long long size_bytes = 0;
    bool approved = false;     // utilizatorul a aprobat descarcarea
};

struct AssetManifest {
    std::string root;          // folderul ExternalAssets
    std::vector<ExternalAsset> assets;

    bool load(const std::string& manifest_path);
    const ExternalAsset* find_file(const std::string& rel_file) const;
    // "" = poate fi folosit; altfel motivul refuzului
    std::string check(const std::string& rel_file) const;
    static bool license_allowed(const std::string& license, bool& needs_attribution, std::string& why);
};

// Cauta manifestul pentru un fisier (urcand din folderul lui) si verifica intrarea.
// rel_out primeste calea relativa la folderul manifestului. "" = permis.
std::string verify_external_file(const std::string& path, std::string* rel_out = nullptr);

}
#endif
