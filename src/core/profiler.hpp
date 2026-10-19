#ifndef BODYCAM_PROFILER_HPP
#define BODYCAM_PROFILER_HPP

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <mutex>

namespace bodycam {

// Profiler minimal (echivalentul "stat unit" / Unreal Insights): fiecare sistem
// masoara cat dureaza, iar overlay-ul de debug afiseaza mediile. Regula: nu
// optimizam dupa intuitie, ci dupa ce arata masuratorile.
class Profiler {
public:
    struct Entry {
        std::string name;
        double avg_ms = 0.0;
        double last_ms = 0.0;
        double max_ms = 0.0;
    };

    static Profiler& get() {
        static Profiler p;
        return p;
    }

    void add(const char* name, double ms) {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& e : entries_) {
            if (e.name == name) {
                e.last_ms = ms;
                e.avg_ms = e.avg_ms * 0.92 + ms * 0.08;
                e.max_ms = std::max(e.max_ms * 0.995, ms);
                return;
            }
        }
        entries_.push_back({name, ms, ms, ms});
    }

    std::vector<Entry> entries() const {
        std::lock_guard<std::mutex> lock(mu_);
        return entries_;
    }

private:
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
};

class ProfileScope {
public:
    explicit ProfileScope(const char* name) : name_(name), t0_(std::chrono::high_resolution_clock::now()) {}
    ~ProfileScope() {
        auto t1 = std::chrono::high_resolution_clock::now();
        Profiler::get().add(name_, std::chrono::duration<double, std::milli>(t1 - t0_).count());
    }
private:
    const char* name_;
    std::chrono::high_resolution_clock::time_point t0_;
};

#define BODYCAM_PROFILE_CAT2(a, b) a##b
#define BODYCAM_PROFILE_CAT(a, b) BODYCAM_PROFILE_CAT2(a, b)
#define PROFILE_SCOPE(name) ::bodycam::ProfileScope BODYCAM_PROFILE_CAT(profile_scope_, __LINE__)(name)

}
#endif
