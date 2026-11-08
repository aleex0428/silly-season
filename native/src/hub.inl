// The look of the overlay: the drawing kit, the F7 market hub and the breaking-news banner. Included by
// plugin.cpp inside its anonymous namespace, after OverlayData and g_ov. Everything is drawn with the core's
// vector calls plus two textures of our own (data/ui: a soft shadow and a radial glow); when the textures
// can't load, vector stand-ins draw instead.

// ---------------------------------------------------------------------------------------------------
// Drawing kit

namespace ov = swofty::overlay;

namespace ui {

// palette (RGBA)
constexpr uint32_t kRed = 0xE10600FF, kAmber = 0xFFB020FF, kGreen = 0x30D158FF, kBlue = 0x0A84FFFF, kGold = 0xFFD60AFF;
constexpr uint32_t kDanger = 0xFF453AFF, kDangerText = 0xFF6961FF;
constexpr uint32_t kText = 0xF4F5F8FF, kText2 = 0xA9ACBAFF, kMuted = 0x6E7180FF;
constexpr uint32_t kCard = 0x1A1C26FF, kCard2 = 0x14161EFF, kCardHi = 0x232633FF, kLine = 0xFFFFFF14;

bool g_tex_shadow = false, g_tex_glow = false;
// the "Panel effects" option off: no shadows, glows, weave or entrance animations (paint thread only)
bool g_lite = false;

inline void load_textures()
{
    ov::Result a = ov::load_image("ss_shadow", "data/ui/shadow.png");
    ov::Result b = ov::load_image("ss_glow", "data/ui/glow.png");
    g_tex_shadow = a == ov::Result::ok || a == ov::Result::late;
    g_tex_glow = b == ov::Result::ok || b == ov::Result::late;
    swofty::log("[silly-season] overlay textures: shadow %s, glow %s", ov::name(a), ov::name(b));
}

// seconds since the overlay first drew: drives pulses, the ticker and the light sweeps
inline float clock_s()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}
// 0..1..0 once per period
inline float pulse(float period, float phase = 0)
{
    return 0.5f - 0.5f * std::cos((clock_s() / period + phase) * 6.2831853f);
}

inline uint32_t rgb(uint32_t c, uint32_t a = 0xFF) { return (c << 8) | a; }
inline uint32_t alpha(uint32_t rgba, float a)
{
    uint32_t al = static_cast<uint32_t>(std::clamp(a * static_cast<float>(rgba & 0xFF), 0.0f, 255.0f));
    return (rgba & 0xFFFFFF00u) | al;
}
inline uint32_t shade(uint32_t rgba, float f) // darker (f < 1) or lighter (f > 1), alpha kept
{
    auto ch = [&](int s) {
        float v = static_cast<float>((rgba >> s) & 0xFF);
        v = f <= 1 ? v * f : v + (255 - v) * (f - 1);
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f)) << s;
    };
    return ch(24) | ch(16) | ch(8) | (rgba & 0xFF);
}
inline uint32_t ink_on(uint32_t c) // readable text on a team colour (0xRRGGBB)
{
    float lum = 0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF);
    return lum > 165 ? 0x101114FF : 0xFFFFFFFF;
}
inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
inline float ease_back(float t) // a small overshoot
{
    t = std::clamp(t, 0.0f, 1.0f);
    float c1 = 1.4f, c3 = c1 + 1;
    return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
}

inline ov::TextStyle ts(float size, uint32_t rgba = kText, uint32_t align = ov::align::left | ov::align::top,
                        float tracking = 0, float max_width = 0)
{
    ov::TextStyle s;
    s.size = size;
    s.rgba = rgba;
    s.align = align;
    s.tracking = tracking;
    s.max_width = max_width;
    return s;
}
// the same style with a soft drop shadow, for headings over colour
inline ov::TextStyle lifted(ov::TextStyle s, float k, uint32_t shadow = 0x000000A0)
{
    s.shadow = shadow;
    s.shadow_dx = 0;
    s.shadow_dy = 2 * k;
    s.shadow_blur = 6 * k;
    return s;
}

// text bounds, remembered: the same strings are measured every frame (the ticker, cut names, wrapped
// cards), and a measure only depends on the style and the text. Paint thread only.
inline void measured(ov::Paint *p, const ov::TextStyle &s, std::string_view t, float *w, float *h)
{
    static std::unordered_map<std::string, std::pair<float, float>> cache;
    char head[64];
    std::snprintf(head, sizeof head, "%.2f|%.2f|%.1f|%.2f|", s.size, s.tracking, s.max_width, s.line_height);
    std::string key(head);
    key.append(t);
    if (auto it = cache.find(key); it != cache.end()) {
        *w = it->second.first;
        *h = it->second.second;
        return;
    }
    ov::measure(p, s, t, w, h);
    if (cache.size() > 6000)
        cache.clear();
    cache.emplace(std::move(key), std::make_pair(*w, *h));
}

inline float text_w(ov::Paint *p, const ov::TextStyle &s, std::string_view t)
{
    float w = 0, h = 0;
    measured(p, s, t, &w, &h);
    return w;
}
inline float text_h(ov::Paint *p, const ov::TextStyle &s, std::string_view t)
{
    float w = 0, h = 0;
    measured(p, s, t, &w, &h);
    return h;
}

// text cut to a width with "..."
inline std::string fit(ov::Paint *p, const ov::TextStyle &s, std::string t, float max_w)
{
    if (text_w(p, s, t) <= max_w)
        return t;
    while (!t.empty() && text_w(p, s, t + "...") > max_w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) // whole UTF-8 characters
            t.pop_back();
    }
    return t + "...";
}

// ----- light and depth ----------------------------------------------------------------------------

// a soft shadow under a rounded box (drawn before the box)
inline void shadow(ov::Paint *p, float x, float y, float w, float h, float k, float strength = 1, uint32_t col = 0x000000FF,
                   float drop = 6)
{
    if (g_lite)
        return;
    if (g_tex_shadow) {
        float s = k, m = 40 * s;
        ov::image_nine(p, "ss_shadow", x - m, y - m + drop * k, w + 2 * m, h + 2 * m, 48, 48, 48, 48, s,
                       alpha(col, 0.55f * strength));
        return;
    }
    for (int i = 4; i >= 1; --i) {
        float g = i * 3.5f * k;
        ov::rect_rounded(p, x - g, y - g + drop * k, w + 2 * g, h + 2 * g, 12 * k + g, alpha(col, 0.07f * strength));
    }
}

// a soft round light of radius r (additive look on the dark panel)
inline void glow(ov::Paint *p, float cx, float cy, float r, uint32_t col)
{
    if (g_lite || r <= 0 || (col & 0xFF) == 0)
        return;
    if (g_tex_glow) {
        ov::image(p, "ss_glow", cx - r, cy - r, 2 * r, 2 * r, col);
        return;
    }
    for (int i = 1; i <= 5; ++i)
        ov::circle(p, cx, cy, r * (1.0f - (i - 1) * 0.19f), alpha(col, 0.16f));
}

// ----- shapes -------------------------------------------------------------------------------------

// a parallelogram leaning right by skew
inline void para(ov::Paint *p, float x, float y, float w, float h, float skew, uint32_t col)
{
    float pts[8] = {x + skew, y, x + w, y, x + w - skew, y + h, x, y + h};
    ov::polygon(p, pts, 4, col);
}

// diagonal hatching across a box (clip it first): the carbon weave behind headers
inline void hatch(ov::Paint *p, float x, float y, float w, float h, float k, uint32_t col, float step)
{
    if (g_lite)
        return;
    for (float o = -h; o < w; o += step)
        ov::line(p, x + o, y + h, x + o + h, y, 1.0f * k, col);
}

// a box outline made of dashes
inline void dashed_rect(ov::Paint *p, float x, float y, float w, float h, float k, uint32_t col, float dash = 7, float gap = 5)
{
    float d = dash * k, g = gap * k, t = 1.2f * k;
    for (float o = 0; o < w; o += d + g) {
        float l = std::min(d, w - o);
        ov::rect(p, x + o, y, l, t, col);
        ov::rect(p, x + o, y + h - t, l, t, col);
    }
    for (float o = 0; o < h; o += d + g) {
        float l = std::min(d, h - o);
        ov::rect(p, x, y + o, t, l, col);
        ov::rect(p, x + w - t, y + o, t, l, col);
    }
}

// a rounded pill with a label; returns its width
inline float pill(ov::Paint *p, float x, float y, std::string_view label, uint32_t fg, uint32_t bg, float k, float size = 13)
{
    ov::TextStyle s = ts(size * k, fg, ov::align::left | ov::align::top, 0.8f * k);
    float w = text_w(p, s, label) + 16 * k, h = (size + 9) * k;
    ov::rect_rounded(p, x, y, w, h, h / 2, bg);
    ov::text(p, x + 8 * k, y + 4.5f * k, s, label);
    return w;
}

// a slanted broadcast label: tinted fill, a solid nose and the text; returns its width
inline float tag(ov::Paint *p, float x, float y, std::string_view label, uint32_t col, float k, float size = 12, bool solid = false)
{
    ov::TextStyle s = ts(size * k, solid ? ink_on(col >> 8) : shade(col, 1.35f), ov::align::left | ov::align::middle, 1.3f * k);
    float h = (size + 10) * k, sk = h * 0.32f, w = text_w(p, s, label) + 22 * k + sk;
    para(p, x, y, w, h, sk, solid ? col : alpha(col, 0.20f));
    if (!solid)
        para(p, x, y, 4 * k + sk, h, sk, col);
    ov::text(p, x + sk + (solid ? 9 : 11) * k, y + h / 2, s, label);
    return w;
}

// a team badge: a slanted block in the team colour with its three-letter code; returns its width
inline float team_chip(ov::Paint *p, float x, float y, std::string_view code, uint32_t col, float k, float h = 24)
{
    ov::TextStyle s = ts(13 * k, ink_on(col), ov::align::center | ov::align::middle, 1.2f * k);
    float hh = h * k, sk = hh * 0.3f;
    float w = std::max(56 * k, text_w(p, s, code) + 22 * k + sk);
    uint32_t c = rgb(col);
    para(p, x, y, w, hh, sk, c);
    para(p, x + sk * 0.5f, y, w - sk * 0.5f, hh * 0.45f, sk * 0.45f, 0xFFFFFF22); // the sheen on top
    para(p, x + w - sk - 7 * k, y, sk + 7 * k, hh, sk, shade(c, 0.62f));         // a darker tail stripe
    ov::text(p, x + (w - 7 * k) / 2, y + hh / 2, s, code.empty() ? "---" : code);
    return w;
}

// a progress bar with a glowing head
inline void bar(ov::Paint *p, float x, float y, float w, float h, float frac, uint32_t col)
{
    ov::rect_rounded(p, x, y, w, h, h / 2, 0xFFFFFF12);
    float f = std::clamp(frac, 0.0f, 1.0f);
    if (f <= 0)
        return;
    float fw = std::max(h, w * f);
    ov::gradient(p, x, y, fw, h, alpha(col, 0.35f), col, true, h / 2);
    glow(p, x + fw - h / 2, y + h / 2, h * 2.6f, alpha(col, 0.55f));
    ov::circle(p, x + fw - h / 2, y + h / 2, h * 0.32f, 0xFFFFFFCC);
}

// n slanted segments, the first `on` lit
inline void meter(ov::Paint *p, float x, float y, int n, int on, uint32_t col, float k, float sw = 14, float sh = 6)
{
    for (int i = 0; i < n; ++i)
        para(p, x + i * (sw + 3) * k, y, sw * k, sh * k, 3 * k, i < on ? col : 0xFFFFFF14);
}

inline void triangle(ov::Paint *p, float cx, float cy, float s, bool up, uint32_t col)
{
    float pts[6] = {cx - s, up ? cy + s * 0.6f : cy - s * 0.6f, cx + s, up ? cy + s * 0.6f : cy - s * 0.6f, cx,
                    up ? cy - s * 0.8f : cy + s * 0.8f};
    ov::polygon(p, pts, 3, col);
}

inline void arrow_right(ov::Paint *p, float x, float cy, float len, float k, uint32_t col)
{
    ov::rect(p, x, cy - 1 * k, len - 6 * k, 2 * k, col);
    float pts[6] = {x + len - 8 * k, cy - 5 * k, x + len, cy, x + len - 8 * k, cy + 5 * k};
    ov::polygon(p, pts, 3, col);
}

// an arrow of moving dashes: the faster, the hotter the story
inline void flow_arrow(ov::Paint *p, float x, float cy, float len, float k, uint32_t col, float speed)
{
    float d = 7 * k, g = 5 * k, body = len - 10 * k;
    float off = std::fmod(clock_s() * speed * k, d + g);
    ov::push_clip(p, x, cy - 3 * k, body, 6 * k);
    for (float o = -(d + g) + off; o < body; o += d + g)
        ov::rect_rounded(p, x + o, cy - 1.5f * k, d, 3 * k, 1.5f * k, col);
    ov::pop_clip(p);
    float pts[6] = {x + len - 11 * k, cy - 6 * k, x + len, cy, x + len - 11 * k, cy + 6 * k};
    ov::polygon(p, pts, 3, col);
}

// a keyboard / pad key cap; returns its width
inline float keycap(ov::Paint *p, float x, float y, std::string_view label, float k)
{
    ov::TextStyle s = ts(12.5f * k, kText, ov::align::center | ov::align::middle, 0.5f * k);
    float w = std::max(26 * k, text_w(p, s, label) + 14 * k), h = 24 * k;
    ov::rect_rounded(p, x, y + 2 * k, w, h, 5 * k, 0x00000080);
    ov::gradient(p, x, y, w, h, 0x353947FF, 0x262935FF, false, 5 * k);
    ov::rect_rounded_outline(p, x, y, w, h, 5 * k, 1 * k, 0xFFFFFF1E);
    ov::text(p, x + w / 2, y + h / 2, s, label);
    return w;
}

// a raised card: shadow, a gentle top-lit fill, a hairline border and a highlight along the top edge
inline void card(ov::Paint *p, float x, float y, float w, float h, float k, bool hover = false, uint32_t accent = 0)
{
    float r = 12 * k;
    shadow(p, x, y, w, h, k, hover ? 1.3f : 1.0f);
    if (hover && accent)
        shadow(p, x, y, w, h, k, 0.5f, accent, 0);
    ov::gradient(p, x, y, w, h, hover ? 0x252836FF : kCard, hover ? 0x1A1C26FF : kCard2, false, r);
    ov::rect_rounded_outline(p, x, y, w, h, r, 1 * k, hover ? (accent ? alpha(accent, 0.6f) : 0xFFFFFF30) : 0xFFFFFF12);
    ov::gradient(p, x + r, y + 1 * k, (w - 2 * r) / 2, 1 * k, 0xFFFFFF00, 0xFFFFFF22, true);
    ov::gradient(p, x + w / 2, y + 1 * k, (w - 2 * r) / 2, 1 * k, 0xFFFFFF22, 0xFFFFFF00, true);
}

// a team's livery band: its colour fading out, with racing stripes
// (radius rounds the top corners; the caller clips the band to h)
inline void livery(ov::Paint *p, float x, float y, float w, float h, uint32_t col, float k, float strength = 1, float radius = 0)
{
    uint32_t c = rgb(col);
    ov::gradient(p, x, y, w, h + radius, alpha(c, 0.95f * strength), alpha(shade(c, 0.5f), 0.25f * strength), true, radius);
    float sx = x + w * 0.62f;
    for (int i = 0; i < 3; ++i)
        para(p, sx + i * 26 * k, y, 14 * k, h, h * 0.55f, alpha(0xFFFFFFFF, 0.07f * strength));
    ov::gradient(p, x, y + h * 0.55f, w, h * 0.45f, 0x00000000, alpha(0x00000055, strength));
}

// five dots for how reliable an outlet is
inline void dots(ov::Paint *p, float x, float cy, int n, float k)
{
    uint32_t col = n >= 4 ? kGreen : n == 3 ? kAmber : kDanger;
    for (int i = 0; i < 5; ++i)
        ov::circle(p, x + i * 11 * k + 4 * k, cy, 3.5f * k, i < n ? col : 0xFFFFFF1F);
}

// a dot that breathes, with a ring that grows out of it
inline void live_dot(ov::Paint *p, float cx, float cy, float r, uint32_t col, float period = 1.6f)
{
    float t = std::fmod(clock_s(), period) / period;
    ov::circle_outline(p, cx, cy, r * (1 + 1.6f * t), std::max(1.0f, r * 0.35f), alpha(col, 0.7f * (1 - t)));
    glow(p, cx, cy, r * 3.2f, alpha(col, 0.35f + 0.25f * pulse(period)));
    ov::circle(p, cx, cy, r, col);
}

