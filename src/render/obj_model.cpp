#include "obj_model.hpp"
#include "../core/asset_manifest.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace bodycam {

namespace {
struct MtlInfo { Rgb kd{0.5f, 0.5f, 0.5f}; float ks = 0.0f; float ns = 0.0f; float d = 1.0f; uint8_t mat = MatPlastic; };

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

uint8_t classify(const std::string& name, const MtlInfo& mi) {
    std::string n = lower(name);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (has("glass") || has("lens") || mi.d < 0.9f) return MatGlass;
    if (has("rubber") || has("tire") || has("tyre")) return MatRubber;
    if (has("wood")) return MatWood;
    if (has("skin") || has("face") || has("hand")) return MatSkin;
    if (has("leather") || has("glove") || has("boot")) return MatLeather;
    if (has("fabric") || has("cloth") || has("shirt") || has("pants") || has("vest")) return MatFabric;
    if (has("brass")) return MatBrass;
    if (has("alu")) return MatAluminum;
    if (has("metal") || has("steel") || has("iron") || has("barrel")) return MatSteel;
    if (has("paint")) return MatPaint;
    if (mi.ks > 0.45f || mi.ns > 250.0f) return MatSteel;
    return MatPlastic;
}

void load_mtl(const std::string& path, std::map<std::string, MtlInfo>& out) {
    std::ifstream f(path);
    if (!f) return;
    std::string line, cur;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string k;
        ss >> k;
        if (k == "newmtl") { ss >> cur; out[cur] = MtlInfo{}; }
        else if (cur.empty()) continue;
        else if (k == "Kd") { float r = 0.5f, g = 0.5f, b = 0.5f; ss >> r >> g >> b; out[cur].kd = {r, g, b}; }
        else if (k == "Ks") { float r = 0, g = 0, b = 0; ss >> r >> g >> b; out[cur].ks = (r + g + b) / 3.0f; }
        else if (k == "Ns") ss >> out[cur].ns;
        else if (k == "d") ss >> out[cur].d;
        else if (k == "Tr") { float t = 0; ss >> t; out[cur].d = 1.0f - t; }
    }
    for (auto& kv : out) kv.second.mat = classify(kv.first, kv.second);
}

// indexul OBJ (1-based, negativ = de la sfarsit) -> 0-based; -1 invalid
int fix_index(int i, size_t n) {
    if (i > 0) return i - 1 < static_cast<int>(n) ? i - 1 : -1;
    if (i < 0) return static_cast<int>(n) + i >= 0 ? static_cast<int>(n) + i : -1;
    return -1;
}
}

