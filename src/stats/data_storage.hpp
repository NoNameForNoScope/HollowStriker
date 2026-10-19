#ifndef BODYCAM_DATA_STORAGE_HPP
#define BODYCAM_DATA_STORAGE_HPP

// Stocarea persistenta a rezultatelor simularii: fisiere CSV in data/ (acelasi format ca
// rezultatele experimentelor existente - results/*.csv - citibile direct in Python/Excel).
// Scrierea se face doar la evenimente (sfarsit de runda / meci / sesiune), niciodata pe cadru.
#include "records.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bodycam {

namespace csvio {

inline std::string fmt(int v) { return std::to_string(v); }
inline std::string fmt(int64_t v) { return std::to_string(static_cast<long long>(v)); }
inline std::string fmt(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}
inline std::string fmt(const std::string& s) {
    std::string o = s;
    for (char& c : o) if (c == ',' || c == '\n' || c == '\r') c = ' ';
    return o;
}
inline void parse(const std::string& s, int& v) { try { v = std::stoi(s); } catch (...) {} }
inline void parse(const std::string& s, int64_t& v) { try { v = static_cast<int64_t>(std::stoll(s)); } catch (...) {} }
inline void parse(const std::string& s, double& v) { try { v = std::stod(s); } catch (...) {} }
inline void parse(const std::string& s, std::string& v) { v = s; }

struct HeaderV {
    std::string out;
    template <class T> void operator()(const char* name, T&) { if (!out.empty()) out += ','; out += name; }
};
struct WriteV {
    std::string out;
    bool first = true;
    template <class T> void operator()(const char*, T& v) { if (!first) out += ','; first = false; out += fmt(v); }
};
struct ReadV {
    const std::vector<std::string>* cols;
    const std::map<std::string, size_t>* idx;
    template <class T> void operator()(const char* name, T& v) {
        auto it = idx->find(name);
        if (it != idx->end() && it->second < cols->size()) parse((*cols)[it->second], v);
    }
};

inline std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

template <class T> std::string header() {
    T t;
    HeaderV h;
    t.visit(h);
    return h.out;
}
template <class T> std::string row(const T& rec) {
    WriteV w;
    const_cast<T&>(rec).visit(w);
    return w.out;
}

} // namespace csvio

class DataStorage {
public:
    std::string dir = "data";
    bool read_only = false;      // capturi / teste: datele existente se citesc, nimic nu se scrie

    std::string path(const std::string& file) const { return dir + "/" + file; }
    bool ensure_dir() const;

    // Adauga inregistrari; daca fisierul are alt antet (coloane noi), il rescrie cu antetul curent.
    template <class T> bool append(const std::string& file, const std::vector<T>& recs) {
        if (recs.empty() || read_only) return true;
        if (!ensure_dir()) return false;
        const std::string p = path(file);
        const std::string hdr = csvio::header<T>();
        std::string existing;
        {
            std::ifstream in(p);
            if (in) std::getline(in, existing);
        }
        if (!existing.empty() && existing != hdr) {
            std::vector<T> old = load<T>(file);
            std::ofstream out(p, std::ios::trunc);
            if (!out) return false;
            out << hdr << "\n";
            for (const auto& r : old) out << csvio::row(r) << "\n";
            existing = hdr;
        }
        std::ofstream out(p, std::ios::app);
        if (!out) return false;
        if (existing.empty()) out << hdr << "\n";
        for (const auto& r : recs) out << csvio::row(r) << "\n";
        return static_cast<bool>(out);
    }
    template <class T> bool append_one(const std::string& file, const T& rec) { return append(file, std::vector<T>{rec}); }

    // Citeste toate inregistrarile (coloanele dupa nume; lipsa = valoarea implicita)
    template <class T> std::vector<T> load(const std::string& file) const {
        return load_if<T>(file, [](const T&) { return true; });
    }
    template <class T, class Pred> std::vector<T> load_if(const std::string& file, Pred pred) const {
        std::vector<T> out;
        std::ifstream in(path(file));
        if (!in) return out;
        std::string line;
        if (!std::getline(in, line)) return out;
        std::vector<std::string> names = csvio::split(line);
        std::map<std::string, size_t> idx;
        for (size_t i = 0; i < names.size(); ++i) idx[names[i]] = i;
        while (std::getline(in, line)) {
            if (line.empty() || line == "\r") continue;
            std::vector<std::string> cols = csvio::split(line);
            T rec;
            csvio::ReadV r{&cols, &idx};
            rec.visit(r);
            if (pred(rec)) out.push_back(std::move(rec));
        }
        return out;
    }
};

}
#endif