// the side menu's icons, drawn in a box of side s centred on (cx, cy)
enum class Icon { paddock, news, press, rumours, deals, grid, drivers, contracts, stock, f2, staff, free, history };

inline void icon(ov::Paint *p, Icon i, float cx, float cy, float s, uint32_t c, float k)
{
    float h = s / 2, t = std::max(1.5f, 1.8f * k);
    switch (i) {
    case Icon::paddock:
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
                ov::rect_rounded(p, cx - h + a * (h + 1.5f * k), cy - h + b * (h + 1.5f * k), h - 1.5f * k, h - 1.5f * k,
                                 2 * k, c);
        break;
    case Icon::news:
        ov::rect_rounded_outline(p, cx - h, cy - h * 0.8f, s, s * 0.8f, 2 * k, t, c);
        for (int r = 0; r < 3; ++r)
            ov::rect(p, cx - h * 0.6f, cy - h * 0.4f + r * h * 0.4f, r == 0 ? h * 1.2f : h * 0.9f, t, c);
        break;
    case Icon::press:
        ov::rect_rounded(p, cx - h * 0.35f, cy - h, h * 0.7f, h * 1.15f, h * 0.35f, c);
        ov::arc(p, cx, cy - h * 0.15f, h * 0.65f, 0.0f, 3.14159f, t, c);
        ov::rect(p, cx - t / 2, cy + h * 0.5f, t, h * 0.4f, c);
        ov::rect(p, cx - h * 0.4f, cy + h * 0.85f, h * 0.8f, t, c);
        break;
    case Icon::rumours: {
        ov::rect_rounded(p, cx - h, cy - h * 0.8f, s, h * 1.3f, 4 * k, c);
        float pts[6] = {cx - h * 0.5f, cy + h * 0.45f, cx - h * 0.05f, cy + h * 0.45f, cx - h * 0.6f, cy + h};
        ov::polygon(p, pts, 3, c);
        for (int r = 0; r < 3; ++r)
            ov::circle(p, cx - h * 0.45f + r * h * 0.45f, cy - h * 0.15f, std::max(1.2f, h * 0.12f), 0x0E0F14FF);
        break;
    }
    case Icon::deals: {
        ov::circle_outline(p, cx, cy, h, t, c);
        float pts[6] = {cx - h * 0.45f, cy, cx - h * 0.1f, cy + h * 0.35f, cx + h * 0.5f, cy - h * 0.35f};
        ov::polyline(p, pts, 3, t * 1.2f, c);
        break;
    }
    case Icon::grid:
        for (int r = 0; r < 3; ++r) {
            ov::rect_rounded(p, cx - h, cy - h + r * h * 0.72f, h * 0.8f, h * 0.45f, 2 * k, c);
            ov::rect_rounded(p, cx + h * 0.2f, cy - h + r * h * 0.72f + h * 0.3f, h * 0.8f, h * 0.45f, 2 * k, c);
        }
        break;
    case Icon::drivers:
        ov::circle(p, cx, cy - h * 0.35f, h * 0.45f, c);
        ov::arc(p, cx, cy + h * 0.95f, h * 0.85f, 3.14159f, 6.28318f, t * 1.4f, c);
        break;
    case Icon::contracts:
        ov::rect_rounded_outline(p, cx - h * 0.8f, cy - h, h * 1.6f, s, 2 * k, t, c);
        for (int r = 0; r < 3; ++r)
            ov::rect(p, cx - h * 0.45f, cy - h * 0.45f + r * h * 0.42f, h * 0.9f, t, c);
        break;
    case Icon::stock: {
        float pts[8] = {cx - h, cy + h * 0.6f, cx - h * 0.3f, cy - h * 0.05f, cx + h * 0.15f, cy + h * 0.3f, cx + h, cy - h * 0.6f};
        ov::polyline(p, pts, 4, t * 1.2f, c);
        ov::circle(p, cx + h, cy - h * 0.6f, std::max(2.2f * k, h * 0.14f), c);
        break;
    }
    case Icon::f2: {
        ov::TextStyle st = ts(s * 0.62f, c, ov::align::center | ov::align::middle);
        ov::rect_rounded_outline(p, cx - h, cy - h * 0.75f, s, h * 1.5f, 3 * k, t, c);
        ov::text(p, cx, cy, st, "F2");
        break;
    }
    case Icon::staff:
        ov::circle_outline(p, cx, cy, h * 0.55f, t * 1.3f, c);
        for (int a = 0; a < 8; ++a) {
            float ang = a * 0.785398f, x0 = cx + std::cos(ang) * h * 0.62f, y0 = cy + std::sin(ang) * h * 0.62f,
                  x1 = cx + std::cos(ang) * h * 0.95f, y1 = cy + std::sin(ang) * h * 0.95f;
            ov::line(p, x0, y0, x1, y1, t * 1.4f, c);
        }
        break;
    case Icon::free:
        ov::circle_outline(p, cx - h * 0.2f, cy - h * 0.35f, h * 0.42f, t, c);
        ov::arc(p, cx - h * 0.2f, cy + h * 0.95f, h * 0.75f, 3.14159f, 6.28318f, t, c);
        ov::rect(p, cx + h * 0.45f, cy - h * 0.1f, h * 0.55f, t, c);
        ov::rect(p, cx + h * 0.72f - t / 2, cy - h * 0.37f, t, h * 0.55f, c);
        break;
    case Icon::history:
        ov::circle_outline(p, cx, cy, h, t, c);
        ov::line(p, cx, cy, cx, cy - h * 0.6f, t, c);
        ov::line(p, cx, cy, cx + h * 0.45f, cy + h * 0.2f, t, c);
        break;
    }
}

// the Silly Season mark: a red slanted badge with two white strokes
inline void logo(ov::Paint *p, float x, float y, float h, float k)
{
    float w = h * 1.45f, sk = h * 0.42f;
    glow(p, x + w / 2, y + h / 2, h * 1.6f, 0xE1060050);
    para(p, x, y, w, h, sk, kRed);
    para(p, x + sk * 0.5f, y, w - sk * 0.5f, h * 0.45f, sk * 0.45f, 0xFFFFFF1A);
    float sw = h * 0.16f;
    para(p, x + w * 0.30f, y + h * 0.2f, sw + h * 0.6f * 0.42f, h * 0.6f, h * 0.6f * 0.42f, 0xFFFFFFFF);
    para(p, x + w * 0.52f, y + h * 0.2f, sw + h * 0.6f * 0.42f, h * 0.6f, h * 0.6f * 0.42f, 0xFFFFFFB0);
}

// what a news item is, as a label and a colour
struct Tag {
    const char *es, *en;
    uint32_t col;
};
inline Tag news_tag(const std::string &k)
{
    if (k == "rumour") return {"RUMOR", "RUMOUR", 0x8E9AAFFF};
    if (k == "talks") return {"NEGOCIACIÓN", "TALKS", kAmber};
    if (k == "official") return {"OFICIAL", "OFFICIAL", kGreen};
    if (k == "retirement") return {"RETIRADA", "RETIREMENT", 0xBF5AF2FF};
    if (k == "sacking") return {"DESPIDO", "SACKED", kDanger};
    if (k == "collapse") return {"SE CAE", "DEAL OFF", 0xFF9F0AFF};
    if (k == "pressure") return {"PRESIÓN", "PRESSURE", 0xFF6B6BFF};
    if (k == "player") return {"TU FUTURO", "YOUR FUTURE", kRed};
    if (k == "press" || k == "press_reply") return {"PRENSA", "PRESS", kAmber};
    if (k == "standin" || k == "standin_return") return {"SUSTITUTO", "STAND-IN", 0x40C8E0FF};
    if (k == "staff") return {"TÉCNICOS", "ENGINEERS", 0x5E5CE6FF};
    if (k == "clause") return {"CLÁUSULA", "CLAUSE", 0xFF375FFF};
    if (k == "feud") return {"RIVALIDAD", "FEUD", 0xFF375FFF};
    if (k == "talents") return {"PROMESAS", "TALENTS", 0x32D74BFF};
    if (k == "f2") return {"F2", "F2", 0x64D2FFFF};
    if (k == "grid" || k == "summary" || k == "review" || k == "preseason") return {"TEMPORADA", "SEASON", kBlue};
    return {"MERCADO", "MARKET", 0xA0A3B1FF};
}

inline Tag move_tag(const std::string &k)
{
    if (k == "transfer") return {"FICHAJE", "TRANSFER", kGreen};
    if (k == "renewal") return {"RENUEVA", "RENEWAL", kBlue};
    if (k == "promotion") return {"ASCENSO", "PROMOTION", 0x64D2FFFF};
    if (k == "comeback") return {"REGRESO", "COMEBACK", 0xBF5AF2FF};
    if (k == "retirement") return {"RETIRADA", "RETIRED", 0xBF5AF2FF};
    if (k == "sacked") return {"DESPEDIDO", "SACKED", kDanger};
    if (k == "replaced") return {"SUSTITUIDO", "REPLACED", 0xFF9F0AFF};
    return {"SALE", "LEFT", 0xA0A3B1FF};
}

} // namespace ui

// ---------------------------------------------------------------------------------------------------
// The market hub (F7)

namespace paint_state {
using Clock = std::chrono::steady_clock;
int tab = 0;
float scroll = 0, scroll_target = 0, content_h = 0;
float menu_y = -1; // the sliding highlight in the side menu
int press_sel = 0;
Clock::time_point last_frame, opened_at, tab_at;
bool banner_on = false;
Clock::time_point banner_at;
ss::NewsItem banner;
}

void choose_answer(int i)
{
    g_press_answer_id = g_ov.press_id;
    g_press_from_hub = false;
    g_press_answer = i;
    g_ov.press_pending = false; // until the flow thread rebuilds the page
}

struct PageDef {
    ui::Icon icon;
    const char *es, *en;
    const char *desc_es, *desc_en;
    uint32_t accent;
};
constexpr PageDef kPages[] = {
    {ui::Icon::paddock, "Paddock", "Paddock", "El mercado de un vistazo", "The market at a glance", ui::kRed},
    {ui::Icon::news, "Noticias", "News", "Todo lo que se cuenta en el paddock, lo último primero",
     "Every story from the paddock, newest first", ui::kBlue},
    {ui::Icon::press, "Prensa", "Press", "Los periodistas quieren tu opinión sobre el mercado",
     "The journalists want your take on the market", ui::kAmber},
    {ui::Icon::rumours, "Rumores", "Rumours", "Quién suena para dónde y quién lo dice", "Who is linked where, and who says so",
     0xBF5AF2FF},
    {ui::Icon::deals, "Fichajes", "Deals", "Contratos firmados y los movimientos de la temporada",
     "Contracts signed and this season's moves", ui::kGreen},
    {ui::Icon::grid, "Parrilla", "Grid", "La parrilla del año que viene, equipo a equipo", "Next season's line-up, team by team",
     ui::kRed},
    {ui::Icon::drivers, "Pilotos", "Drivers", "Todos los pilotos de la parrilla por valor de mercado",
     "Every driver on the grid, ranked by market value", 0x64D2FFFF},
    {ui::Icon::contracts, "Contratos", "Contracts", "Cuándo termina el contrato de cada piloto",
     "When every driver's deal runs out", ui::kAmber},
    {ui::Icon::stock, "Bolsa", "Stock", "Quién ha subido y quién ha bajado en la última carrera",
     "Whose value moved at the last race", ui::kGreen},
    {ui::Icon::f2, "Fórmula 2", "Formula 2", "El campeonato donde la F1 busca a sus próximas estrellas",
     "Where F1 finds its next stars", 0x64D2FFFF},
    {ui::Icon::staff, "Técnicos", "Engineers", "Los directores técnicos detrás de cada coche",
     "The technical directors behind every car", 0x5E5CE6FF},
    {ui::Icon::free, "Agentes libres", "Free agents", "Pilotos sin asiento, ordenados por potencial",
     "Drivers without a seat, ranked by potential", 0x40C8E0FF},
    {ui::Icon::history, "Historial", "History", "Cada movimiento que ha hecho el mercado", "Every move the market has made",
     0xA0A3B1FF},
};
constexpr int kPageCount = static_cast<int>(sizeof kPages / sizeof kPages[0]);
enum PageId { P_PADDOCK, P_NEWS, P_PRESS, P_RUMOURS, P_DEALS, P_GRID, P_DRIVERS, P_CONTRACTS, P_STOCK, P_F2, P_STAFF, P_FREE, P_HISTORY };

// the side menu's groups: the first page of each
struct MenuGroup {
    int first;
    const char *es, *en;
};
constexpr MenuGroup kGroups[] = {
    {P_PADDOCK, "MERCADO", "MARKET"}, {P_GRID, "PARRILLA Y PILOTOS", "GRID & DRIVERS"}, {P_F2, "CANTERA", "TALENT"},
    {P_HISTORY, "ARCHIVO", "ARCHIVE"}};

// what every page draw gets: where to draw, the input, the scale and how long the page has been open
struct Ctx {
    ov::Paint *p;
    const ov::Input &in;
    ov::Rect area; // the visible, clipped content area
    float k;
    bool es;
    float t;           // seconds since the page opened
    mutable int seq = 0; // blocks drawn so far, for the staggered entrance
    const char *T(const char *es_text, const char *en_text) const { return es ? es_text : en_text; }
    bool visible(float y, float h) const { return y + h >= area.y && y <= area.y + area.h; }
    bool hover(float x, float y, float w, float h) const
    {
        return in.inside(x, y, w, h) && in.inside(area.x, area.y, area.w, area.h);
    }
    // 0..1: bars and gauges fill up as the page opens
    float grow() const { return ui::ease_out((t - 0.12f) / 0.75f); }
    int count(int v) const { return static_cast<int>(std::lround(v * ui::ease_out((t - 0.05f) / 0.8f))); }
};

// a block slides up and fades in, one after another
struct Reveal {
    ov::Paint *p;
    bool on = false;
    explicit Reveal(const Ctx &c, float rise = 18)
        : p(c.p)
    {
        int i = std::min(c.seq++, 14);
        if (ui::g_lite)
            return;
        float a = ui::ease_out((c.t - 0.03f - 0.045f * static_cast<float>(i)) / 0.34f);
        if (a < 0.999f) {
            on = true;
            ov::push_opacity(p, a);
            ov::push_translate(p, 0, (1 - a) * rise * c.k);
        }
    }
    ~Reveal()
    {
        if (on) {
            ov::pop_transform(p);
            ov::pop_opacity(p);
        }
    }
    Reveal(const Reveal &) = delete;
    Reveal &operator=(const Reveal &) = delete;
};

float section_title(const Ctx &c, float x, float y, std::string_view title, std::string_view note = {}, float w = 0)
{
    float k = c.k;
    if (w <= 0)
        w = c.area.x + c.area.w - x;
    ui::para(c.p, x, y + 3 * k, 9 * k, 14 * k, 4 * k, ui::kRed);
    ov::TextStyle s = ui::ts(14 * k, ui::kText, ov::align::left | ov::align::top, 2.0f * k);
    ov::text(c.p, x + 18 * k, y + 1 * k, s, title);
    float tw = ui::text_w(c.p, s, title);
    float nw = 0;
    if (!note.empty()) {
        ov::TextStyle ns = ui::ts(13 * k, ui::kMuted, ov::align::right | ov::align::top, 0.4f * k);
        ov::text(c.p, x + w, y + 2 * k, ns, note);
        nw = ui::text_w(c.p, ns, note) + 16 * k;
    }
    float lx = x + 18 * k + tw + 14 * k, lw = x + w - nw - lx;
    if (lw > 10 * k)
        ov::gradient(c.p, lx, y + 10 * k, lw, 1 * k, 0xFFFFFF22, 0xFFFFFF04, true);
    return 34 * k;
}

float empty_state(const Ctx &c, float y, ui::Icon ic, std::string_view line1, std::string_view line2)
{
    float k = c.k, h = 280 * k, cx = c.area.x + c.area.w / 2;
    Reveal r(c);
    ui::card(c.p, c.area.x, y, c.area.w, h, k);
    ui::glow(c.p, cx, y + 96 * k, 130 * k, 0xFFFFFF14);
    ov::circle(c.p, cx, y + 96 * k, 46 * k, 0xFFFFFF0A);
    ov::circle_outline(c.p, cx, y + 96 * k, 46 * k, 1.5f * k, 0xFFFFFF1C);
    ov::arc(c.p, cx, y + 96 * k, 46 * k, -1.5708f + ui::clock_s() * 0.8f, -0.4f + ui::clock_s() * 0.8f, 2 * k, 0xFFFFFF40);
    ui::icon(c.p, ic, cx, y + 96 * k, 38 * k, ui::kText2, k);
    ov::text(c.p, cx, y + 166 * k, ui::ts(23 * k, ui::kText, ov::align::center | ov::align::top), line1);
    ov::text(c.p, cx, y + 202 * k, ui::ts(16 * k, ui::kText2, ov::align::center | ov::align::top, 0, c.area.w - 80 * k), line2);
    return h;
}