bool load_obj(const std::string& path, ObjModel& out) {
    out = ObjModel{};
    std::ifstream f(path);
    if (!f) { out.error = "nu se poate deschide " + path; return false; }
    std::vector<V3> vp, vn;
    std::map<std::string, MtlInfo> mtl;
    MtlInfo cur;
    std::string cur_name;
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();
    std::string line;
    bool first = true;
    // piesele conectate (varfuri comune) -> centrul fiecarei piese, pentru separarea pe parti
    std::vector<int> parent;
    auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    std::vector<int> tri_root_vertex;   // pe triunghi: un varf din piesa lui
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string k;
        ss >> k;
        if (k == "v") { double x = 0, y = 0, z = 0; ss >> x >> y >> z; vp.push_back({x, y, z}); parent.push_back(static_cast<int>(parent.size())); }
        else if (k == "vn") { double x = 0, y = 0, z = 0; ss >> x >> y >> z; vn.push_back(norm(V3{x, y, z})); }
        else if (k == "mtllib") { std::string fn; std::getline(ss >> std::ws, fn); load_mtl((dir / fn).string(), mtl); }
        else if (k == "usemtl") {
            ss >> cur_name;
            auto it = mtl.find(cur_name);
            cur = it != mtl.end() ? it->second : MtlInfo{};
            if (it == mtl.end()) cur.mat = classify(cur_name, cur);
        } else if (k == "f") {
            std::vector<int> pi, ni;
            std::string tok;
            while (ss >> tok) {
                int a = 0, c = 0;
                size_t s1 = tok.find('/');
                a = std::atoi(tok.substr(0, s1).c_str());
                if (s1 != std::string::npos) {
                    size_t s2 = tok.find('/', s1 + 1);
                    if (s2 != std::string::npos) c = std::atoi(tok.substr(s2 + 1).c_str());
                }
                pi.push_back(fix_index(a, vp.size()));
                ni.push_back(c != 0 ? fix_index(c, vn.size()) : -1);
            }
            if (pi.size() < 3) continue;
            for (size_t q = 1; q < pi.size(); ++q)
                if (pi[0] >= 0 && pi[q] >= 0) { int ra = find(pi[0]), rb = find(pi[q]); if (ra != rb) parent[ra] = rb; }
            // culoarea: Kd din MTL este deja liniara (exportul Blender scrie culoarea de baza liniara)
            Rgb kd{cur.kd.r, cur.kd.g, cur.kd.b};
            for (size_t t = 1; t + 1 < pi.size(); ++t) {
                int ids[3] = {pi[0], pi[t], pi[t + 1]};
                int nds[3] = {ni[0], ni[t], ni[t + 1]};
                if (ids[0] < 0 || ids[1] < 0 || ids[2] < 0) continue;
                V3 fn = norm(cross(vp[ids[1]] - vp[ids[0]], vp[ids[2]] - vp[ids[0]]));
                for (int c = 0; c < 3; ++c) {
                    V3 p = vp[ids[c]];
                    out.pos.push_back(p);
                    out.nrm.push_back(nds[c] >= 0 ? vn[nds[c]] : fn);
                    out.col.push_back(kd);
                    out.mat.push_back(cur.mat);
                    if (c == 0) tri_root_vertex.push_back(ids[0]);
                    if (first) { out.bmin = out.bmax = p; first = false; }
                    out.bmin = {std::min(out.bmin.x, p.x), std::min(out.bmin.y, p.y), std::min(out.bmin.z, p.z)};
                    out.bmax = {std::max(out.bmax.x, p.x), std::max(out.bmax.y, p.y), std::max(out.bmax.z, p.z)};
                }
                out.triangles++;
            }
        }
    }
    if (out.triangles == 0) { out.error = "modelul nu are fete: " + path; return false; }
    // centrul fiecarei piese (media varfurilor triunghiurilor ei)
    std::vector<V3> sum(vp.size(), V3{0, 0, 0});
    std::vector<int> cnt(vp.size(), 0);
    for (size_t t = 0; t < tri_root_vertex.size(); ++t) {
        int r = find(tri_root_vertex[t]);
        V3 c = (out.pos[t * 3] + out.pos[t * 3 + 1] + out.pos[t * 3 + 2]) * (1.0 / 3.0);
        sum[r] = sum[r] + c;
        cnt[r]++;
    }
    out.tri_center.resize(tri_root_vertex.size());
    for (size_t t = 0; t < tri_root_vertex.size(); ++t) {
        int r = find(tri_root_vertex[t]);
        out.tri_center[t] = sum[r] * (1.0 / std::max(1, cnt[r]));
    }
    return true;
}

void append_obj(Mesh& m, const ObjModel& o, const M3& R, V3 t, double scale) {
    const uint8_t keep = m.mat;
    for (size_t i = 0; i + 2 < o.pos.size(); i += 3) {
        uint32_t ids[3];
        for (int c = 0; c < 3; ++c) {
            m.mat = o.mat[i + c];
            ids[c] = m.vertex(t + R * (o.pos[i + c] * scale), norm(R * o.nrm[i + c]), o.col[i + c]);
        }
        m.tri(ids[0], ids[1], ids[2]);
    }
    m.mat = keep;
}

const ObjModel* external_model(const std::string& path) {
    static std::mutex mtx;
    static std::map<std::string, std::unique_ptr<ObjModel>> cache;   // nullptr = refuzat (nu se reincearca)
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.get();
    std::string rel;
    std::string why = verify_external_file(path, &rel);
    if (!why.empty()) {
        std::fprintf(stderr, "[resurse externe] %s refuzat: %s\n", path.c_str(), why.c_str());
        cache[path] = nullptr;
        return nullptr;
    }
    auto om = std::make_unique<ObjModel>();
    if (!load_obj(path, *om)) {
        std::fprintf(stderr, "[resurse externe] %s\n", om->error.c_str());
        cache[path] = nullptr;
        return nullptr;
    }
    std::printf("[resurse externe] %s: %d triunghiuri (licenta verificata)\n", rel.c_str(), om->triangles);
    const ObjModel* p = om.get();
    cache[path] = std::move(om);
    return p;
}

}
