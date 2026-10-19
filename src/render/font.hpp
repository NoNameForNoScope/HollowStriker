#ifndef BODYCAM_FONT_HPP
#define BODYCAM_FONT_HPP

// Font bitmap 5x7 (ASCII 32..95) pentru OSD-ul camerei si overlay-ul de debug.
#include <cstdint>
#include <cstring>

namespace bodycam {
namespace font {

// Fiecare caracter: 7 randuri a cate 5 biti (bitul 4 = coloana din stanga)
inline const uint8_t* glyph(char ch) {
    static const uint8_t table[64][7] = {
        {0, 0, 0, 0, 0, 0, 0},                          // ' '
        {4, 4, 4, 4, 4, 0, 4},                          // !
        {10, 10, 0, 0, 0, 0, 0},                        // "
        {10, 31, 10, 10, 31, 10, 0},                    // #
        {4, 15, 20, 14, 5, 30, 4},                      // $
        {24, 25, 2, 4, 8, 19, 3},                       // %
        {12, 18, 20, 8, 21, 18, 13},                    // &
        {4, 4, 0, 0, 0, 0, 0},                          // '
        {2, 4, 8, 8, 8, 4, 2},                          // (
        {8, 4, 2, 2, 2, 4, 8},                          // )
        {0, 4, 21, 14, 21, 4, 0},                       // *
        {0, 4, 4, 31, 4, 4, 0},                         // +
        {0, 0, 0, 0, 12, 4, 8},                         // ,
        {0, 0, 0, 31, 0, 0, 0},                         // -
        {0, 0, 0, 0, 0, 12, 12},                        // .
        {0, 1, 2, 4, 8, 16, 0},                         // /
        {14, 17, 19, 21, 25, 17, 14},                   // 0
        {4, 12, 4, 4, 4, 4, 14},                        // 1
        {14, 17, 1, 2, 4, 8, 31},                       // 2
        {31, 2, 4, 2, 1, 17, 14},                       // 3
        {2, 6, 10, 18, 31, 2, 2},                       // 4
        {31, 16, 30, 1, 1, 17, 14},                     // 5
        {6, 8, 16, 30, 17, 17, 14},                     // 6
        {31, 1, 2, 4, 8, 8, 8},                         // 7
        {14, 17, 17, 14, 17, 17, 14},                   // 8
        {14, 17, 17, 15, 1, 2, 12},                     // 9
        {0, 12, 12, 0, 12, 12, 0},                      // :
        {0, 12, 12, 0, 12, 4, 8},                       // ;
        {2, 4, 8, 16, 8, 4, 2},                         // <
        {0, 0, 31, 0, 31, 0, 0},                        // =
        {8, 4, 2, 1, 2, 4, 8},                          // >
        {14, 17, 1, 2, 4, 0, 4},                        // ?
        {14, 17, 1, 13, 21, 21, 14},                    // @
        {14, 17, 17, 17, 31, 17, 17},                   // A
        {30, 17, 17, 30, 17, 17, 30},                   // B
        {14, 17, 16, 16, 16, 17, 14},                   // C
        {28, 18, 17, 17, 17, 18, 28},                   // D
        {31, 16, 16, 30, 16, 16, 31},                   // E
        {31, 16, 16, 30, 16, 16, 16},                   // F
        {14, 17, 16, 23, 17, 17, 15},                   // G
        {17, 17, 17, 31, 17, 17, 17},                   // H
        {14, 4, 4, 4, 4, 4, 14},                        // I
        {7, 2, 2, 2, 2, 18, 12},                        // J
        {17, 18, 20, 24, 20, 18, 17},                   // K
        {16, 16, 16, 16, 16, 16, 31},                   // L
        {17, 27, 21, 21, 17, 17, 17},                   // M
        {17, 17, 25, 21, 19, 17, 17},                   // N
        {14, 17, 17, 17, 17, 17, 14},                   // O
        {30, 17, 17, 30, 16, 16, 16},                   // P
        {14, 17, 17, 17, 21, 18, 13},                   // Q
        {30, 17, 17, 30, 20, 18, 17},                   // R
        {15, 16, 16, 14, 1, 1, 30},                     // S
        {31, 4, 4, 4, 4, 4, 4},                         // T
        {17, 17, 17, 17, 17, 17, 14},                   // U
        {17, 17, 17, 17, 17, 10, 4},                    // V
        {17, 17, 17, 21, 21, 21, 10},                   // W
        {17, 17, 10, 4, 10, 17, 17},                    // X
        {17, 17, 17, 10, 4, 4, 4},                      // Y
        {31, 1, 2, 4, 8, 16, 31},                       // Z
        {14, 8, 8, 8, 8, 8, 14},                        // [
        {0, 16, 8, 4, 2, 1, 0},                         // backslash
        {14, 2, 2, 2, 2, 2, 14},                        // ]
        {4, 10, 17, 0, 0, 0, 0},                        // ^
        {0, 0, 0, 0, 0, 0, 31},                         // _
    };
    unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
    if (c < 32 || c > 95) c = '?';
    return table[c - 32];
}

// put(x, y) este apelat pentru fiecare pixel aprins; scale >= 1
template <class Put>
inline void draw_text(int x, int y, const char* text, int scale, Put put) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { y += 9 * scale; cx = x; continue; }
        const uint8_t* g = glyph(*p);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (g[row] & (16 >> col))
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx) put(cx + col * scale + sx, y + row * scale + sy);
        cx += 6 * scale;
    }
}

inline int text_width(const char* text, int scale) {
    return static_cast<int>(std::strlen(text)) * 6 * scale;
}

}
}
#endif