// one news card; returns its height. hero: the big top story on the Paddock page
float news_card(const Ctx &c, float x, float y, float w, const UiNews &n, bool compact, bool hero = false)
{
    float k = c.k, pad = 20 * k;
    ui::Tag tag = ui::news_tag(n.kind);
    float title_px = hero ? 27.0f : compact ? 18.5f : 20.5f, body_px = hero ? 17.0f : compact ? 15.0f : 16.5f;
    ov::TextStyle title = ui::ts(title_px * k, ui::kText, ov::align::left | ov::align::top, 0, w - 2 * pad - 10 * k);
    ov::TextStyle body = ui::ts(body_px * k, ui::kText2, ov::align::left | ov::align::top, 0, w - 2 * pad - 10 * k);
    body.line_height = 1.32f;
    std::string text = n.body;
    size_t cut = hero ? 240 : compact ? 150 : std::string::npos;
    if (text.size() > cut) {
        text = text.substr(0, cut);
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
            text.pop_back();
        text += "...";
    }
    float th = ui::text_h(c.p, title, n.title), bh = ui::text_h(c.p, body, text);
    float h = pad + 22 * k + 12 * k + th + 8 * k + bh + pad + (hero ? 10 * k : 0);
    if (!c.visible(y, h))
        return h;
    Reveal r(c);
    bool hov = c.hover(x, y, w, h);
    ui::card(c.p, x, y, w, h, k, hov, tag.col);
    {
        ov::ClipScope clip(c.p, x, y, w, h);
        ov::gradient(c.p, x, y, w * (hero ? 0.75f : 0.45f), h, ui::alpha(tag.col, hero ? 0.20f : 0.09f), ui::alpha(tag.col, 0), true, 12 * k);
        if (hero) {
            ui::glow(c.p, x + w - 60 * k, y + 20 * k, 260 * k, ui::alpha(tag.col, 0.22f));
            ui::hatch(c.p, x + w * 0.55f, y, w * 0.45f, h, k, 0xFFFFFF05, 10 * k);
        }
    }
    ov::rect_rounded(c.p, x, y + 14 * k, 3.5f * k, h - 28 * k, 2 * k, tag.col);
    ui::glow(c.p, x + 2 * k, y + h / 2, 24 * k, ui::alpha(tag.col, 0.35f));
    float px = x + pad + 10 * k, tx = px;
    if (hero)
        tx += ui::tag(c.p, tx, y + pad, c.T("PORTADA", "TOP STORY"), ui::kRed, k, 12, true) + 6 * k;
    float tw = ui::tag(c.p, tx, y + pad, c.T(tag.es, tag.en), tag.col, k, 12);
    char when[48];
    if (n.round == 0)
        std::snprintf(when, sizeof when, "%s  ·  %d", c.T("PRETEMPORADA", "PRE-SEASON"), n.year);
    else
        std::snprintf(when, sizeof when, "%s %d  ·  %d", c.T("CARRERA", "ROUND"), n.round, n.year);
    ov::text(c.p, tx + tw + 12 * k, y + pad + 11 * k, ui::ts(12.5f * k, ui::kMuted, ov::align::left | ov::align::middle, 1.2f * k), when);
    if (n.unread) {
        ui::live_dot(c.p, x + w - pad - 4 * k, y + pad + 11 * k, 4.5f * k, ui::kRed);
        ov::text(c.p, x + w - pad - 18 * k, y + pad + 11 * k, ui::ts(12 * k, 0xFF6A63FF, ov::align::right | ov::align::middle, 1.6f * k),
                 c.T("NUEVO", "NEW"));
    }
    float ty = y + pad + 34 * k;
    ov::text(c.p, px, ty, hero ? ui::lifted(title, k) : title, n.title);
    ov::text(c.p, px, ty + th + 8 * k, body, text);
    return h;
}

float page_news(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    if (d.news.empty())
        return empty_state(c, y0, ui::Icon::news, c.T("Todavía no hay noticias", "No news yet"),
                           c.T("El paddock empieza a hablar después de las primeras carreras.",
                               "The paddock starts talking after the first races."));
    float k = c.k, y = y0;
    // a timeline: one node per race, the cards hanging off it
    float lx = c.area.x + 14 * k, cx = c.area.x + 44 * k, cw = c.area.w - 44 * k;
    int last_round = -1, last_year = -1;
    float line_top = y0 + 14 * k;
    for (const UiNews &n : d.news) {
        if (n.round != last_round || n.year != last_year) {
            char head[64];
            if (n.round == 0)
                std::snprintf(head, sizeof head, "%s %d", c.T("PRETEMPORADA", "PRE-SEASON"), n.year);
            else
                std::snprintf(head, sizeof head, "%s %d  ·  %d", c.T("DESPUÉS DE LA CARRERA", "AFTER ROUND"), n.round, n.year);
            y += (last_round < 0 ? 0 : 12 * k);
            if (c.visible(y, 34 * k)) {
                ov::circle(c.p, lx, y + 11 * k, 7 * k, 0x0B0C11FF);
                ov::circle_outline(c.p, lx, y + 11 * k, 7 * k, 2 * k, ui::kBlue);
                ui::glow(c.p, lx, y + 11 * k, 22 * k, 0x0A84FF55);
                section_title(c, cx, y, head, {}, cw);
            }
            y += 34 * k;
            last_round = n.round;
            last_year = n.year;
        }
        float h = news_card(c, cx, y, cw, n, false);
        if (c.visible(y, h)) {
            ov::circle(c.p, lx, y + 30 * k, 3.5f * k, ui::news_tag(n.kind).col);
            ov::rect(c.p, lx + 4 * k, y + 29.5f * k, cx - lx - 8 * k, 1 * k, 0xFFFFFF20);
        }
        y += h + 12 * k;
    }
    ov::rect(c.p, lx - 0.75f * k, std::max(line_top, c.area.y), 1.5f * k, std::max(0.0f, std::min(y, c.area.y + c.area.h) - std::max(line_top, c.area.y)),
             0xFFFFFF14);
    return y - y0;
}

float kpi(const Ctx &c, float x, float y, float w, float h, int value, const char *label, const char *hint, uint32_t col, ui::Icon ic,
          int lit)
{
    float k = c.k;
    Reveal r(c);
    bool hov = c.hover(x, y, w, h);
    ui::card(c.p, x, y, w, h, k, hov, col);
    {
        ov::ClipScope clip(c.p, x, y, w, h);
        ov::gradient(c.p, x, y, w, h, ui::alpha(col, 0.14f), 0x00000000, false, 12 * k);
        ui::glow(c.p, x + w - 30 * k, y + 10 * k, 170 * k, ui::alpha(col, 0.30f));
        ui::hatch(c.p, x + w * 0.5f, y, w * 0.5f, h, k, 0xFFFFFF05, 9 * k);
    }
    ov::gradient(c.p, x + 12 * k, y, w * 0.6f, 3 * k, col, ui::alpha(col, 0), true, 1.5f * k);
    ov::rect_rounded(c.p, x + w - 62 * k, y + 18 * k, 44 * k, 44 * k, 10 * k, ui::alpha(col, 0.18f));
    ov::rect_rounded_outline(c.p, x + w - 62 * k, y + 18 * k, 44 * k, 44 * k, 10 * k, 1 * k, ui::alpha(col, 0.35f));
    ui::icon(c.p, ic, x + w - 40 * k, y + 40 * k, 20 * k, ui::shade(col, 1.25f), k);
    ov::text(c.p, x + 22 * k, y + 8 * k, ui::lifted(ui::ts(54 * k, ui::kText), k), std::to_string(c.count(value)));
    ov::text(c.p, x + 22 * k, y + h - 48 * k, ui::ts(13 * k, ui::kText, ov::align::left | ov::align::top, 1.8f * k), label);
    ov::text(c.p, x + 22 * k, y + h - 27 * k, ui::ts(12.5f * k, ui::kMuted), ui::fit(c.p, ui::ts(12.5f * k), hint, w - 44 * k));
    ui::meter(c.p, x + w - 20 * k - 8 * 13 * k, y + h - 44 * k, 8, static_cast<int>(std::lround(lit * c.grow())), col, k, 10, 6);
    return h;
}

float page_paddock(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, gap = 16 * k, y = y0;
    // the numbers of the market
    float kw = (c.area.w - 3 * gap) / 4, kh = 124 * k;
    if (c.visible(y, kh)) {
        auto lit = [](int v, int full) { return std::clamp(v * 8 / std::max(1, full), v > 0 ? 1 : 0, 8); };
        kpi(c, c.area.x, y, kw, kh, d.seats_in_play, c.T("ASIENTOS EN JUEGO", "SEATS IN PLAY"),
            c.T("pilotos sin contrato para el año que viene", "drivers out of contract next year"), ui::kRed, ui::Icon::grid,
            lit(d.seats_in_play, 10));
        kpi(c, c.area.x + (kw + gap), y, kw, kh, d.open_talks, c.T("RUMORES Y CHARLAS", "RUMOURS & TALKS"),
            c.T("historias abiertas ahora mismo", "stories open right now"), ui::kAmber, ui::Icon::rumours, lit(d.open_talks, 12));
        kpi(c, c.area.x + 2 * (kw + gap), y, kw, kh, d.done_deals, c.T("FICHAJES CERRADOS", "DONE DEALS"),
            c.T("contratos firmados para el año que viene", "contracts signed for next year"), ui::kGreen, ui::Icon::deals,
            lit(d.done_deals, 8));
        kpi(c, c.area.x + 3 * (kw + gap), y, kw, kh, d.unread, c.T("SIN LEER", "UNREAD"),
            c.T("noticias desde tu última visita", "stories since your last visit"), ui::kBlue, ui::Icon::news, lit(d.unread, 10));
    }
    y += kh + 26 * k;

    // left: the top stories; right: the player's corner and the movers
    float lw = std::round(c.area.w * 0.6f), rw = c.area.w - lw - gap, rx = c.area.x + lw + gap;
    float ly = y + section_title(c, c.area.x, y, c.T("TITULARES", "TOP STORIES"), {}, lw);
    if (d.news.empty())
        ly += empty_state(c, ly, ui::Icon::news, c.T("Sin titulares", "No headlines"),
                          c.T("Vuelve después de la próxima carrera.", "Come back after the next race."));
    for (size_t i = 0; i < d.news.size() && i < 3; ++i)
        ly += news_card(c, c.area.x, ly, lw, d.news[i], true, i == 0) + 12 * k;

    // the player's corner
    float ry = y + section_title(c, rx, y, c.T("TU RINCÓN", "YOUR CORNER"));
    float ch = d.has_player ? (d.press_pending ? 244 * k : 196 * k) : 130 * k;
    if (c.visible(ry, ch)) {
        Reveal r(c);
        ui::card(c.p, rx, ry, rw, ch, k);
        if (d.has_player) {
            float lh = 84 * k;
            {
                ov::ClipScope clip(c.p, rx, ry, rw, lh);
                ov::rect_rounded(c.p, rx, ry, rw, lh + 14 * k, 12 * k, ui::rgb(d.player_col, 0x50));
                ui::livery(c.p, rx, ry, rw, lh, d.player_col, k, 1, 12 * k);
                ov::text(c.p, rx + rw - 16 * k, ry + lh + 8 * k, ui::ts(104 * k, 0xFFFFFF1A, ov::align::right | ov::align::bottom, 2 * k),
                         d.player_code);
            }
            ui::team_chip(c.p, rx + 20 * k, ry + 18 * k, d.player_code, d.player_col, k);
            ov::text(c.p, rx + 20 * k, ry + 50 * k, ui::lifted(ui::ts(24 * k, 0xFFFFFFFF), k), d.player_team);
            // three stats under the livery
            float sy = ry + lh + 18 * k, cw3 = (rw - 40 * k) / 3;
            auto stat = [&](int i, const char *label) {
                float sx = rx + 20 * k + i * cw3;
                if (i)
                    ov::rect(c.p, sx - 10 * k, sy + 2 * k, 1 * k, 52 * k, 0xFFFFFF14);
                ov::text(c.p, sx, sy, ui::ts(11.5f * k, ui::kMuted, ov::align::left | ov::align::top, 1.6f * k), label);
                return sx;
            };
            float s0 = stat(0, c.T("COMPAÑERO", "TEAM-MATE"));
            ov::text(c.p, s0, sy + 24 * k, ui::ts(17 * k, ui::kText),
                     ui::fit(c.p, ui::ts(17 * k), d.teammate.empty() ? std::string("-") : d.teammate, cw3 - 16 * k));
            float s1 = stat(1, c.T("CONTRATO", "CONTRACT"));
            ov::text(c.p, s1, sy + 18 * k, ui::ts(28 * k, ui::kText), d.player_contract > 0 ? std::to_string(d.player_contract) : "-");
            float s2 = stat(2, c.T("COMPROMISO", "COMMITMENT"));
            uint32_t lc = d.loyalty < 0 ? ui::kDanger : d.loyalty > 0 ? ui::kGreen : ui::kAmber;
            ui::meter(c.p, s2, sy + 28 * k, 5, d.loyalty + 3, lc, k, std::min(20.0f, (cw3 / k - 30) / 5), 9);
            ov::text(c.p, s2, sy + 44 * k, ui::ts(12.5f * k, lc),
                     d.loyalty < 0 ? c.T("quiere irse", "wants out") : d.loyalty > 0 ? c.T("comprometido", "committed") : c.T("abierto", "open"));
            if (d.press_pending) {
                float by = ry + ch - 60 * k, bw = rw - 32 * k, bx = rx + 16 * k;
                float pa = 0.45f + 0.55f * ui::pulse(1.8f);
                ov::rect_rounded(c.p, bx, by, bw, 44 * k, 9 * k, 0xFFB02024);
                ov::rect_rounded_outline(c.p, bx, by, bw, 44 * k, 9 * k, 1.5f * k, ui::alpha(ui::kAmber, pa));
                ui::icon(c.p, ui::Icon::press, bx + 24 * k, by + 22 * k, 18 * k, ui::kAmber, k);
                ov::text(c.p, bx + 46 * k, by + 22 * k, ui::ts(13 * k, ui::kAmber, ov::align::left | ov::align::middle, 1.6f * k),
                         c.T("LA PRENSA ESPERA TU RESPUESTA", "THE PRESS WANTS AN ANSWER"));
            }
        } else {
            ui::glow(c.p, rx + 50 * k, ry + 60 * k, 120 * k, 0xFFFFFF10);
            ui::icon(c.p, ui::Icon::drivers, rx + 50 * k, ry + 62 * k, 36 * k, ui::kMuted, k);
            ov::text(c.p, rx + 92 * k, ry + 30 * k, ui::ts(19 * k, ui::kText), c.T("Todavía no sé quién eres", "Who are you?"));
            ov::text(c.p, rx + 92 * k, ry + 60 * k, ui::ts(15 * k, ui::kText2, ov::align::left | ov::align::top, 0, rw - 112 * k),
                     c.T("Tu equipo aparece aquí en cuanto el juego lo dice en el Team HQ.",
                         "Your team shows here once the game reports it at Team HQ."));
        }
    }
    ry += ch + 24 * k;

    // the market movers
    ry += section_title(c, rx, ry, c.T("EN ALZA Y A LA BAJA", "MARKET MOVERS"));
    std::vector<const UiDriver *> movers;
    for (const UiDriver &u : d.drivers)
        if (std::fabs(u.trend) >= 0.05f)
            movers.push_back(&u);
    std::sort(movers.begin(), movers.end(), [](const UiDriver *a, const UiDriver *b) { return a->trend > b->trend; });
    std::vector<const UiDriver *> show;
    for (size_t i = 0; i < movers.size() && i < 2; ++i)
        if (movers[i]->trend > 0)
            show.push_back(movers[i]);
    for (size_t i = 0; i < movers.size() && i < 2; ++i)
        if (movers[movers.size() - 1 - i]->trend < 0)
            show.push_back(movers[movers.size() - 1 - i]);
    float maxv = 0.3f;
    for (const UiDriver *u : show)
        maxv = std::max(maxv, std::fabs(u->trend));
    float mh = std::max<float>(1, static_cast<float>(show.size())) * 44 * k + 20 * k;
    if (c.visible(ry, mh)) {
        Reveal r(c);
        ui::card(c.p, rx, ry, rw, mh, k);
        if (show.empty())
            ov::text(c.p, rx + 20 * k, ry + 22 * k, ui::ts(15 * k, ui::kText2),
                     c.T("La bolsa se mueve después de cada carrera.", "The stock moves after every race."));
        float my = ry + 10 * k;
        for (size_t i = 0; i < show.size(); ++i) {
            const UiDriver *u = show[i];
            bool up = u->trend > 0;
            uint32_t col = up ? ui::kGreen : ui::kDanger;
            if (i && up != (show[i - 1]->trend > 0))
                ov::rect(c.p, rx + 16 * k, my, rw - 32 * k, 1 * k, 0xFFFFFF10);
            ui::triangle(c.p, rx + 26 * k, my + 22 * k, 6 * k, up, col);
            ui::team_chip(c.p, rx + 44 * k, my + 10 * k, u->code, u->col, k);
            ov::text(c.p, rx + 112 * k, my + 22 * k, ui::ts(17 * k, ui::kText, ov::align::left | ov::align::middle),
                     ui::fit(c.p, ui::ts(17 * k), u->name, rw - 290 * k));
            float bw = 90 * k * std::fabs(u->trend) / maxv * c.grow();
            ov::rect_rounded(c.p, rx + rw - 160 * k, my + 19 * k, 90 * k, 6 * k, 3 * k, 0xFFFFFF0C);
            ov::gradient(c.p, rx + rw - 160 * k, my + 19 * k, std::max(6 * k, bw), 6 * k, ui::alpha(col, 0.3f), col, true, 3 * k);
            char v[24];
            std::snprintf(v, sizeof v, "%+.1f", u->trend);
            ov::text(c.p, rx + rw - 20 * k, my + 22 * k, ui::ts(16 * k, up ? ui::kGreen : ui::kDangerText, ov::align::right | ov::align::middle), v);
            my += 44 * k;
        }
    }
    ry += mh;
    return std::max(ly, ry) - y0;
}

