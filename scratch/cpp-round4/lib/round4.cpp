#include "geom.hpp"
#include "text.hpp"
#include <cstring>
int Rect::area() const { return w * h; }
int quad_code(Quadrant q) { return (int)q; }
unsigned cstr_len(const char* s) { return (unsigned)std::strlen(s); }
const char* pick_word(int i) {
    static const char* words[] = { "alpha", "beta", "gamma" };
    return words[(i < 0 || i > 2) ? 0 : i];
}
const int Config::MAX_ITEMS = 128;
int Config::doubled_n() const { return n * 2; }
int entry_sum(const Config::Entry& e) { return e.key + e.val; }
