#ifndef BODYCAM_MAP_HPP
#define BODYCAM_MAP_HPP

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>

namespace bodycam {

// Inaltimea peretilor/tavanului in metri (lumea este 2.5D: grid 1m x 1m).
constexpr double kWallHeight = 2.7;
// Zid scund ('h'): blocheaza mersul si gloantele joase, se poate escalada (mantle)
constexpr double kLowWallZ = 1.0;

// Markeri extrasi din fisierul hartii.
// 'S' spawn Blue, 'E' spawn Red, 'p' patrulare, 'O' zona-obiectiv,
// 'c' lada, 'b' butoi, 'k' conserve, 'z' intrerupator (aprins), 'y' intrerupator (stins),
// 'P' tablou electric (curent general), 'C' terminal (date), 'K' dulap cu provizii
struct MapMarker {
    char type;
    int x, y;
};

class Map {
public:
    int width = 0;
    int height = 0;
    std::vector<char> grid;
    std::vector<MapMarker> markers;

    // Usi ('D'): deschidere 0..1, tinta, sensul de rotatie (+1/-1), incuietoare
    std::vector<float> door_open;
    std::vector<float> door_target;
    std::vector<int8_t> door_swing;
    std::vector<uint8_t> door_locked;
    std::vector<float> door_speed;
    std::vector<int8_t> door_home;   // sensul in care se deschide usa (balamaua e fixa): +1/-1
    std::vector<uint8_t> glass;      // cioburi pe jos (dupa o fereastra sparta)
    std::vector<uint8_t> door_hold;  // panoul loveste un corp: usa sta pe loc in acest pas

    // Incaperi (pentru grupuri de lampi) si starea lampilor
    std::vector<int> room;           // id de incapere pentru fiecare celula (-1 = perete/exterior)
    std::vector<uint8_t> outdoor;    // 1 = sub cerul liber (podele exterioare + zidurile scunde dintre ele)
    std::vector<uint8_t> lamp_on;    // 1 = lampa aprinsa (doar celulele 'L')
    bool power = true;               // curent general (altfel lumini de avarie)

    bool load(const std::string& filepath);
    void from_rows(const std::vector<std::string>& rows);
    char cell(int x, int y) const;

    // Blocheaza miscarea (pereti, ferestre, usi inchise)
    bool is_wall(int x, int y) const;
    // Blocheaza vederea si lumina (ferestrele nu)
    bool blocks_sight(int x, int y) const;
    // Opreste/franeaza gloantele (fereastra sparta nu mai conteaza)
    bool blocks_bullets(int x, int y) const;
    // Perete permanent (pathfinding: usile se pot deschide)
    bool is_static_wall(int x, int y) const;
    bool is_window(int x, int y) const { char c = cell(x, y); return c == '6' || c == '7'; }
    bool is_low(int x, int y) const { return cell(x, y) == 'h'; }
    bool is_door(int x, int y) const;
    float door_state(int x, int y) const;
    bool door_along_x(int x, int y) const;
    bool is_locked(int x, int y) const;
    void set_door(int x, int y, bool open, int swing = 0, float speed = 1.8f);
    void set_door_partial(int x, int y, float target, int swing, float speed);
    // Avanseaza animatia usilor. Intoarce true daca vreo usa a trecut pragul deschis/inchis.
    bool update_doors(double dt);
    void break_window(int x, int y);
    // Panoul usii ca segment (balama -> capatul liber), dupa deschiderea curenta sau data
    bool door_panel(int x, int y, double& hx, double& hy, double& ex, double& ey, double open = -1.0) const;
    // De partea cui se afla (ax, ay)? true = usa trebuie trasa spre tine (nu impinsa)
    bool door_pull_side(int x, int y, double ax, double ay) const;
    bool has_glass(int x, int y) const;

    bool is_outdoor(int x, int y) const;
    int room_at(int x, int y) const;

    // Coliziune cerc-grid
    bool is_colliding(double cx, double cy, double radius) const;
    // Miscare cu alunecare pe axe separate
    void move_with_collision(double& x, double& y, double dx, double dy, double radius) const;

    std::vector<MapMarker> markers_of(char type) const;
    uint32_t revision = 0;           // creste la orice schimbare (lumina, retea)

private:
    void finalize();
    void compute_rooms();
};

}
#endif