float page_press(const Ctx &c, float y0, const ov::Input &in)
{
    using namespace paint_state;
    const OverlayData &d = g_ov;
    float k = c.k;
    if (!d.press_pending || d.press_answers.empty())
        return empty_state(c, y0, ui::Icon::press, c.T("No hay preguntas pendientes", "No questions waiting"),
                           c.T("La prensa te buscará después de algunas carreras. Tus respuestas mueven el mercado.",
                               "The press will find you after a few races. Your answers move the market."));
    int answers = static_cast<int>(d.press_answers.size());
    press_sel = std::clamp(press_sel, 0, answers - 1);
    if (in.key_typed(VK_DOWN) || in.pad_hit(ov::pad::down))
        press_sel = (press_sel + 1) % answers;
    if (in.key_typed(VK_UP) || in.pad_hit(ov::pad::up))
        press_sel = (press_sel + answers - 1) % answers;
    for (int i = 0; i < answers && i < 9; ++i)
        if (in.key_pressed('1' + i) || in.key_pressed(VK_NUMPAD1 + i))
            choose_answer(i);
    if (in.key_pressed(VK_RETURN) || in.pad_hit(ov::pad::a))
        choose_answer(press_sel);

    // the question: the press room, with a spotlight on the microphone
    ov::TextStyle q = ui::ts(28 * k, ui::kText, ov::align::left | ov::align::top, 0, c.area.w - 200 * k);
    q.line_height = 1.25f;
    float qh = std::max(170 * k, ui::text_h(c.p, q, d.press_question) + 104 * k), y = y0;
    {
        Reveal r(c);
        ui::card(c.p, c.area.x, y, c.area.w, qh, k);
        ov::ClipScope clip(c.p, c.area.x, y, c.area.w, qh);
        ov::gradient(c.p, c.area.x, y, c.area.w * 0.7f, qh, 0xFFB02026, 0xFFB02000, true, 12 * k);
        ui::glow(c.p, c.area.x + 70 * k, y + 70 * k, 190 * k, 0xFFB02040);
        ui::hatch(c.p, c.area.x + c.area.w * 0.6f, y, c.area.w * 0.4f, qh, k, 0xFFFFFF05, 10 * k);
        ov::text(c.p, c.area.x + c.area.w - 40 * k, y - 30 * k, ui::ts(260 * k, 0xFFFFFF0C, ov::align::right | ov::align::top), "\xE2\x80\x9D");
        ov::circle(c.p, c.area.x + 70 * k, y + 70 * k, 38 * k, 0x1A1408FF);
        ov::circle_outline(c.p, c.area.x + 70 * k, y + 70 * k, 38 * k, 2 * k, ui::kAmber);
        ov::arc(c.p, c.area.x + 70 * k, y + 70 * k, 46 * k, ui::clock_s() * 1.2f, ui::clock_s() * 1.2f + 1.2f, 2 * k, 0xFFB02080);
        ui::icon(c.p, ui::Icon::press, c.area.x + 70 * k, y + 70 * k, 32 * k, ui::kAmber, k);
        ui::tag(c.p, c.area.x + 134 * k, y + 26 * k, c.T("RUEDA DE PRENSA", "PRESS CONFERENCE"), ui::kAmber, k, 12, true);
        ov::text(c.p, c.area.x + 134 * k + 190 * k, y + 37 * k, ui::ts(13 * k, ui::kAmber, ov::align::left | ov::align::middle, 1.6f * k),
                 c.T("UN PERIODISTA PREGUNTA", "A JOURNALIST ASKS"));
        ov::text(c.p, c.area.x + 134 * k, y + 68 * k, ui::lifted(q, k), d.press_question);
    }
    y += qh + 26 * k;
    y += section_title(c, c.area.x, y, c.T("TU RESPUESTA", "YOUR ANSWER"), c.T("1-3, o Arriba/Abajo y Enter / A", "1-3, or Up/Down and Enter / A"));
    for (int i = 0; i < answers; ++i) {
        ov::TextStyle a = ui::ts(19 * k, ui::kText, ov::align::left | ov::align::top, 0, c.area.w - 150 * k);
        a.line_height = 1.3f;
        float ah = std::max(70 * k, ui::text_h(c.p, a, d.press_answers[static_cast<size_t>(i)]) + 40 * k);
        bool hov = c.hover(c.area.x, y, c.area.w, ah);
        if (hov && in.clicked())
            choose_answer(i);
        bool sel = i == press_sel || hov;
        Reveal r(c);
        ui::card(c.p, c.area.x, y, c.area.w, ah, k, sel, ui::kAmber);
        if (sel) {
            ov::ClipScope clip(c.p, c.area.x, y, c.area.w, ah);
            ov::gradient(c.p, c.area.x, y, c.area.w * 0.5f, ah, 0xFFB0201E, 0xFFB02000, true);
            ov::rect_rounded_outline(c.p, c.area.x, y, c.area.w, ah, 12 * k, 2 * k, ui::alpha(ui::kAmber, 0.6f + 0.4f * ui::pulse(1.4f)));
        }
        float bx = c.area.x + 22 * k, by = y + ah / 2 - 20 * k;
        if (sel)
            ui::glow(c.p, bx + 20 * k, y + ah / 2, 44 * k, 0xFFB02066);
        ov::rect_rounded(c.p, bx, by, 40 * k, 40 * k, 9 * k, sel ? ui::kAmber : 0x2A2D3AFF);
        ov::text(c.p, bx + 20 * k, y + ah / 2, ui::ts(19 * k, sel ? 0x111111FF : ui::kText, ov::align::center | ov::align::middle),
                 std::to_string(i + 1));
        ov::text(c.p, c.area.x + 84 * k, y + 20 * k, a, d.press_answers[static_cast<size_t>(i)]);
        if (sel) {
            float ax = c.area.x + c.area.w - 40 * k + 4 * k * ui::pulse(1.0f);
            float pts[6] = {ax, y + ah / 2 - 9 * k, ax + 10 * k, y + ah / 2, ax, y + ah / 2 + 9 * k};
            ov::polyline(c.p, pts, 3, 2.5f * k, ui::kAmber);
        }
        y += ah + 12 * k;
    }
    return y - y0;
}

float page_rumours(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k;
    if (d.rumours.empty())
        return empty_state(c, y0, ui::Icon::rumours, c.T("El paddock está en silencio", "The paddock is quiet"),
                           c.T("Los rumores llegan a partir de la segunda carrera.", "Rumours start from the second race."));
    int talks = 0;
    for (const UiRumour &r : d.rumours)
        talks += r.stage;
    char note[64];
    int loose = static_cast<int>(d.rumours.size()) - talks;
    std::snprintf(note, sizeof note, "%d %s  ·  %d %s", loose,
                  loose == 1 ? c.T("rumor", "rumour") : c.T("rumores", "rumours"), talks, c.T("negociando", "in talks"));
    float y = y0 + section_title(c, c.area.x, y0, c.T("QUIÉN VA A DÓNDE", "WHO GOES WHERE"), note);
    const char *steps[3] = {c.T("RUMOR", "RUMOUR"), c.T("CHARLAS", "TALKS"), c.T("FIRMADO", "SIGNED")};
    for (const UiRumour &r : d.rumours) {
        float h = 86 * k;
        if (c.visible(y, h)) {
            Reveal rv(c);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            uint32_t hot = r.stage ? ui::kAmber : 0x8E9AAFFF;
            ui::card(c.p, c.area.x, y, c.area.w, h, k, hov, hot);
            {
                ov::ClipScope clip(c.p, c.area.x, y, c.area.w, h);
                if (r.to_col)
                {
                    float gx = c.area.x + c.area.w * 0.2f, gw = c.area.w * 0.22f;
                    ov::gradient(c.p, gx, y, gw, h, ui::rgb(r.to_col, 0x00), ui::rgb(r.to_col, 0x22), true);
                    ov::gradient(c.p, gx + gw, y, gw * 1.3f, h, ui::rgb(r.to_col, 0x22), ui::rgb(r.to_col, 0x00), true);
                }
            }
            ov::rect_rounded(c.p, c.area.x, y + 14 * k, 3.5f * k, h - 28 * k, 2 * k, r.from_col ? ui::rgb(r.from_col) : ui::kMuted);
            float x = c.area.x + 24 * k;
            ov::text(c.p, x, y + 18 * k, ui::ts(20 * k, ui::kText), ui::fit(c.p, ui::ts(20 * k), r.driver, 280 * k));
            if (r.from_col)
                ov::circle(c.p, x + 5 * k, y + 57 * k, 4.5f * k, ui::rgb(r.from_col));
            ov::text(c.p, x + (r.from_col ? 16 * k : 0), y + 57 * k, ui::ts(14 * k, ui::kMuted, ov::align::left | ov::align::middle),
                     r.from.empty() ? std::string(c.T("sin equipo", "no team")) : std::string(c.T("ahora en ", "now at ")) + r.from);
            // the move: dashes run towards the new team, faster once they talk
            float ax = c.area.x + 320 * k;
            ui::flow_arrow(c.p, ax, y + h / 2, 70 * k, k, hot, r.stage ? 46.0f : 16.0f);
            float cw = ui::team_chip(c.p, ax + 84 * k, y + h / 2 - 13 * k, r.to_code, r.to_col, k, 26);
            ov::text(c.p, ax + 96 * k + cw, y + h / 2, ui::ts(19 * k, ui::kText, ov::align::left | ov::align::middle),
                     ui::fit(c.p, ui::ts(19 * k), r.to, 210 * k));
            // the stage: rumour, talks, signed
            float sx = c.area.x + c.area.w - 500 * k, step = 78 * k, sy = y + 34 * k;
            ov::rect(c.p, sx, sy - 1 * k, step * 2, 2 * k, 0xFFFFFF16);
            ov::gradient(c.p, sx, sy - 1 * k, step * r.stage * c.grow(), 2 * k, 0x8E9AAFFF, ui::kAmber, true);
            for (int s = 0; s < 3; ++s) {
                float nx = sx + s * step;
                bool done = s <= r.stage, now = s == r.stage;
                uint32_t nc = s == 0 ? 0x8E9AAFFF : s == 1 ? ui::kAmber : ui::kGreen;
                if (now)
                    ui::live_dot(c.p, nx, sy, 6 * k, nc, 1.8f);
                else if (done)
                    ov::circle(c.p, nx, sy, 5.5f * k, nc);
                else
                    ov::circle_outline(c.p, nx, sy, 6 * k, 1.5f * k, 0xFFFFFF30);
                ov::text(c.p, nx, sy + 20 * k, ui::ts(10.5f * k, done ? ui::kText2 : ui::kMuted, ov::align::center | ov::align::middle, 1.2f * k),
                         steps[s]);
            }
            // who says so
            float ox = c.area.x + c.area.w - 22 * k;
            ov::text(c.p, ox, y + 24 * k, ui::ts(15 * k, ui::kText2, ov::align::right | ov::align::middle), ui::fit(c.p, ui::ts(15 * k), r.outlet, 200 * k));
            ui::dots(c.p, ox - 52 * k, y + 54 * k, r.rel, k);
            ov::text(c.p, ox - 62 * k, y + 54 * k, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::middle, 1.2f * k),
                     c.T("FIABILIDAD", "RELIABILITY"));
        }
        y += h + 10 * k;
    }
    return y - y0;
}

float move_row(const Ctx &c, float x0, float w, float y, const UiMove &m, bool with_year)
{
    float k = c.k, h = 60 * k;
    if (!c.visible(y, h))
        return h;
    Reveal r(c);
    ui::Tag tag = ui::move_tag(m.kind);
    ui::card(c.p, x0, y, w, h, k, c.hover(x0, y, w, h), tag.col);
    {
        ov::ClipScope clip(c.p, x0, y, w, h);
        if (m.to_col)
            ov::gradient(c.p, x0 + w * 0.55f, y, w * 0.45f, h, ui::rgb(m.to_col, 0x00), ui::rgb(m.to_col, 0x22), true);
    }
    float x = x0 + 18 * k;
    if (with_year) {
        char when[32];
        std::snprintf(when, sizeof when, "R%d", m.round);
        ov::text(c.p, x, y + h / 2, ui::ts(14 * k, ui::kMuted, ov::align::left | ov::align::middle, 1 * k), when);
        x += 46 * k;
    }
    ui::tag(c.p, x, y + h / 2 - 11 * k, c.T(tag.es, tag.en), tag.col, k, 12);
    x += 140 * k;
    ov::text(c.p, x, y + h / 2, ui::ts(19 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(19 * k), m.driver, 270 * k));
    x += 290 * k;
    if (!m.from.empty())
        ui::team_chip(c.p, x, y + h / 2 - 12 * k, m.from_code, m.from_col, k);
    else
        ov::text(c.p, x + 28 * k, y + h / 2, ui::ts(16 * k, ui::kMuted, ov::align::center | ov::align::middle), "-");
    ui::arrow_right(c.p, x + 68 * k, y + h / 2, 36 * k, k, ui::kMuted);
    if (!m.to.empty()) {
        float cw = ui::team_chip(c.p, x + 116 * k, y + h / 2 - 12 * k, m.to_code, m.to_col, k);
        ov::text(c.p, x + 128 * k + cw, y + h / 2, ui::ts(16 * k, ui::kText2, ov::align::left | ov::align::middle), m.to);
    } else {
        ov::text(c.p, x + 116 * k, y + h / 2, ui::ts(16 * k, ui::kMuted, ov::align::left | ov::align::middle), c.T("fuera de la parrilla", "off the grid"));
    }
    return h;
}

float page_deals(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    char head[64], note[48];
    std::snprintf(head, sizeof head, "%s %d", c.T("FIRMADOS PARA", "SIGNED FOR"), d.year + 1);
    std::snprintf(note, sizeof note, "%d %s", static_cast<int>(d.deals.size()),
                  d.deals.size() == 1 ? c.T("contrato", "contract") : c.T("contratos", "contracts"));
    y += section_title(c, c.area.x, y, head, d.deals.empty() ? std::string_view{} : std::string_view(note));
    if (d.deals.empty()) {
        y += empty_state(c, y, ui::Icon::deals, c.T("Nada firmado todavía", "Nothing signed yet"),
                         c.T("Los fichajes se cierran a lo largo de la temporada.", "Deals get done as the season goes on."));
    } else {
        // two columns of signed contracts
        float gap = 14 * k, w = (c.area.w - gap) / 2, h = 112 * k;
        for (size_t i = 0; i < d.deals.size(); ++i) {
            const UiDeal &u = d.deals[i];
            float x = c.area.x + (i % 2) * (w + gap), cy = y + (i / 2) * (h + gap);
            if (!c.visible(cy, h))
                continue;
            Reveal r(c);
            ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), ui::rgb(u.col));
            {
                ov::ClipScope clip(c.p, x, cy, w, h);
                ov::gradient(c.p, x, cy, w * 0.6f, h, ui::rgb(u.col, 0x48), ui::rgb(u.col, 0x00), true);
                ui::glow(c.p, x + 40 * k, cy + 30 * k, 150 * k, ui::rgb(u.col, 0x30));
                ov::text(c.p, x + w - 20 * k, cy + h + 14 * k, ui::ts(96 * k, 0xFFFFFF0A, ov::align::right | ov::align::bottom, 2 * k), u.code);
            }
            ui::team_chip(c.p, x + 20 * k, cy + 20 * k, u.code, u.col, k);
            ov::text(c.p, x + 20 * k, cy + 56 * k, ui::ts(13.5f * k, ui::kText2), ui::fit(c.p, ui::ts(13.5f * k), u.team, 130 * k));
            ov::text(c.p, x + 160 * k, cy + 18 * k, ui::lifted(ui::ts(22 * k, ui::kText), k), ui::fit(c.p, ui::ts(22 * k), u.driver, w - 330 * k));
            ui::Tag tag = ui::move_tag(u.kind);
            ui::tag(c.p, x + 160 * k, cy + 56 * k, c.T(tag.es, tag.en), tag.col, k, 12);
            ov::text(c.p, x + 160 * k, cy + 92 * k, ui::ts(12 * k, ui::kMuted, ov::align::left | ov::align::middle, 1.4f * k),
                     c.T("CONTRATO HASTA", "CONTRACT UNTIL"));
            ov::text(c.p, x + 160 * k + 130 * k, cy + 92 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), std::to_string(u.until));
            // the stamp
            float sx = x + w - 92 * k, sy = cy + h / 2;
            float sa = ui::ease_back((c.t - 0.25f - 0.05f * static_cast<float>(std::min<size_t>(i, 10))) / 0.4f);
            if (sa > 0) {
                ov::push_rotate(c.p, -0.16f, sx, sy);
                ov::push_scale(c.p, 1.6f - 0.6f * sa, 1.6f - 0.6f * sa, sx, sy);
                ov::OpacityScope so(c.p, std::min(1.0f, sa));
                ov::rect_rounded_outline(c.p, sx - 58 * k, sy - 18 * k, 116 * k, 36 * k, 6 * k, 2.5f * k, 0x30D158C0);
                ov::rect_rounded_outline(c.p, sx - 53 * k, sy - 13 * k, 106 * k, 26 * k, 4 * k, 1 * k, 0x30D15880);
                ov::text(c.p, sx, sy, ui::ts(15 * k, 0x30D158E0, ov::align::center | ov::align::middle, 3 * k), c.T("FIRMADO", "SIGNED"));
                ov::pop_transform(c.p);
                ov::pop_transform(c.p);
            }
        }
        y += ((d.deals.size() + 1) / 2) * (h + gap);
    }
    y += 20 * k;
    std::snprintf(head, sizeof head, "%s %d", c.T("MOVIMIENTOS DE", "MOVES OF"), d.year);
    y += section_title(c, c.area.x, y, head);
    if (d.moves_year.empty()) {
        if (c.visible(y, 40 * k))
            ov::text(c.p, c.area.x, y + 6 * k, ui::ts(16 * k, ui::kText2), c.T("Ningún cambio esta temporada.", "No changes this season."));
        y += 40 * k;
    }
    for (const UiMove &m : d.moves_year)
        y += move_row(c, c.area.x, c.area.w, y, m, false) + 8 * k;
    return y - y0;
}

float page_grid(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    char head[64];
    std::snprintf(head, sizeof head, "%s %d", d.finalised ? c.T("PARRILLA DEFINITIVA", "FINAL GRID") : c.T("PARRILLA PREVISTA", "PROJECTED GRID"),
                  d.year + 1);
    y += section_title(c, c.area.x, y, head, d.finalised ? "" : c.T("con los fichajes de hoy", "with today's deals"));
    float gap = 14 * k, w = (c.area.w - gap) / 2, h = 142 * k;
    for (size_t i = 0; i < d.grid.size(); ++i) {
        const UiTeam &t = d.grid[i];
        float x = c.area.x + (i % 2) * (w + gap), cy = y + (i / 2) * (h + gap);
        if (!c.visible(cy, h))
            continue;
        Reveal r(c);
        bool hov = c.hover(x, cy, w, h);
        ui::card(c.p, x, cy, w, h, k, hov, ui::rgb(t.col));
        float lh = 48 * k;
        {
            ov::ClipScope clip(c.p, x, cy, w, lh);
            ov::rect_rounded(c.p, x, cy, w, lh + 14 * k, 12 * k, ui::rgb(t.col, 0x40));
            ui::livery(c.p, x, cy, w, lh, t.col, k, 1, 12 * k);
            ov::text(c.p, x + w - 16 * k, cy + lh / 2, ui::ts(34 * k, 0xFFFFFF26, ov::align::right | ov::align::middle, 1 * k), t.code);
        }
        ov::text(c.p, x + 18 * k, cy + lh / 2, ui::lifted(ui::ts(19 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 0.8f * k), k), t.name);
        char rank[16];
        std::snprintf(rank, sizeof rank, "P%d", t.rank);
        float rx = x + w - 100 * k;
        ov::text(c.p, rx, cy + lh / 2, ui::lifted(ui::ts(15 * k, 0xFFFFFFD0, ov::align::right | ov::align::middle, 1 * k), k), rank);
        for (int s = 0; s < 2; ++s) {
            const UiSeat &seat = t.seat[s];
            float sy = cy + lh + 12 * k + s * 40 * k;
            ov::rect_rounded(c.p, x + 18 * k, sy, 26 * k, 28 * k, 6 * k, 0xFFFFFF0C);
            ov::text(c.p, x + 31 * k, sy + 14 * k, ui::ts(13 * k, ui::kText2, ov::align::center | ov::align::middle), std::to_string(s + 1));
            if (seat.name.empty()) {
                float pa = 0.25f + 0.25f * ui::pulse(2.2f, static_cast<float>(i) * 0.13f);
                ui::dashed_rect(c.p, x + 54 * k, sy, 190 * k, 28 * k, k, ui::alpha(0xFFFFFFFF, pa + 0.15f));
                ov::text(c.p, x + 149 * k, sy + 14 * k, ui::ts(12.5f * k, ui::kText2, ov::align::center | ov::align::middle, 1.6f * k),
                         c.T("POR DECIDIR", "TO BE DECIDED"));
                continue;
            }
            uint32_t nc = seat.player ? ui::kGold : ui::kText;
            ov::text(c.p, x + 56 * k, sy + 14 * k, ui::ts(18 * k, nc, ov::align::left | ov::align::middle),
                     ui::fit(c.p, ui::ts(18 * k), seat.name, w - 190 * k));
            if (seat.player) {
                ui::glow(c.p, x + w - 46 * k, sy + 14 * k, 40 * k, 0xFFD60A40);
                ui::tag(c.p, x + w - 72 * k, sy + 3 * k, c.T("TÚ", "YOU"), ui::kGold, k, 11, true);
            } else if (seat.fresh) {
                ui::glow(c.p, x + w - 50 * k, sy + 14 * k, 40 * k, 0x30D15840);
                ui::tag(c.p, x + w - 80 * k, sy + 3 * k, c.T("NUEVO", "NEW"), ui::kGreen, k, 11, true);
            }
        }
    }
    y += ((d.grid.size() + 1) / 2) * (h + gap);
    return y - y0;
}

float page_drivers(const Ctx &c, float y0, bool free_agents)
{
    const OverlayData &d = g_ov;
    const std::vector<UiDriver> &list = free_agents ? d.free_agents : d.drivers;
    float k = c.k, y = y0;
    if (list.empty())
        return empty_state(c, y0, free_agents ? ui::Icon::free : ui::Icon::drivers, c.T("Nadie en la lista", "Nobody on the list"),
                           c.T("Todos los pilotos tienen equipo.", "Every driver has a seat."));
    // column header
    float cx_rank = c.area.x + 14 * k, cx_name = c.area.x + 70 * k, cx_team = c.area.x + 390 * k, cx_age = c.area.x + 490 * k,
          cx_val = c.area.x + 570 * k, cx_trend = c.area.x + 830 * k, cx_con = c.area.x + 930 * k, cx_sal = c.area.x + 1040 * k;
    {
        ov::gradient(c.p, c.area.x, y, c.area.w, 32 * k, 0xFFFFFF0E, 0xFFFFFF06, false, 7 * k);
        ov::TextStyle hs = ui::ts(12 * k, ui::kText2, ov::align::left | ov::align::middle, 1.6f * k);
        float hy = y + 16 * k;
        ov::text(c.p, cx_rank + 18 * k, hy, ui::ts(12 * k, ui::kText2, ov::align::center | ov::align::middle, 1.6f * k), "#");
        ov::text(c.p, cx_name, hy, hs, c.T("PILOTO", "DRIVER"));
        ov::text(c.p, cx_team, hy, hs, free_agents ? c.T("ÚLTIMO", "LAST") : c.T("EQUIPO", "TEAM"));
        ov::text(c.p, cx_age, hy, hs, c.T("EDAD", "AGE"));
        ov::text(c.p, cx_val, hy, hs, free_agents ? c.T("NIVEL / POTENCIAL", "LEVEL / POTENTIAL") : c.T("VALOR", "VALUE"));
        if (!free_agents) {
            ov::text(c.p, cx_trend, hy, hs, c.T("TENDENCIA", "TREND"));
            ov::text(c.p, cx_con, hy, hs, c.T("CONTRATO", "CONTRACT"));
            if (cx_sal + 60 * k < c.area.x + c.area.w)
                ov::text(c.p, cx_sal, hy, hs, c.T("SUELDO", "SALARY"));
        }
    }
    y += 40 * k;
    int rank = 0;
    for (const UiDriver &u : list) {
        ++rank;
        float h = 50 * k;
        if (c.visible(y, h)) {
            Reveal r(c, 10);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            ov::rect_rounded(c.p, c.area.x, y, c.area.w, h, 9 * k, hov ? ui::kCardHi : (rank % 2 ? 0x1A1C25FF : 0x15171FFF));
            if (hov) {
                ov::rect_rounded_outline(c.p, c.area.x, y, c.area.w, h, 9 * k, 1 * k, ui::rgb(u.col, 0x80));
                ov::gradient(c.p, c.area.x, y, c.area.w * 0.4f, h, ui::rgb(u.col, 0x26), ui::rgb(u.col, 0x00), true, 9 * k);
            }
            ov::rect_rounded(c.p, c.area.x, y + 10 * k, 3 * k, h - 20 * k, 1.5f * k, ui::rgb(u.col));
            float my = y + h / 2;
            if (rank <= 3) {
                uint32_t medal = rank == 1 ? ui::kGold : rank == 2 ? 0xC7C7CCFF : 0xD08B4CFF;
                ui::glow(c.p, cx_rank + 18 * k, my, 26 * k, ui::alpha(medal, 0.35f));
                ov::circle(c.p, cx_rank + 18 * k, my, 13 * k, medal);
                ov::text(c.p, cx_rank + 18 * k, my, ui::ts(14 * k, 0x111111FF, ov::align::center | ov::align::middle), std::to_string(rank));
            } else {
                ov::text(c.p, cx_rank + 18 * k, my, ui::ts(15 * k, ui::kMuted, ov::align::center | ov::align::middle), std::to_string(rank));
            }
            ov::text(c.p, cx_name, my, ui::ts(18 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(18 * k), u.name, 230 * k));
            // badges after the name
            float bx = cx_name + std::min(ui::text_w(c.p, ui::ts(18 * k), u.name), 230 * k) + 10 * k;
            if (u.pressure)
                bx += ui::tag(c.p, bx, my - 10 * k, c.T("PRESIÓN", "PRESSURE"), ui::kDanger, k, 10) + 4 * k;
            if (u.clause && bx < cx_team - 80 * k)
                bx += ui::tag(c.p, bx, my - 10 * k, c.T("CLÁUSULA", "CLAUSE"), 0xFF375FFF, k, 10) + 4 * k;
            if (u.feud && bx < cx_team - 70 * k)
                ui::tag(c.p, bx, my - 10 * k, c.T("RIVAL", "FEUD"), 0xBF5AF2FF, k, 10);
            if (free_agents) {
                ov::text(c.p, cx_team, my, ui::ts(15 * k, ui::kText2, ov::align::left | ov::align::middle),
                         u.last_team.empty() ? std::string(u.kind == "f2" ? "F2" : u.kind == "legend" ? c.T("leyenda", "legend") : "-") : u.last_team);
            } else {
                ui::team_chip(c.p, cx_team, my - 12 * k, u.code, u.col, k);
            }
            ov::text(c.p, cx_age + 14 * k, my, ui::ts(16 * k, ui::kText2, ov::align::center | ov::align::middle), std::to_string(u.age));
            float vw = 170 * k;
            if (free_agents) {
                ov::rect_rounded(c.p, cx_val, my - 4 * k, vw, 8 * k, 4 * k, 0xFFFFFF12);
                ov::rect_rounded(c.p, cx_val, my - 4 * k, std::max(8 * k, vw * std::clamp((u.potential - 55) / 45.0f, 0.0f, 1.0f) * c.grow()), 8 * k,
                                 4 * k, 0x0A84FF55);
                ui::bar(c.p, cx_val, my - 4 * k, vw, 8 * k, (u.value - 55) / 45.0f * c.grow(), ui::kBlue);
            } else {
                ui::bar(c.p, cx_val, my - 4 * k, vw, 8 * k, (u.value - 55) / 45.0f * c.grow(), ui::kRed);
            }
            char v[32];
            if (free_agents)
                std::snprintf(v, sizeof v, "%.0f / %.0f", u.value, u.potential);
            else
                std::snprintf(v, sizeof v, "%.0f", u.value);
            ov::text(c.p, cx_val + vw + 12 * k, my, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle), v);
            if (!free_agents) {
                if (std::fabs(u.trend) >= 0.05f) {
                    bool up = u.trend > 0;
                    ui::triangle(c.p, cx_trend + 8 * k, my, 5 * k, up, up ? ui::kGreen : ui::kDanger);
                    char t[16];
                    std::snprintf(t, sizeof t, "%+.1f", u.trend);
                    ov::text(c.p, cx_trend + 20 * k, my, ui::ts(15 * k, up ? ui::kGreen : ui::kDangerText, ov::align::left | ov::align::middle), t);
                } else {
                    ov::text(c.p, cx_trend + 8 * k, my, ui::ts(15 * k, ui::kMuted, ov::align::left | ov::align::middle), "=");
                }
                uint32_t cc = u.contract <= d.year ? ui::kDanger : u.contract == d.year + 1 ? ui::kAmber : ui::kGreen;
                ui::tag(c.p, cx_con, my - 11 * k, std::to_string(u.contract), cc, k, 12);
                if (cx_sal + 60 * k < c.area.x + c.area.w)
                    ov::text(c.p, cx_sal, my, ui::ts(15 * k, ui::kText2, ov::align::left | ov::align::middle), u.salary);
            }
        }
        y += h + 4 * k;
    }
    return y - y0;
}

float page_contracts(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k;
    struct Group {
        int from, to;
        std::string es, en, sub_es, sub_en;
        uint32_t col;
    };
    Group groups[] = {
        {0, d.year, "TERMINAN EN " + std::to_string(d.year), "OUT OF CONTRACT AFTER " + std::to_string(d.year),
         "Libres para firmar con otro", "Free to sign elsewhere", ui::kDanger},
        {d.year + 1, d.year + 1, "TERMINAN EN " + std::to_string(d.year + 1), "OUT OF CONTRACT AFTER " + std::to_string(d.year + 1),
         "El próximo verano en el mercado", "On the market next summer", ui::kAmber},
        {d.year + 2, 9999, "ATADOS HASTA " + std::to_string(d.year + 2) + " O MÁS", "SIGNED TO " + std::to_string(d.year + 2) + " AND BEYOND",
         "A salvo por ahora", "Safe for now", ui::kGreen},
    };
    // three columns, like a board on the wall of the team's office
    float gap = 16 * k, cw = (c.area.w - 2 * gap) / 3, bottom = y0;
    for (int gi = 0; gi < 3; ++gi) {
        const Group &g = groups[gi];
        std::vector<const UiDriver *> list;
        for (const UiDriver &u : d.drivers)
            if (u.contract >= g.from && u.contract <= g.to)
                list.push_back(&u);
        std::sort(list.begin(), list.end(), [](const UiDriver *x, const UiDriver *z) { return x->value > z->value; });
        float x = c.area.x + gi * (cw + gap), y = y0, hh = 92 * k;
        if (c.visible(y, hh)) {
            Reveal r(c);
            ui::card(c.p, x, y, cw, hh, k);
            ov::ClipScope clip(c.p, x, y, cw, hh);
            ov::gradient(c.p, x, y, cw, hh, ui::alpha(g.col, 0.20f), ui::alpha(g.col, 0.02f), false, 12 * k);
            ui::glow(c.p, x + cw - 40 * k, y + 30 * k, 140 * k, ui::alpha(g.col, 0.25f));
            ov::rect(c.p, x, y, cw, 3 * k, g.col);
            ov::text(c.p, x + 18 * k, y + 20 * k, ui::ts(13 * k, ui::kText, ov::align::left | ov::align::top, 1.6f * k),
                     ui::fit(c.p, ui::ts(13 * k, ui::kText, ov::align::left | ov::align::top, 1.6f * k), c.es ? g.es : g.en, cw - 90 * k));
            ov::text(c.p, x + 18 * k, y + 46 * k, ui::ts(14.5f * k, ui::kText2), c.es ? g.sub_es : g.sub_en);
            ov::text(c.p, x + cw - 20 * k, y + hh / 2, ui::lifted(ui::ts(44 * k, g.col, ov::align::right | ov::align::middle), k),
                     std::to_string(c.count(static_cast<int>(list.size()))));
        }
        y += hh + 12 * k;
        float tile_h = 62 * k;
        for (const UiDriver *u : list) {
            if (c.visible(y, tile_h)) {
                Reveal r(c, 10);
                bool hov = c.hover(x, y, cw, tile_h);
                ui::card(c.p, x, y, cw, tile_h, k, hov, ui::rgb(u->col));
                {
                    ov::ClipScope clip(c.p, x, y, cw, tile_h);
                    ov::gradient(c.p, x, y, cw * 0.5f, tile_h, ui::rgb(u->col, 0x2A), ui::rgb(u->col, 0x00), true);
                }
                ov::rect_rounded(c.p, x, y + 10 * k, 4 * k, tile_h - 20 * k, 2 * k, ui::rgb(u->col));
                ov::text(c.p, x + 18 * k, y + 11 * k, ui::ts(17 * k, ui::kText), ui::fit(c.p, ui::ts(17 * k), u->name, cw - 110 * k));
                ov::text(c.p, x + 18 * k, y + 37 * k, ui::ts(13.5f * k, ui::kText2), ui::fit(c.p, ui::ts(13.5f * k), u->team, cw - 120 * k));
                ov::text(c.p, x + cw - 16 * k, y + tile_h / 2, ui::ts(15 * k, ui::kText, ov::align::right | ov::align::middle), u->salary);
            }
            y += tile_h + 8 * k;
        }
        if (list.empty()) {
            if (c.visible(y, 40 * k)) {
                ui::dashed_rect(c.p, x, y, cw, 46 * k, k, 0xFFFFFF22);
                ov::text(c.p, x + cw / 2, y + 23 * k, ui::ts(14 * k, ui::kMuted, ov::align::center | ov::align::middle), c.T("Nadie.", "Nobody."));
            }
            y += 54 * k;
        }
        bottom = std::max(bottom, y);
    }
    return bottom - y0;
}

float page_stock(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    std::vector<const UiDriver *> list;
    for (const UiDriver &u : d.drivers)
        list.push_back(&u);
    std::sort(list.begin(), list.end(), [](const UiDriver *a, const UiDriver *b) { return a->trend > b->trend; });
    y += section_title(c, c.area.x, y, c.T("BOLSA DE PILOTOS", "DRIVER STOCK"),
                       c.T("cambio de valor en la última carrera", "value change over the last race"));
    float maxv = 0.5f;
    for (const UiDriver *u : list)
        maxv = std::max(maxv, std::fabs(u->trend));
    // a diverging bar chart around a centre line
    float name_w = 300 * k, chart_x = c.area.x + name_w, chart_w = c.area.w - name_w - 80 * k, mid = chart_x + chart_w / 2;
    float h = 36 * k;
    // the axis: falling on the left, rising on the right
    if (c.visible(y, 30 * k)) {
        ov::text(c.p, chart_x + 4 * k, y + 10 * k, ui::ts(12 * k, ui::kDangerText, ov::align::left | ov::align::middle, 1.6f * k),
                 c.T("BAJAN", "FALLING"));
        ov::text(c.p, chart_x + chart_w - 4 * k, y + 10 * k, ui::ts(12 * k, ui::kGreen, ov::align::right | ov::align::middle, 1.6f * k),
                 c.T("SUBEN", "RISING"));
        ov::text(c.p, mid, y + 10 * k, ui::ts(12 * k, ui::kMuted, ov::align::center | ov::align::middle), "0");
    }
    y += 26 * k;
    float top = y;
    {
        Reveal r(c, 8);
        ov::rect_rounded(c.p, chart_x, top - 4 * k, chart_w, static_cast<float>(list.size()) * h + 8 * k, 10 * k, 0xFFFFFF05);
        ov::gradient(c.p, chart_x, top - 4 * k, chart_w / 2, static_cast<float>(list.size()) * h + 8 * k, 0xFF453A00, 0xFF453A0C, true);
        ov::gradient(c.p, mid, top - 4 * k, chart_w / 2, static_cast<float>(list.size()) * h + 8 * k, 0x30D1580C, 0x30D15800, true);
        for (int q = 1; q < 4; ++q)
            if (q != 2)
                ov::rect(c.p, chart_x + chart_w * q / 4, top, 1 * k, static_cast<float>(list.size()) * h, 0xFFFFFF0A);
    }
    for (const UiDriver *u : list) {
        if (c.visible(y, h)) {
            Reveal r(c, 8);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            if (hov)
                ov::rect_rounded(c.p, c.area.x, y, c.area.w, h, 7 * k, 0xFFFFFF0C);
            ui::team_chip(c.p, c.area.x, y + 6 * k, u->code, u->col, k);
            ov::text(c.p, c.area.x + 66 * k, y + h / 2, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle),
                     ui::fit(c.p, ui::ts(16 * k), u->name, name_w - 80 * k));
            float len = (chart_w / 2 - 6 * k) * std::fabs(u->trend) / maxv * c.grow();
            bool up = u->trend >= 0;
            uint32_t col = up ? ui::kGreen : ui::kDanger;
            if (len >= 1) {
                float bh = h - 16 * k;
                if (up)
                    ov::gradient(c.p, mid, y + 8 * k, len, bh, 0x30D15840, ui::kGreen, true, 4 * k);
                else
                    ov::gradient(c.p, mid - len, y + 8 * k, len, bh, ui::kDanger, 0xFF453A40, true, 4 * k);
                float ex = up ? mid + len : mid - len;
                ui::glow(c.p, ex, y + h / 2, 22 * k, ui::alpha(col, 0.6f));
                ov::rect(c.p, up ? ex - 2 * k : ex, y + 8 * k, 2 * k, bh, 0xFFFFFFB0);
            }
            char v[16];
            std::snprintf(v, sizeof v, "%+.1f", u->trend);
            ov::text(c.p, c.area.x + c.area.w, y + h / 2,
                     ui::ts(15 * k, up ? (u->trend > 0.04f ? ui::kGreen : ui::kText2) : ui::kDangerText, ov::align::right | ov::align::middle), v);
        }
        y += h;
    }
    ov::rect(c.p, mid - 0.75f * k, top - 4 * k, 1.5f * k, y - top + 8 * k, 0xFFFFFF40);
    return y - y0 + 12 * k;
}

float page_f2(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    if (d.f2.empty())
        return empty_state(c, y0, ui::Icon::f2, c.T("La F2 todavía no ha arrancado", "F2 hasn't started yet"),
                           c.T("La clasificación aparece después de la primera carrera.", "The standings show after the first race."));
    char head[64];
    std::snprintf(head, sizeof head, "%s %d", c.T("CLASIFICACIÓN DE F2", "F2 STANDINGS"), d.year);
    y += section_title(c, c.area.x, y, head, c.T("la cantera de la F1", "F1's waiting room"));
    const uint32_t medals[3] = {ui::kGold, 0xC7C7CCFF, 0xD08B4CFF};
    size_t first_row = 0;
    // the podium: second, first, third
    if (d.f2.size() >= 3) {
        float ph = 250 * k, pw = 250 * k, gap = 18 * k, cx = c.area.x + c.area.w / 2;
        if (c.visible(y, ph)) {
            Reveal r(c);
            ui::glow(c.p, cx, y + 70 * k, 300 * k, 0xFFD60A1C);
            const int order[3] = {1, 0, 2};
            const float heights[3] = {132, 96, 74}; // by finishing place
            for (int o = 0; o < 3; ++o) {
                int i = order[o];
                const UiDriver &u = d.f2[static_cast<size_t>(i)];
                float bx = cx - pw * 1.5f - gap + o * (pw + gap), bh = heights[i] * k * (0.3f + 0.7f * c.grow());
                float by = y + ph - bh;
                ov::gradient(c.p, bx, by, pw, bh, ui::alpha(medals[i], 0.55f), ui::alpha(medals[i], 0.08f), false, 10 * k);
                ov::rect_rounded_outline(c.p, bx, by, pw, bh, 10 * k, 1 * k, ui::alpha(medals[i], 0.6f));
                ov::rect(c.p, bx + 10 * k, by, pw - 20 * k, 3 * k, medals[i]);
                ov::text(c.p, bx + pw / 2, by + bh / 2 + 4 * k, ui::lifted(ui::ts(std::min(58.0f * k, bh * 0.7f), 0xFFFFFFE0, ov::align::center | ov::align::middle), k),
                         std::to_string(i + 1));
                float ny = by - 64 * k;
                ov::circle(c.p, bx + pw / 2, ny - 4 * k, 7 * k, medals[i]);
                ui::glow(c.p, bx + pw / 2, ny - 4 * k, 24 * k, ui::alpha(medals[i], 0.5f));
                ov::text(c.p, bx + pw / 2, ny + 22 * k, ui::lifted(ui::ts(19 * k, ui::kText, ov::align::center | ov::align::middle), k),
                         ui::fit(c.p, ui::ts(19 * k), u.name, pw + 20 * k));
                char sub[48];
                std::snprintf(sub, sizeof sub, "%d %s  ·  %.0f %s", u.age, c.T("años", "y/o"), u.potential, c.T("pot.", "pot."));
                ov::text(c.p, bx + pw / 2, ny + 46 * k, ui::ts(14 * k, ui::kText2, ov::align::center | ov::align::middle), sub);
            }
        }
        y += ph + 22 * k;
        first_row = 3;
    }
    for (size_t idx = first_row; idx < d.f2.size(); ++idx) {
        const UiDriver &u = d.f2[idx];
        int pos = static_cast<int>(idx) + 1;
        float h = 52 * k;
        if (c.visible(y, h)) {
            Reveal r(c, 10);
            ui::card(c.p, c.area.x, y, c.area.w, h, k, c.hover(c.area.x, y, c.area.w, h), 0x64D2FFFF);
            bool podium = pos <= 3;
            uint32_t medal = podium ? medals[pos - 1] : 0x2A2D3AFF;
            ov::circle(c.p, c.area.x + 32 * k, y + h / 2, 15 * k, medal);
            ov::text(c.p, c.area.x + 32 * k, y + h / 2, ui::ts(15 * k, podium ? 0x111111FF : ui::kText, ov::align::center | ov::align::middle),
                     std::to_string(pos));
            ov::text(c.p, c.area.x + 66 * k, y + h / 2, ui::ts(18 * k, ui::kText, ov::align::left | ov::align::middle), u.name);
            char age[16];
            std::snprintf(age, sizeof age, "%d %s", u.age, c.T("años", "y/o"));
            ov::text(c.p, c.area.x + 430 * k, y + h / 2, ui::ts(15 * k, ui::kText2, ov::align::left | ov::align::middle), age);
            float bw = 240 * k, bx = c.area.x + c.area.w - bw - 100 * k;
            ui::bar(c.p, bx, y + h / 2 - 4 * k, bw, 8 * k, (u.potential - 55) / 45.0f * c.grow(), 0x64D2FFFF);
            char v[24];
            std::snprintf(v, sizeof v, "%.0f %s", u.potential, c.T("pot.", "pot."));
            ov::text(c.p, c.area.x + c.area.w - 18 * k, y + h / 2, ui::ts(15 * k, ui::kText, ov::align::right | ov::align::middle), v);
        }
        y += h + 6 * k;
    }
    return y - y0;
}

float page_staff(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    std::vector<const UiTeam *> list;
    for (const UiTeam &t : d.teams)
        list.push_back(&t);
    std::sort(list.begin(), list.end(), [](const UiTeam *a, const UiTeam *b) { return a->td_rating > b->td_rating; });
    y += section_title(c, c.area.x, y, c.T("DIRECTORES TÉCNICOS", "TECHNICAL DIRECTORS"),
                       c.T("cada invierno mueven el nivel del coche", "they move the car's level every winter"));
    float gap = 14 * k, w = (c.area.w - gap) / 2, h = 108 * k;
    for (size_t i = 0; i < list.size(); ++i) {
        const UiTeam &t = *list[i];
        float x = c.area.x + (i % 2) * (w + gap), cy = y + (i / 2) * (h + gap);
        if (!c.visible(cy, h))
            continue;
        Reveal r(c);
        ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), ui::rgb(t.col));
        {
            ov::ClipScope clip(c.p, x, cy, w, h);
            ov::gradient(c.p, x, cy, w * 0.5f, h, ui::rgb(t.col, 0x30), ui::rgb(t.col, 0x00), true);
        }
        ov::rect_rounded(c.p, x, cy + 12 * k, 4 * k, h - 24 * k, 2 * k, ui::rgb(t.col));
        // a ring gauge of the director's level, with ticks
        float f = std::clamp((t.td_rating - 50) / 50.0f, 0.0f, 1.0f);
        uint32_t gc = f > 0.8f ? ui::kGreen : f > 0.55f ? ui::kAmber : ui::kDanger;
        float gx = x + 62 * k, gy = cy + h / 2, rr = 32 * k, a0 = 2.3562f, sweep = 4.7124f; // a 270 degree dial
        ui::glow(c.p, gx, gy, 70 * k, ui::alpha(gc, 0.18f));
        ov::arc(c.p, gx, gy, rr, a0, a0 + sweep, 6 * k, 0xFFFFFF12);
        for (int tk = 0; tk <= 10; ++tk) {
            float a = a0 + sweep * tk / 10.0f;
            ov::line(c.p, gx + std::cos(a) * (rr + 7 * k), gy + std::sin(a) * (rr + 7 * k), gx + std::cos(a) * (rr + 10 * k),
                     gy + std::sin(a) * (rr + 10 * k), 1.2f * k, 0xFFFFFF30);
        }
        float fe = a0 + sweep * f * c.grow();
        if (f > 0) {
            ov::arc(c.p, gx, gy, rr, a0, fe, 6 * k, gc);
            ui::glow(c.p, gx + std::cos(fe) * rr, gy + std::sin(fe) * rr, 14 * k, ui::alpha(gc, 0.8f));
        }
        char v[8];
        std::snprintf(v, sizeof v, "%.0f", t.td_rating);
        ov::text(c.p, gx, gy - 2 * k, ui::ts(20 * k, ui::kText, ov::align::center | ov::align::middle), v);
        ov::text(c.p, gx, gy + 24 * k, ui::ts(9.5f * k, ui::kMuted, ov::align::center | ov::align::middle, 1.2f * k), "TD");
        ov::text(c.p, x + 118 * k, cy + 24 * k, ui::ts(20 * k, ui::kText), ui::fit(c.p, ui::ts(20 * k), t.td, w - 230 * k));
        ov::text(c.p, x + 118 * k, cy + 56 * k, ui::ts(15 * k, ui::kText2), t.name);
        const char *lvl = f > 0.8f ? c.T("ÉLITE", "ELITE") : f > 0.55f ? c.T("SÓLIDO", "STRONG") : f > 0.3f ? c.T("NORMAL", "AVERAGE") : c.T("FLOJO", "WEAK");
        ui::tag(c.p, x + 118 * k, cy + 78 * k, lvl, gc, k, 10);
        char pos[8];
        std::snprintf(pos, sizeof pos, "#%d", static_cast<int>(i) + 1);
        ov::text(c.p, x + w - 20 * k, cy + 24 * k, ui::ts(28 * k, 0xFFFFFF30, ov::align::right | ov::align::top), pos);
    }
    y += ((list.size() + 1) / 2) * (h + gap);
    return y - y0;
}

float page_history(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    if (d.history.empty())
        return empty_state(c, y0, ui::Icon::history, c.T("La historia está por escribir", "History is yet to be written"),
                           c.T("Cada fichaje, retirada y despido queda aquí.", "Every signing, retirement and sacking lands here."));
    float lx = c.area.x + 50 * k, rx = c.area.x + 84 * k, rw = c.area.w - 84 * k;
    float top = y0 + 30 * k;
    int year = -1;
    for (const UiMove &m : d.history) {
        if (m.year != year) {
            year = m.year;
            if (year != d.history.front().year)
                y += 14 * k;
            if (c.visible(y, 56 * k)) {
                Reveal r(c);
                ov::rect_rounded(c.p, c.area.x, y + 6 * k, 100 * k, 40 * k, 9 * k, 0xE1060026);
                ov::rect_rounded_outline(c.p, c.area.x, y + 6 * k, 100 * k, 40 * k, 9 * k, 1 * k, 0xE1060080);
                ov::text(c.p, c.area.x + 50 * k, y + 26 * k, ui::lifted(ui::ts(24 * k, ui::kText, ov::align::center | ov::align::middle, 1 * k), k),
                         std::to_string(year));
                ov::gradient(c.p, c.area.x + 116 * k, y + 26 * k, c.area.w - 116 * k, 1 * k, 0xFFFFFF26, 0xFFFFFF00, true);
            }
            y += 58 * k;
        }
        float h = move_row(c, rx, rw, y, m, true);
        if (c.visible(y, h)) {
            uint32_t col = ui::move_tag(m.kind).col;
            ui::glow(c.p, lx, y + h / 2, 16 * k, ui::alpha(col, 0.6f));
            ov::circle(c.p, lx, y + h / 2, 5 * k, col);
            ov::rect(c.p, lx + 6 * k, y + h / 2 - 0.5f * k, rx - lx - 6 * k, 1 * k, 0xFFFFFF22);
        }
        y += h + 8 * k;
    }
    float vis_top = std::max(top, c.area.y), vis_bot = std::min(y, c.area.y + c.area.h);
    if (vis_bot > vis_top)
        ov::rect(c.p, lx - 0.75f * k, vis_top, 1.5f * k, vis_bot - vis_top, 0xFFFFFF14);
    return y - y0;
}

// the headline ticker under the header: the latest stories scroll past like a broadcast crawl
void draw_ticker(ov::Paint *p, const OverlayData &d, float x, float y, float w, float h, float k)
{
    ov::rect(p, x, y, w, h, 0x00000040);
    ov::rect(p, x, y, w, 1 * k, 0xFFFFFF10);
    ov::rect(p, x, y + h - 1 * k, w, 1 * k, 0xFFFFFF10);
    float tagw = 150 * k;
    ui::para(p, x, y, tagw + 14 * k, h, 14 * k, ui::kRed);
    ui::para(p, x, y, tagw + 14 * k, h * 0.45f, 6 * k, 0xFFFFFF14);
    float dot_x = x + 26 * k;
    ov::circle(p, dot_x, y + h / 2, 4.5f * k, 0xFFFFFF60 + static_cast<uint32_t>(0x9F * ui::pulse(1.2f)));
    ov::text(p, dot_x + 14 * k, y + h / 2, ui::ts(14 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 2.2f * k),
             d.es ? "ÚLTIMA HORA" : "LATEST");
    float cx = x + tagw + 24 * k, cw = w - tagw - 24 * k;
    ov::TextStyle s = ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle, 0.4f * k);
    ov::TextStyle hs = ui::ts(13 * k, ui::kText, ov::align::left | ov::align::middle, 1.4f * k);
    struct Item {
        std::string head, text;
        uint32_t col;
        float hw, w;
    };
    // each story: its headline, then its first sentence
    std::vector<Item> items;
    for (const UiNews &n : d.news) {
        if (items.size() >= 10)
            break;
        std::string line = n.body;
        size_t end = line.find(". ");
        if (end != std::string::npos)
            line = line.substr(0, end + 1);
        if (line.size() > 140) {
            line = line.substr(0, line.rfind(' ', 140));
            line += "...";
        }
        items.push_back({n.title, line, ui::news_tag(n.kind).col, 0, 0});
    }
    if (items.empty())
        items.push_back({"SILLY SEASON",
                         d.es ? "El paddock empieza a hablar después de las primeras carreras" : "The paddock starts talking after the first races",
                         ui::kMuted, 0, 0});
    float sep = 56 * k, total = 0;
    for (Item &it : items) {
        it.hw = ui::text_w(p, hs, it.head) + 12 * k;
        it.w = 18 * k + it.hw + ui::text_w(p, s, it.text);
        total += it.w + sep;
    }
    float off = std::fmod(ui::clock_s() * 70 * k, total);
    ov::ClipScope clip(p, cx, y, cw, h);
    for (float base = cx - off; base < cx + cw; base += total) {
        float ix = base;
        for (const Item &it : items) {
            if (ix + it.w > cx && ix < cx + cw) {
                ui::para(p, ix, y + h / 2 - 6 * k, 8 * k, 12 * k, 4 * k, it.col);
                ov::TextStyle hc = hs;
                hc.rgba = ui::shade(it.col, 1.3f);
                ov::text(p, ix + 18 * k, y + h / 2, hc, it.head);
                ov::text(p, ix + 18 * k + it.hw, y + h / 2, s, it.text);
            }
            ix += it.w;
            // a red double slash between stories
            ui::para(p, ix + sep / 2 - 9 * k, y + h / 2 - 8 * k, 6 * k, 16 * k, 5 * k, 0xE10600A0);
            ui::para(p, ix + sep / 2 - 1 * k, y + h / 2 - 8 * k, 6 * k, 16 * k, 5 * k, 0xE1060060);
            ix += sep;
        }
    }
    ov::gradient(p, cx, y, 40 * k, h, 0x0F1017FF, 0x0F101700, true);
    ov::gradient(p, cx + cw - 60 * k, y, 60 * k, h, 0x0F101700, 0x0F1017FF, true);
}

void draw_board(ov::Paint *p, const ov::Input &in, float dt)
{
    using namespace paint_state;
    const OverlayData &d = g_ov;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p);
    auto T = [&](const char *es_text, const char *en_text) { return d.es ? es_text : en_text; };

    // tabs: Q/E, Left/Right, Tab, LB/RB
    int old_tab = tab;
    if (in.key_pressed(VK_RIGHT) || in.key_pressed('E') || in.pad_hit(ov::pad::rb) || in.pad_hit(ov::pad::right) ||
        (in.key_pressed(VK_TAB) && !in.key_down(VK_SHIFT)))
        tab = (tab + 1) % kPageCount;
    if (in.key_pressed(VK_LEFT) || in.key_pressed('Q') || in.pad_hit(ov::pad::lb) || in.pad_hit(ov::pad::left) ||
        (in.key_pressed(VK_TAB) && in.key_down(VK_SHIFT)))
        tab = (tab + kPageCount - 1) % kPageCount;

    float t_open = std::chrono::duration<float>(Clock::now() - opened_at).count();
    float oa = ui::ease_out(t_open / 0.3f);
    const PageDef &page = kPages[tab];

    // the backdrop: the game dims behind the hub, with a red light in one corner and the page's in the other
    ov::rect(p, 0, 0, W, H, ui::alpha(0x05060BEA, oa));
    ui::glow(p, W * 0.08f, H * 0.0f, 900 * k, ui::alpha(0xE1060036, oa));
    ui::glow(p, W * 0.95f, H * 1.0f, 900 * k, ui::alpha(page.accent, 0.10f * oa));
    ov::OpacityScope fade(p, oa);
    float pw = std::min(W - 48 * k, 1800 * k), ph = std::min(H - 40 * k, 1010 * k);
    float px = std::round((W - pw) / 2), py = std::round((H - ph) / 2);
    float sc = 0.97f + 0.03f * ui::ease_back(t_open / 0.4f);
    ov::push_scale(p, sc, sc, W / 2, H / 2);

    // the panel: tinted glass with a deep shadow
    ui::shadow(p, px, py, pw, ph, k, 2.0f, 0x000000FF, 14);
    ov::gradient(p, px, py, pw, ph, 0x13141CF6, 0x0A0B10FA, false, 18 * k);
    ov::rect_rounded_outline(p, px, py, pw, ph, 18 * k, 1 * k, 0xFFFFFF1C);
    ov::gradient(p, px + 18 * k, py, pw * 0.45f, 2 * k, ui::kRed, 0xE1060000, true);

    // header: the mark and the wordmark, the window's status and the season
    float hh = 100 * k;
    {
        ov::ClipScope clip(p, px, py, pw, hh);
        ui::hatch(p, px + pw * 0.3f, py, pw * 0.7f, hh, k, 0xFFFFFF06, 9 * k);
        ov::gradient(p, px + pw * 0.3f, py, 320 * k, hh, 0x13141CFF, 0x13141C00, true);
        ui::glow(p, px + 120 * k, py + hh / 2, 260 * k, 0xE1060030);
        // speed stripes behind the wordmark
        for (int i = 0; i < 6; ++i) {
            float sx = px + 470 * k + i * 64 * k;
            ui::para(p, sx, py, 14 * k, hh, 40 * k, ui::alpha(0xE1060000 | 0x1C, 1.0f - i * 0.15f));
        }
        // a light sweep across the wordmark every few seconds
        float st = std::fmod(ui::clock_s(), 6.0f);
        if (st < 1.1f && !ui::g_lite) {
            float sx = px - 200 * k + (pw * 0.5f + 200 * k) * ui::ease_out(st / 1.1f);
            ui::para(p, sx, py, 60 * k, hh, 50 * k, 0xFFFFFF0C);
            ui::para(p, sx + 70 * k, py, 18 * k, hh, 50 * k, 0xFFFFFF08);
        }
    }
    ui::logo(p, px + 30 * k, py + 26 * k, 48 * k, k);
    ov::text(p, px + 118 * k, py + 20 * k, ui::lifted(ui::ts(38 * k, ui::kText, ov::align::left | ov::align::top, 2.4f * k), k, 0xE1060070),
             "SILLY SEASON");
    ov::text(p, px + 120 * k, py + 66 * k, ui::ts(12.5f * k, ui::kText2, ov::align::left | ov::align::top, 3.4f * k), T("MERCADO DE PILOTOS", "DRIVER MARKET"));
    {
        // the season, on the right
        float rx = px + pw - 32 * k;
        char season[16];
        std::snprintf(season, sizeof season, "%d", d.year);
        ov::TextStyle ys = ui::lifted(ui::ts(40 * k, ui::kText, ov::align::right | ov::align::top, 1.5f * k), k);
        ov::text(p, rx, py + 34 * k, ys, season);
        float yw = ui::text_w(p, ys, season);
        ov::text(p, rx, py + 18 * k, ui::ts(11.5f * k, ui::kMuted, ov::align::right | ov::align::top, 2.6f * k), T("TEMPORADA", "SEASON"));
        float lx = rx - std::max(yw, 90 * k) - 26 * k;
        ov::rect(p, lx, py + 24 * k, 1 * k, 52 * k, 0xFFFFFF18);
        // the transfer window: open while the season runs, closed once the grid is final
        const char *status = d.finalised ? T("PARRILLA CERRADA", "GRID CONFIRMED") : T("MERCADO ABIERTO", "TRANSFER WINDOW OPEN");
        char sub[48];
        std::snprintf(sub, sizeof sub, "%s %d", T("FICHAJES PARA", "MOVES FOR"), d.year + 1);
        ov::TextStyle ss_ = ui::ts(13.5f * k, ui::kText, ov::align::right | ov::align::top, 1.8f * k);
        float sw_ = std::max(ui::text_w(p, ss_, status), 150 * k);
        float bx = lx - 26 * k - sw_ - 40 * k, bw = sw_ + 40 * k + 14 * k;
        ov::rect_rounded(p, bx, py + 24 * k, bw, 52 * k, 10 * k, 0xFFFFFF08);
        ov::rect_rounded_outline(p, bx, py + 24 * k, bw, 52 * k, 10 * k, 1 * k, 0xFFFFFF14);
        uint32_t scol = d.finalised ? ui::kGold : ui::kGreen;
        if (d.finalised)
            ov::circle(p, bx + 22 * k, py + 50 * k, 5 * k, scol);
        else
            ui::live_dot(p, bx + 22 * k, py + 50 * k, 5 * k, scol);
        ov::text(p, bx + bw - 14 * k, py + 33 * k, ss_, status);
        ov::text(p, bx + bw - 14 * k, py + 55 * k, ui::ts(11.5f * k, ui::kMuted, ov::align::right | ov::align::top, 1.8f * k), sub);
    }
    float tk_h = 40 * k, body_top = py + hh + tk_h;
    draw_ticker(p, d, px, py + hh, pw, tk_h, k);

    // the footer: the controls and where you are
    float fh = 50 * k, fy = py + ph - fh;
    {
        ov::ClipScope clip(p, px, fy, pw, fh);
        ov::rect_rounded(p, px, fy - 20 * k, pw, fh + 20 * k, 18 * k, 0x00000036);
        ov::rect(p, px, fy, pw, 1 * k, 0xFFFFFF10);
        ov::TextStyle hs = ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::middle, 0.4f * k);
        float x = px + 28 * k, cy = fy + fh / 2 - 12 * k;
        auto hint = [&](std::initializer_list<const char *> keys, const char *what) {
            for (const char *key : keys)
                x += ui::keycap(p, x, cy, key, k) + 4 * k;
            x += 6 * k;
            ov::text(p, x, cy + 12 * k, hs, what);
            x += ui::text_w(p, hs, what) + 28 * k;
        };
        hint({"Q", "E", "LB", "RB"}, T("cambiar de página", "change page"));
        hint({T("Arriba", "Up"), T("Abajo", "Down")}, T("desplazar", "scroll"));
        if (tab == P_PRESS && d.press_pending)
            hint({"1", "2", "3"}, T("responder", "answer"));
        hint({"F7", "Esc", "B"}, T("cerrar", "close"));
        // page dots
        float dx = px + pw - 30 * k;
        for (int i = kPageCount - 1; i >= 0; --i) {
            bool on = i == tab;
            float dw = on ? 26 * k : 7 * k;
            dx -= dw;
            ov::rect_rounded(p, dx, fy + fh / 2 - 3.5f * k, dw, 7 * k, 3.5f * k, on ? ui::kRed : 0xFFFFFF26);
            if (on)
                ui::glow(p, dx + dw / 2, fy + fh / 2, 24 * k, 0xE1060070);
            dx -= 6 * k;
        }
    }

    // the side menu, in groups, with a highlight that slides to the open page
    float sw = 292 * k, menu_top = body_top + 16 * k;
    ov::rect(p, px, body_top, sw, fy - body_top, 0x0000001E);
    ov::rect(p, px + sw, body_top, 1 * k, fy - body_top, 0xFFFFFF10);
    float item_y[kPageCount];
    {
        float y = menu_top;
        int g = 0;
        for (int i = 0; i < kPageCount; ++i) {
            if (g < static_cast<int>(std::size(kGroups)) && kGroups[g].first == i) {
                y += (g ? 10 * k : 0);
                ov::text(p, px + 30 * k, y + 8 * k, ui::ts(11 * k, ui::kMuted, ov::align::left | ov::align::middle, 2.4f * k),
                         T(kGroups[g].es, kGroups[g].en));
                y += 22 * k;
                ++g;
            }
            item_y[i] = y;
            y += 44 * k;
        }
    }
    float ix = px + 14 * k, iw = sw - 28 * k, ih = 40 * k;
    float target = item_y[tab];
    if (menu_y < 0 || t_open < 0.05f)
        menu_y = target;
    menu_y += (target - menu_y) * std::min(1.0f, dt * 16);
    if (std::fabs(target - menu_y) < 0.5f)
        menu_y = target;
    ov::gradient(p, ix, menu_y, iw, ih, 0xE1060048, 0xE1060006, true, 9 * k);
    ov::rect_rounded_outline(p, ix, menu_y, iw, ih, 9 * k, 1 * k, 0xE1060040);
    ov::rect_rounded(p, ix - 2 * k, menu_y + 9 * k, 4 * k, ih - 18 * k, 2 * k, ui::kRed);
    ui::glow(p, ix, menu_y + ih / 2, 34 * k, 0xE1060090);
    for (int i = 0; i < kPageCount; ++i) {
        float iy = item_y[i];
        bool hov = in.inside(ix, iy, iw, ih);
        if (hov && in.clicked())
            tab = i;
        bool on = i == tab;
        if (hov && !on)
            ov::rect_rounded(p, ix, iy, iw, ih, 9 * k, 0xFFFFFF0A);
        uint32_t col = on ? ui::kText : hov ? 0xD8DAE2FF : ui::kText2;
        float tx = ix + 12 * k, ty = iy + ih / 2 - 15 * k;
        if (on) {
            ov::rect_rounded(p, tx, ty, 30 * k, 30 * k, 8 * k, ui::kRed);
            ov::rect_rounded(p, tx, ty, 30 * k, 14 * k, 8 * k, 0xFFFFFF1A);
        } else {
            ov::rect_rounded(p, tx, ty, 30 * k, 30 * k, 8 * k, hov ? 0xFFFFFF16 : 0xFFFFFF09);
        }
        ui::icon(p, kPages[i].icon, tx + 15 * k, ty + 15 * k, 16 * k, on ? 0xFFFFFFFF : col, k * 0.9f);
        ov::text(p, tx + 44 * k, iy + ih / 2, ui::ts(16.5f * k, col, ov::align::left | ov::align::middle, 0.2f * k), T(kPages[i].es, kPages[i].en));
        // badges: unread news, a waiting question, open rumours
        std::string badge;
        uint32_t bcol = ui::kRed;
        if (i == P_NEWS && d.unread > 0)
            badge = std::to_string(std::min(d.unread, 99));
        if (i == P_PRESS && d.press_pending) {
            badge = "!";
            bcol = ui::kAmber;
        }
        if (i == P_RUMOURS && !d.rumours.empty())
            badge = std::to_string(d.rumours.size()), bcol = 0x3A3D4AFF;
        if (!badge.empty()) {
            ov::TextStyle bs = ui::ts(12 * k, bcol == ui::kAmber ? 0x111111FF : ui::kText, ov::align::center | ov::align::middle);
            float bw = std::max(22 * k, ui::text_w(p, bs, badge) + 12 * k);
            float bx = ix + iw - bw - 8 * k;
            if (bcol != 0x3A3D4AFF)
                ui::glow(p, bx + bw / 2, iy + ih / 2, 22 * k, ui::alpha(bcol, 0.4f + 0.3f * ui::pulse(1.6f)));
            ov::rect_rounded(p, bx, iy + ih / 2 - 10 * k, bw, 20 * k, 10 * k, bcol);
            ov::text(p, bx + bw / 2, iy + ih / 2, bs, badge);
        }
    }
    // your team, at the foot of the menu
    if (d.has_player) {
        float ch = 92 * k, cx = px + 16 * k, cw = sw - 32 * k, cy = fy - ch - 16 * k;
        if (cy > item_y[kPageCount - 1] + ih + 12 * k) {
            ui::card(p, cx, cy, cw, ch, k);
            {
                ov::ClipScope clip(p, cx, cy, cw, ch);
                ov::gradient(p, cx, cy, cw, ch, ui::rgb(d.player_col, 0x55), ui::rgb(d.player_col, 0x08), true, 12 * k);
                for (int i = 0; i < 3; ++i)
                    ui::para(p, cx + cw - 80 * k + i * 22 * k, cy, 11 * k, ch, ch * 0.5f, 0xFFFFFF0A);
            }
            ov::text(p, cx + 16 * k, cy + 14 * k, ui::ts(10.5f * k, ui::kText2, ov::align::left | ov::align::top, 2.2f * k), T("TU EQUIPO", "YOUR TEAM"));
            ui::team_chip(p, cx + 16 * k, cy + 36 * k, d.player_code, d.player_col, k, 22);
            ov::text(p, cx + 84 * k, cy + 47 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle),
                     ui::fit(p, ui::ts(16 * k), d.player_team, cw - 100 * k));
            char con[48];
            std::snprintf(con, sizeof con, "%s %d", T("Contrato hasta", "Contract until"), d.player_contract);
            ov::text(p, cx + 16 * k, cy + 72 * k, ui::ts(12.5f * k, ui::kText2), d.player_contract > 0 ? con : T("Sin contrato", "No contract"));
        }
    }

    if (tab != old_tab) {
        scroll = scroll_target = 0;
        tab_at = Clock::now();
        press_sel = 0;
    }

    // the page: breadcrumb, title and what it shows, then its content in a scrolled, clipped area
    float cx = px + sw + 40 * k, cw = px + pw - 40 * k - cx, cy = body_top + 22 * k;
    float t_tab = std::chrono::duration<float>(Clock::now() - tab_at).count(), ta = ui::ease_out(t_tab / 0.25f);
    const PageDef &pg = kPages[tab];
    {
        float wx = cx + cw - 70 * k, wy = cy + 44 * k;
        ui::glow(p, wx, wy, 170 * k, ui::alpha(pg.accent, 0.12f * ta));
        ui::icon(p, pg.icon, wx, wy, 78 * k, ui::alpha(pg.accent, 0.11f * ta), k * 2.6f);
        int g = 0;
        for (int i = 0; i < static_cast<int>(std::size(kGroups)); ++i)
            if (kGroups[i].first <= tab)
                g = i;
        char crumb[96];
        std::snprintf(crumb, sizeof crumb, "SILLY SEASON   /   %s", T(kGroups[g].es, kGroups[g].en));
        ov::text(p, cx, cy, ui::ts(11.5f * k, ui::kMuted, ov::align::left | ov::align::top, 2.4f * k), crumb);
        ov::OpacityScope tf(p, ta);
        float slide = (1 - ta) * 18 * k;
        ov::text(p, cx - slide, cy + 15 * k, ui::lifted(ui::ts(34 * k, ui::kText, ov::align::left | ov::align::top, 0.6f * k), k),
                 T(pg.es, pg.en));
        ov::text(p, cx - slide * 0.5f, cy + 55 * k, ui::ts(15 * k, ui::kText2), T(pg.desc_es, pg.desc_en));
        ov::gradient(p, cx, cy + 80 * k, 280 * k * ta, 2 * k, pg.accent, ui::alpha(pg.accent, 0), true);
    }
    cy += 96 * k;
    ov::Rect area{cx, cy, cw, fy - 14 * k - cy};

    bool pressing = tab == P_PRESS && d.press_pending;
    if (!pressing) {
        if (in.key_typed(VK_DOWN) || in.pad_hit(ov::pad::down))
            scroll_target += 90 * k;
        if (in.key_typed(VK_UP) || in.pad_hit(ov::pad::up))
            scroll_target -= 90 * k;
        if (in.key_typed(VK_NEXT))
            scroll_target += area.h * 0.85f;
        if (in.key_typed(VK_PRIOR))
            scroll_target -= area.h * 0.85f;
        if (in.key_pressed(VK_HOME))
            scroll_target = 0;
        if (in.key_pressed(VK_END))
            scroll_target = content_h;
    }
    scroll_target -= in.wheel * 80 * k;
    scroll_target = std::clamp(scroll_target, 0.0f, std::max(0.0f, content_h - area.h));
    scroll += (scroll_target - scroll) * std::min(1.0f, dt * 14);
    if (std::fabs(scroll_target - scroll) < 0.5f)
        scroll = scroll_target;

    {
        // a little room around the area so shadows and glows aren't cut flat at the sides
        ov::ClipScope clip(p, area.x - 12 * k, area.y, area.w + 24 * k, area.h);
        Ctx c{p, in, area, k, d.es, t_tab};
        float y0 = area.y - scroll;
        float h = 0;
        switch (tab) {
        case P_PADDOCK: h = page_paddock(c, y0); break;
        case P_NEWS: h = page_news(c, y0); break;
        case P_PRESS: h = page_press(c, y0, in); break;
        case P_RUMOURS: h = page_rumours(c, y0); break;
        case P_DEALS: h = page_deals(c, y0); break;
        case P_GRID: h = page_grid(c, y0); break;
        case P_DRIVERS: h = page_drivers(c, y0, false); break;
        case P_CONTRACTS: h = page_contracts(c, y0); break;
        case P_STOCK: h = page_stock(c, y0); break;
        case P_F2: h = page_f2(c, y0); break;
        case P_STAFF: h = page_staff(c, y0); break;
        case P_FREE: h = page_drivers(c, y0, true); break;
        case P_HISTORY: h = page_history(c, y0); break;
        default: break;
        }
        content_h = h + 16 * k;
    }
    // fade the content's edges and draw a scroll bar when it overflows
    if (content_h > area.h + 1) {
        if (scroll > 1)
            ov::gradient(p, area.x - 12 * k, area.y, area.w + 24 * k, 26 * k, 0x101118FF, 0x10111800);
        if (scroll < content_h - area.h - 1)
            ov::gradient(p, area.x - 12 * k, area.y + area.h - 34 * k, area.w + 24 * k, 34 * k, 0x0C0D1200, 0x0C0D12FF);
        float track_x = px + pw - 18 * k, frac = area.h / content_h;
        float th = std::max(40 * k, area.h * frac), ty = area.y + (area.h - th) * (scroll / (content_h - area.h));
        ov::rect_rounded(p, track_x, area.y, 4 * k, area.h, 2 * k, 0xFFFFFF0A);
        ov::gradient(p, track_x, ty, 4 * k, th, 0xE10600FF, 0x8A0400FF, false, 2 * k);
        ui::glow(p, track_x + 2 * k, ty + th / 2, 20 * k, 0xE1060050);
    }
    ov::pop_transform(p);
}

// the breaking-news lower third: it wipes in piece by piece like a broadcast graphic
void draw_banner(ov::Paint *p)
{
    using namespace paint_state;
    auto now = Clock::now();
    if (!banner_on) {
        if (g_ov.banners.empty())
            return;
        banner = std::move(g_ov.banners.front());
        g_ov.banners.pop_front();
        banner_on = true;
        banner_at = now;
    }
    float t = std::chrono::duration<float>(now - banner_at).count();
    constexpr float in_s = 0.8f, hold_s = 7.5f, out_s = 0.5f;
    if (t > in_s + hold_s + out_s) {
        banner_on = false;
        return;
    }
    bool es = g_ov.es;
    float out = t > in_s + hold_s ? ui::ease_out((t - in_s - hold_s) / out_s) : 0;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = std::min(1040 * k, safe.w), pad = 22 * k;
    bool press = banner.kind == "press";
    ui::Tag kind = ui::news_tag(banner.kind);
    uint32_t accent = press ? ui::kAmber : kind.col;
    bool hard = !press && (banner.kind == "official" || banner.kind == "retirement" || banner.kind == "sacking" || banner.kind == "collapse");
    std::string text = banner.body;
    if (press)
        text += es ? "   (F7 para responder)" : "   (F7 to answer)";
    float logo_w = 92 * k;
    ov::TextStyle body = ui::ts(19 * k, 0xDDDFE6FF, ov::align::left | ov::align::top, 0, w - logo_w - 2 * pad - 12 * k);
    body.line_height = 1.3f;
    ov::TextStyle title = ui::lifted(ui::ts(25 * k, 0xFFFFFFFF, ov::align::left | ov::align::top, 0.4f * k, w - logo_w - 2 * pad), k);
    float th = ui::text_h(p, title, banner.title), bh = ui::text_h(p, body, text);
    float tag_h = 40 * k, h = tag_h + 16 * k + th + 10 * k + bh + pad + 6 * k;
    float x = safe.x, y = safe.y + safe.h - h - 70 * k;
    // the pieces arrive one after another: the brand block, the red label, the strip, then the story
    float a_logo = ui::ease_out(t / 0.3f), a_tag = ui::ease_out((t - 0.12f) / 0.3f), a_strip = ui::ease_out((t - 0.24f) / 0.3f),
          a_body = ui::ease_out((t - 0.32f) / 0.42f), a_text = ui::ease_out((t - 0.5f) / 0.3f);
    ov::OpacityScope fade(p, 1 - out);
    ov::push_translate(p, -out * 60 * k, 0);
    ui::glow(p, x + w * 0.3f, y + h * 0.6f, w * 0.55f, ui::alpha(accent, 0.12f * a_body));
    ui::shadow(p, x, y + tag_h, w * a_body, h - tag_h, k, 1.6f * a_body);

    // the story panel, wiping open from the left
    float by = y + tag_h, bhh = h - tag_h;
    if (a_body > 0) {
        ov::ClipScope clip(p, x, by, w * a_body, bhh);
        ov::gradient(p, x, by, w, bhh, 0x15161FF4, 0x0C0D12F4, false, 0);
        ov::gradient(p, x + logo_w, by, w * 0.6f, bhh, ui::alpha(accent, 0.14f), ui::alpha(accent, 0), true);
        ui::hatch(p, x + w * 0.55f, by, w * 0.45f, bhh, k, 0xFFFFFF05, 10 * k);
        ov::rect(p, x, by, w, 3 * k, accent);
        ui::glow(p, x + w * a_body - 4 * k, by + 1.5f * k, 30 * k, ui::alpha(accent, 0.8f * (1 - a_body) + 0.2f));
        // time left: a line that runs out along the bottom
        float left = std::clamp(1 - (t - in_s) / hold_s, 0.0f, 1.0f);
        ov::rect(p, x, by + bhh - 3 * k, w, 3 * k, 0xFFFFFF10);
        ov::gradient(p, x, by + bhh - 3 * k, w * left, 3 * k, ui::alpha(accent, 0.3f), accent, true);
        ov::OpacityScope tf(p, a_text);
        float tx = x + logo_w + pad - (1 - a_text) * 16 * k;
        ov::text(p, tx, by + 16 * k, title, banner.title);
        ov::text(p, tx, by + 16 * k + th + 10 * k, body, text);
    }
    // the brand block on the left of the panel, full height
    if (a_logo > 0) {
        float lh = h * a_logo;
        ov::ClipScope clip(p, x, y + h - lh, logo_w, lh);
        ov::gradient(p, x, y, logo_w, h, 0x1C1D27FF, 0x0B0C10FF, false, 0);
        ui::hatch(p, x, y, logo_w, h, k, 0xFFFFFF08, 8 * k);
        ui::glow(p, x + logo_w / 2, y + h / 2, logo_w * 0.9f, 0xE1060050);
        ui::logo(p, x + logo_w / 2 - 34 * k, y + h / 2 - 24 * k, 48 * k, k);
        ov::rect(p, x + logo_w - 3 * k, y, 3 * k, h, ui::kRed);
    }
    // the label: BREAKING NEWS (or PRESS), with a shine running over it
    const char *label = press ? (es ? "PRENSA" : "PRESS") : (es ? "ÚLTIMA HORA" : "BREAKING NEWS");
    ov::TextStyle ls = ui::ts(18 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 2.0f * k);
    float lw = ui::text_w(p, ls, label) + 64 * k;
    float lx = x + logo_w;
    if (a_tag > 0) {
        ov::ClipScope clip(p, lx, y, (lw + 16 * k) * a_tag, tag_h);
        uint32_t lc = press ? 0xC77F00FF : hard ? ui::kRed : 0xC2050AFF;
        ui::para(p, lx, y, lw + 14 * k, tag_h, 14 * k, lc);
        ui::para(p, lx, y, lw + 14 * k, tag_h * 0.45f, 6 * k, 0xFFFFFF1E);
        ui::live_dot(p, lx + 22 * k, y + tag_h / 2, 4.5f * k, 0xFFFFFFFF, 1.2f);
        ov::text(p, lx + 38 * k, y + tag_h / 2, ui::lifted(ls, k), label);
        float sh = std::fmod(t + 0.4f, 3.0f) / 0.9f;
        if (sh < 1)
            ui::para(p, lx - 40 * k + (lw + 80 * k) * sh, y, 26 * k, tag_h, 14 * k, 0xFFFFFF38);
    }
    // the kind and the brand on a white strip next to it
    if (a_strip > 0) {
        ov::TextStyle ks = ui::ts(14.5f * k, 0x111218FF, ov::align::left | ov::align::middle, 1.6f * k);
        std::string kind_text = std::string("SILLY SEASON  ·  ") + (es ? kind.es : kind.en);
        float kw = ui::text_w(p, ks, kind_text) + 40 * k;
        float sx = lx + lw + 2 * k;
        ov::ClipScope clip(p, sx, y, (kw + 20 * k) * a_strip, tag_h);
        ui::para(p, sx, y + 6 * k, kw + 14 * k, tag_h - 6 * k, 12 * k, 0xF2F3F7FF);
        ui::para(p, sx + kw - 2 * k, y + 6 * k, 14 * k, tag_h - 6 * k, 12 * k, accent);
        ov::text(p, sx + 22 * k, y + tag_h / 2 + 3 * k, ks, kind_text);
    }
    ov::pop_transform(p);
}

void paint(ov::Paint *p, void *)
{
    using namespace paint_state;
    auto now = Clock::now();
    float dt = last_frame.time_since_epoch().count() ? std::chrono::duration<float>(now - last_frame).count() : 0.016f;
    last_frame = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    ov::Input in = ov::input(p);
    std::lock_guard lock(g_ov_lock);
    ui::g_lite = !g_ov.effects;
    bool usable = g_ov.career && g_ov.enabled && !g_in_session.load();
    bool open = g_board_open.load();
    bool toggle = in.key_pressed(VK_F7) || (in.pad_held(ov::pad::back) && in.pad_hit(ov::pad::y));
    if (open && (!usable || toggle || in.key_pressed(VK_ESCAPE) || in.pad_hit(ov::pad::b))) {
        open = false;
        ov::focus(false);
    } else if (!open && usable && toggle) {
        open = true;
        ov::focus(true);
        scroll = scroll_target = 0;
        press_sel = 0;
        menu_y = -1;
        opened_at = tab_at = now;
        // a waiting question opens on the press page
        if (g_ov.press_pending)
            tab = P_PRESS;
        g_seen_request = true;
    }
    g_board_open = open;
    if (open)
        draw_board(p, in, dt);
    else if (usable)
        draw_banner(p);
}
