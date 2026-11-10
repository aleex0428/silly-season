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
    if (g_lite || w <= 0 || h <= 0)
        return;
    // a line from (x + o, y + h) up to (x + o + h, y), cut to [x, x + w]: no clip needed
    for (float o = -h; o < w; o += step) {
        float t0 = std::max(0.0f, -o / h), t1 = std::min(1.0f, (w - o) / h);
        if (t0 < t1)
            ov::line(p, x + o + t0 * h, y + h - t0 * h, x + o + t1 * h, y + h - t1 * h, 1.0f * k, col);
    }
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
enum class Icon { paddock, news, press, rumours, deals, grid, drivers, contracts, stock, f2, staff, free, history, scout, chain, future, standings };

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
    case Icon::chain:
        ov::rect_rounded_outline(p, cx - h, cy - h * 0.45f, h * 1.1f, h * 0.9f, h * 0.45f, t * 1.2f, c);
        ov::rect_rounded_outline(p, cx - h * 0.1f, cy - h * 0.45f, h * 1.1f, h * 0.9f, h * 0.45f, t * 1.2f, c);
        break;
    case Icon::scout:
        ov::circle_outline(p, cx - h * 0.48f, cy + h * 0.2f, h * 0.42f, t * 1.2f, c);
        ov::circle_outline(p, cx + h * 0.48f, cy + h * 0.2f, h * 0.42f, t * 1.2f, c);
        ov::rect_rounded(p, cx - h * 0.75f, cy - h * 0.75f, h * 0.5f, h * 0.6f, 2 * k, c);
        ov::rect_rounded(p, cx + h * 0.25f, cy - h * 0.75f, h * 0.5f, h * 0.6f, 2 * k, c);
        ov::rect(p, cx - h * 0.25f, cy - h * 0.35f, h * 0.5f, t, c);
        break;
    case Icon::future: { // an arrow up and to the right, out of a circle
        ov::circle_outline(p, cx - h * 0.2f, cy + h * 0.2f, h * 0.7f, t, c);
        ov::line(p, cx - h * 0.2f, cy + h * 0.2f, cx + h * 0.85f, cy - h * 0.85f, t * 1.2f, c);
        float pts[6] = {cx + h * 0.95f, cy - h * 0.95f, cx + h * 0.25f, cy - h * 0.85f, cx + h * 0.85f, cy - h * 0.25f};
        ov::polygon(p, pts, 3, c);
        break;
    }
    case Icon::standings: // a podium
        ov::rect_rounded(p, cx - h * 0.35f, cy - h * 0.8f, h * 0.7f, h * 1.8f, 2 * k, c);
        ov::rect_rounded(p, cx - h * 1.05f, cy - h * 0.25f, h * 0.62f, h * 1.25f, 2 * k, c);
        ov::rect_rounded(p, cx + h * 0.43f, cy + h * 0.15f, h * 0.62f, h * 0.85f, 2 * k, c);
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
    if (k == "deadline") return {"DEADLINE DAY", "DEADLINE DAY", kRed};
    if (k == "domino") return {"EFECTO DOMINÓ", "DOMINO", 0xFF9F0AFF};
    if (k == "refusal") return {"RECHAZO", "TURNED DOWN", 0xFF6B6BFF};
    if (k == "wants_out") return {"QUIERE SALIR", "WANTS OUT", 0xFF375FFF};
    if (k == "strategy") return {"PLAN", "TEAM PLAN", 0x5E5CE6FF};
    if (k == "awards") return {"PREMIOS", "AWARDS", kGold};
    if (k == "media") return {"MEDIOS", "MEDIA", 0x8E9AAFFF};
    if (k == "scout") return {"OJEADOR", "SCOUT", 0x40C8E0FF};
    if (k == "number") return {"DORSAL", "NUMBER", 0x64D2FFFF};
    if (k == "swap") return {"EQUIPO HERMANO", "SISTER TEAMS", 0xFF9F0AFF};
    if (k == "sponsor") return {"DINERO", "MONEY", kGreen};
    if (k == "principal") return {"JEFE DE EQUIPO", "TEAM BOSS", kDanger};
    if (k == "farewell") return {"DESPEDIDA", "FAREWELL", 0xBF5AF2FF};
    if (k == "academy") return {"ACADEMIA", "ACADEMY", 0x32D74BFF};
    if (k == "reserve") return {"RESERVAS", "RESERVES", 0x40C8E0FF};
    return {"MERCADO", "MARKET", 0xA0A3B1FF};
}

inline Tag strategy_tag(const std::string &k)
{
    if (k == "title") return {"A POR EL TÍTULO", "TITLE PUSH", kGold};
    if (k == "rebuild") return {"RECONSTRUCCIÓN", "REBUILD", 0x5E5CE6FF};
    if (k == "youth") return {"CANTERA", "YOUTH", 0x32D74BFF};
    if (k == "survival") return {"SUPERVIVENCIA", "SURVIVAL", 0xFF9F0AFF};
    return {"ASPIRANTE", "CONTENDER", 0x8E9AAFFF};
}

inline Tag trait_tag(const std::string &k)
{
    if (k == "ambition") return {"AMBICIOSO", "AMBITIOUS", kAmber};
    if (k == "loyalty") return {"LEAL", "LOYAL", kBlue};
    return {"DINERO", "MONEY", kGreen};
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
    if (k == "promoted") return {"SUBE", "PROMOTED", 0x64D2FFFF};
    if (k == "demoted") return {"BAJA", "DEMOTED", 0xFF9F0AFF};
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
// 1.1.0 hub: each page's scroll, kept while the panel is closed; the side a new page slides in from; the
// scroll bar held by the mouse; the press answer just sent (shown until the paddock reacts)
std::array<float, 32> page_scroll{};
int tab_dir = 1;
bool dragging = false;
Clock::time_point answered_at;
std::string answered_q, answered_a;
}

void choose_answer(int i)
{
    if (i < 0 || i >= static_cast<int>(g_ov.press_answers.size()))
        return;
    g_press_answer_id = g_ov.press_id;
    g_press_from_hub = false;
    g_press_answer = i;
    paint_state::answered_at = paint_state::Clock::now();
    paint_state::answered_q = g_ov.press_question;
    paint_state::answered_a = g_ov.press_answers[static_cast<size_t>(i)];
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
    {ui::Icon::future, "Tu futuro", "Your Future", "Quién te quiere, cuánto vales y qué se dice de ti",
     "Who wants you, what you're worth and what the paddock says", ui::kRed},
    {ui::Icon::rumours, "Rumores", "Rumours", "Quién suena para dónde y quién lo dice", "Who is linked where, and who says so",
     0xBF5AF2FF},
    {ui::Icon::deals, "Fichajes", "Deals", "Contratos firmados y los movimientos de la temporada",
     "Contracts signed and this season's moves", ui::kGreen},
    {ui::Icon::chain, "Transfer Centre", "Transfer Centre", "Cada fichaje que abre un asiento, y quién entra después",
     "Every move that opens a seat, and who comes in next", 0xFF9F0AFF},
    {ui::Icon::grid, "Parrilla", "Grid", "La parrilla del año que viene, equipo a equipo", "Next season's line-up, team by team",
     ui::kRed},
    {ui::Icon::standings, "Clasificación", "Standings", "El campeonato de pilotos y el de constructores, con los resultados reales",
     "The drivers' and constructors' championships, from the real results", ui::kGold},
    {ui::Icon::drivers, "Pilotos", "Drivers", "Todos los pilotos de la parrilla por valor de mercado",
     "Every driver on the grid, ranked by market value", 0x64D2FFFF},
    {ui::Icon::contracts, "Contratos", "Contracts", "Cuándo termina el contrato de cada piloto",
     "When every driver's deal runs out", ui::kAmber},
    {ui::Icon::stock, "Bolsa", "Stock", "Quién ha subido y quién ha bajado en la última carrera",
     "Whose value moved at the last race", ui::kGreen},
    {ui::Icon::f2, "Fórmula 2", "Formula 2", "El campeonato donde la F1 busca a sus próximas estrellas",
     "Where F1 finds its next stars", 0x64D2FFFF},
    {ui::Icon::scout, "Ojeador", "Scouting", "Los pilotos que encajan en tu equipo el año que viene",
     "The drivers who fit your team next season", 0x40C8E0FF},
    {ui::Icon::staff, "Técnicos", "Engineers", "Los directores técnicos detrás de cada coche",
     "The technical directors behind every car", 0x5E5CE6FF},
    {ui::Icon::free, "Agentes libres", "Free agents", "Pilotos sin asiento, ordenados por potencial",
     "Drivers without a seat, ranked by potential", 0x40C8E0FF},
    {ui::Icon::history, "Historial", "History", "Cada movimiento que ha hecho el mercado", "Every move the market has made",
     0xA0A3B1FF},
};
constexpr int kPageCount = static_cast<int>(sizeof kPages / sizeof kPages[0]);
enum PageId { P_PADDOCK, P_NEWS, P_PRESS, P_FUTURE, P_RUMOURS, P_DEALS, P_TRANSFERS, P_GRID, P_STANDINGS, P_DRIVERS, P_CONTRACTS, P_STOCK, P_F2, P_SCOUT, P_STAFF, P_FREE, P_HISTORY };
static_assert(P_HISTORY + 1 == static_cast<int>(sizeof kPages / sizeof kPages[0]), "kPages and PageId must match");

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

// ---------------------------------------------------------------------------------------------------
// 1.1.0 phase 4: rows you can pick, the cards they open, the tables' search, sort and filter, and the
// full-screen moments (the grid reveal, the signing card, the Team HQ widget)

namespace ui4 {
using Clock = std::chrono::steady_clock;
enum Kind { K_DRIVER = 1, K_TEAM = 2, K_SEARCH = 3 };
struct Row {
    float y, h;
    std::string id;
    int kind;
};
std::vector<Row> rows;  // this frame's rows, in order
std::vector<Row> rows_prev; // last frame's, complete (a card opened from a list steps through them)
size_t rows_last = 0;   // last frame's: Up/Down pick a row when the page has any
// the list a card was opened from: Left/Right step through it
std::vector<std::string> browse;
int browse_kind = 0;
// the global search (/ on any page without a table)
std::string gquery;
int gsel = 0;
int sel = -1;           // the picked row
bool sel_moved = false; // the keys moved it: scroll it into view
bool sel_open = false;  // Enter / A: open it
int modal = 0;          // the open card: K_DRIVER, K_TEAM or 0
std::string modal_id;
Clock::time_point modal_at;
std::vector<std::pair<int, std::string>> modal_back; // the cards it was opened from
// the tables
std::string query;
bool typing = false;
int sort_key = 0;
std::string filter;          // "" everyone, "trait:<name>" or a team name
bool sort_next = false, filter_next = false;
// the moments
bool reveal_on = false, reveal_manual = false;
Clock::time_point reveal_at;
bool signing_on = false;
Clock::time_point signing_at;
UiSigning signing;
Clock::time_point widget_at;
bool widget_hq = false;
int widget_unread = -1;

void open_card(int kind, const std::string &id)
{
    if (modal) {
        modal_back.push_back({modal, modal_id});
        browse.clear(); // a card opened from another card: nothing to step through
    }
    modal = kind;
    modal_id = id;
    modal_at = Clock::now();
}

// a card opened from a page's row: the page's rows of that kind become the list to step through
void open_from_list(int kind, const std::string &id)
{
    browse.clear();
    browse_kind = kind;
    const std::vector<Row> &src = rows_prev.empty() ? rows : rows_prev;
    for (const Row &r : src)
        if (r.kind == kind && !r.id.empty() && std::find(browse.begin(), browse.end(), r.id) == browse.end())
            browse.push_back(r.id);
    if (std::find(browse.begin(), browse.end(), id) == browse.end())
        browse.clear();
    open_card(kind, id);
    if (modal_back.empty() && browse.size() < 2)
        browse.clear();
}

// the card's place in its list (-1: not opened from one)
int browse_index()
{
    if (browse.size() < 2 || modal != browse_kind || !modal_back.empty())
        return -1;
    auto it = std::find(browse.begin(), browse.end(), modal_id);
    return it == browse.end() ? -1 : static_cast<int>(it - browse.begin());
}

void browse_step(int step)
{
    int i = browse_index();
    if (i < 0)
        return;
    int n = static_cast<int>(browse.size());
    modal_id = browse[static_cast<size_t>((i + step + n) % n)];
    modal_at = Clock::now() - std::chrono::milliseconds(120); // a quicker entrance than a fresh card
}

void open_search()
{
    gquery.clear();
    gsel = 0;
    browse.clear();
    modal_back.clear();
    modal = K_SEARCH;
    modal_id.clear();
    modal_at = Clock::now();
}

void close_card()
{
    if (!modal_back.empty()) {
        modal = modal_back.back().first;
        modal_id = modal_back.back().second;
        modal_back.pop_back();
    } else {
        modal = 0;
        modal_id.clear();
    }
    modal_at = Clock::now() - std::chrono::seconds(1);
}

void reset_page()
{
    rows_last = 0;
    sel = -1;
    query.clear();
    typing = false;
    sort_key = 0;
    filter.clear();
    sort_next = filter_next = false;
}

std::string lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    return s;
}

std::string upper(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
    return s;
}

// the search and the filter let this driver through
bool matches(const std::string &name, const std::string &team, const std::string &trait)
{
    if (!query.empty()) {
        std::string q = lower(query);
        if (lower(name).find(q) == std::string::npos && lower(team).find(q) == std::string::npos)
            return false;
    }
    if (filter.rfind("trait:", 0) == 0)
        return trait == filter.substr(6);
    return filter.empty() || team == filter;
}

// typed text into a search box (UTF-32 to UTF-8): the table's, or the global search's
void type_into(const ov::Input &in, std::string &query = ui4::query)
{
    for (uint32_t i = 0; i < in.char_count && i < 16; ++i) {
        uint32_t ch = in.chars[i];
        if (ch < 32 || ch == 127 || query.size() > 24)
            continue;
        if (ch < 0x80) {
            query += static_cast<char>(ch);
        } else if (ch < 0x800) {
            query += static_cast<char>(0xC0 | (ch >> 6));
            query += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    if (in.key_typed(VK_BACK) && !query.empty()) {
        query.pop_back();
        while (!query.empty() && (static_cast<unsigned char>(query.back()) & 0xC0) == 0x80)
            query.pop_back();
        if (!query.empty() && (static_cast<unsigned char>(query.back()) & 0xC0) == 0xC0)
            query.pop_back();
    }
}
} // namespace ui4

// a row you can pick: clicking it, or Enter/A on it, opens its card. Call it for every row, seen or not.
bool pick_row(const Ctx &c, float x, float y, float w, float h, const std::string &id, int kind)
{
    using namespace ui4;
    ui4::rows.push_back({y, h, id, kind});
    int i = static_cast<int>(ui4::rows.size()) - 1;
    if (!id.empty() && c.hover(x, y, w, h) && c.in.clicked()) {
        sel = i;
        open_from_list(kind, id);
    }
    bool picked = sel == i;
    if (picked && sel_open && !id.empty()) {
        sel_open = false;
        open_from_list(kind, id);
    }
    return picked;
}

// the picked row's frame, drawn over its card
void pick_mark(const Ctx &c, float x, float y, float w, float h, bool picked)
{
    if (!picked)
        return;
    float k = c.k;
    ov::rect_rounded_outline(c.p, x - 2 * k, y - 2 * k, w + 4 * k, h + 4 * k, 11 * k, 2 * k, ui::alpha(ui::kRed, 0.65f + 0.35f * ui::pulse(1.4f)));
    ui::triangle(c.p, x - 10 * k, y + h / 2, 6 * k, false, ui::kRed);
}

// search, sort and filter above a table. sorts: the sort keys' labels; teams: the team names to filter by
float toolbar(const Ctx &c, float y, const std::vector<std::pair<const char *, const char *>> &sorts, std::vector<std::string> teams,
              int shown, const char *what_es, const char *what_en, bool traits = true)
{
    using namespace ui4;
    float k = c.k, h = 38 * k, x = c.area.x;
    // the filter's options: everyone, the three characters (for drivers), then the teams
    std::vector<std::string> options = {""};
    if (traits)
        options.insert(options.end(), {"trait:ambition", "trait:loyalty", "trait:money"});
    std::sort(teams.begin(), teams.end());
    teams.erase(std::unique(teams.begin(), teams.end()), teams.end());
    for (const std::string &t : teams)
        if (!t.empty())
            options.push_back(t);
    auto at = std::find(options.begin(), options.end(), filter);
    size_t fi = at == options.end() ? 0 : static_cast<size_t>(at - options.begin());
    if (filter_next) {
        filter = options[(fi + 1) % options.size()];
        filter_next = false;
        sel = -1;
    }
    if (sort_next) {
        sort_key = sorts.empty() ? 0 : (sort_key + 1) % static_cast<int>(sorts.size());
        sort_next = false;
        sel = -1;
    }
    // the search box
    float bw = 380 * k;
    bool hov = c.hover(x, y, bw, h);
    if (hov && c.in.clicked())
        typing = true;
    ov::rect_rounded(c.p, x, y, bw, h, 9 * k, typing ? 0xFFFFFF14 : hov ? 0xFFFFFF10 : 0xFFFFFF0A);
    ov::rect_rounded_outline(c.p, x, y, bw, h, 9 * k, 1 * k, typing ? ui::alpha(ui::kRed, 0.8f) : 0xFFFFFF1C);
    ov::circle_outline(c.p, x + 20 * k, y + h / 2 - 2 * k, 6.5f * k, 1.6f * k, ui::kText2);
    ov::line(c.p, x + 25 * k, y + h / 2 + 3 * k, x + 29 * k, y + h / 2 + 7 * k, 1.8f * k, ui::kText2);
    ov::TextStyle qs = ui::ts(15 * k, query.empty() ? ui::kMuted : ui::kText, ov::align::left | ov::align::middle);
    std::string shown_q = query.empty() && !typing ? std::string(c.T("Buscar piloto o equipo   ( / )", "Search a driver or team   ( / )")) : query;
    ov::text(c.p, x + 40 * k, y + h / 2, qs, ui::fit(c.p, qs, shown_q, bw - 60 * k));
    if (typing && std::fmod(ui::clock_s(), 1.0f) < 0.55f)
        ov::rect(c.p, x + 42 * k + ui::text_w(c.p, qs, query), y + 10 * k, 1.5f * k, h - 20 * k, ui::kText);
    // the chips: sort and filter
    auto chip = [&](float cx, const std::string &label, const char *key) {
        ov::TextStyle ls = ui::ts(12.5f * k, ui::kText, ov::align::left | ov::align::middle, 1.2f * k);
        float w = ui::text_w(c.p, ls, label) + 30 * k + ui::text_w(c.p, ls, key);
        bool hv = c.hover(cx, y, w, h);
        ov::rect_rounded(c.p, cx, y, w, h, 9 * k, hv ? 0xFFFFFF14 : 0xFFFFFF0A);
        ov::rect_rounded_outline(c.p, cx, y, w, h, 9 * k, 1 * k, 0xFFFFFF1C);
        ov::text(c.p, cx + 12 * k, y + h / 2, ls, label);
        ov::text(c.p, cx + w - 12 * k, y + h / 2, ui::ts(12 * k, ui::kMuted, ov::align::right | ov::align::middle, 1 * k), key);
        return std::make_pair(w, hv && c.in.clicked());
    };
    float cx = x + bw + 12 * k;
    if (!sorts.empty()) {
        std::string label = std::string(c.T("ORDEN: ", "SORT: ")) + c.T(sorts[static_cast<size_t>(sort_key)].first, sorts[static_cast<size_t>(sort_key)].second);
        auto [w, hit] = chip(cx, label, "S / X");
        if (hit)
            sort_next = true;
        cx += w + 10 * k;
    }
    {
        std::string label = filter.empty()                ? c.T("TODOS", "EVERYONE")
                            : filter == "trait:ambition"  ? c.T("AMBICIOSOS", "AMBITIOUS")
                            : filter == "trait:loyalty"   ? c.T("LEALES", "LOYAL")
                            : filter == "trait:money"     ? c.T("POR DINERO", "MONEY")
                                                          : filter;
        auto [w, hit] = chip(cx, std::string(c.T("FILTRO: ", "FILTER: ")) + label, "F / Y");
        if (hit)
            filter_next = true;
    }
    char count[48];
    std::snprintf(count, sizeof count, "%d %s", shown, c.T(what_es, what_en));
    ov::text(c.p, c.area.x + c.area.w, y + h / 2, ui::ts(13 * k, ui::kMuted, ov::align::right | ov::align::middle, 0.6f * k), count);
    return h + 16 * k;
}

// the Transfer Centre: the chains of moves, and the signings waiting for their seat
float page_transfers(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    if (d.chains.empty() && d.waiting.empty())
        return empty_state(c, y0, ui::Icon::deals, c.T("Todavía no hay cadenas", "No chains yet"),
                           c.T("Cuando un fichaje deja un asiento libre, la cadena empieza aquí.",
                               "When a signing leaves a seat open, the chain starts here."));
    const char *stage_es[4] = {"RUMOR", "CHARLAS", "ACUERDO", "OFICIAL"}, *stage_en[4] = {"RUMOUR", "TALKS", "AGREED", "OFFICIAL"};
    const uint32_t stage_col[4] = {0x8E9AAFFF, ui::kAmber, 0x64D2FFFF, ui::kGreen};
    auto link_card = [&](float x, float cy, float w, float h, const UiLink &l) {
        bool picked = pick_row(c, x, cy, w, h, l.driver_id, ui4::K_DRIVER);
        if (!c.visible(cy, h))
            return;
        Reveal r(c, 10);
        int sg = std::clamp(l.stage, 0, 3);
        ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), stage_col[sg]);
        {
            ov::ClipScope clip(c.p, x, cy, w, h);
            if (l.to_col)
                ov::gradient(c.p, x + w * 0.4f, cy, w * 0.6f, h, ui::rgb(l.to_col, 0x00), ui::rgb(l.to_col, 0x2A), true);
        }
        pick_mark(c, x, cy, w, h, picked);
        ov::text(c.p, x + 16 * k, cy + 22 * k, ui::ts(17 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(17 * k), l.driver, w - 120 * k));
        ui::tag(c.p, x + w - 14 * k - ui::text_w(c.p, ui::ts(11 * k), c.T(stage_es[sg], stage_en[sg])) - 24 * k, cy + 11 * k, c.T(stage_es[sg], stage_en[sg]),
                stage_col[sg], k, 10, sg == 3);
        if (sg < 3 && l.odds > 0) { // 1.1.0 phase 5: the chance it happens
            uint32_t oc = l.odds >= 66 ? ui::kGreen : l.odds >= 40 ? ui::kAmber : 0x8E9AAFFF;
            ov::text(c.p, x + w - 14 * k, cy + 46 * k, ui::ts(12.5f * k, oc, ov::align::right | ov::align::middle, 0.6f * k),
                     std::to_string(l.odds) + c.T("% probabilidad", "% chance"));
        }
        float fx = x + 16 * k, fy = cy + h - 34 * k;
        float fw = l.from_code.empty() ? ui::tag(c.p, fx, fy + 2 * k, c.T("LIBRE", "FREE"), ui::kMuted, k, 10) : ui::team_chip(c.p, fx, fy, l.from_code, l.from_col, k, 22);
        ui::flow_arrow(c.p, fx + fw + 10 * k, fy + 11 * k, 40 * k, k, stage_col[sg], sg >= 2 ? 40.0f : 18.0f);
        ui::team_chip(c.p, fx + fw + 62 * k, fy, l.to_code, l.to_col, k, 22);
        if (!l.waits.empty())
            ov::text(c.p, x + w - 14 * k, fy + 11 * k, ui::ts(11.5f * k, ui::kAmber, ov::align::right | ov::align::middle),
                     ui::fit(c.p, ui::ts(11.5f * k), std::string(c.T("espera a ", "waits for ")) + l.waits, 150 * k));
    };
    if (!d.chains.empty()) {
        char note[48];
        std::snprintf(note, sizeof note, "%zu %s", d.chains.size(), d.chains.size() == 1 ? c.T("cadena", "chain") : c.T("cadenas", "chains"));
        y += section_title(c, c.area.x, y, c.T("EFECTO DOMINÓ", "THE DOMINOES"), note);
        float w = 300 * k, h = 96 * k, gap = 54 * k;
        int per_line = std::max(1, static_cast<int>((c.area.w + gap) / (w + gap)));
        int n = 0;
        for (const std::vector<UiLink> &chain : d.chains) {
            ++n;
            if (c.visible(y, 24 * k)) {
                char head[48];
                std::snprintf(head, sizeof head, "%s %d  ·  %zu %s", c.T("CADENA", "CHAIN"), n, chain.size(), c.T("movimientos", "moves"));
                ov::text(c.p, c.area.x, y + 10 * k, ui::ts(11.5f * k, ui::kMuted, ov::align::left | ov::align::middle, 1.6f * k), head);
            }
            y += 26 * k;
            for (size_t i = 0; i < chain.size(); ++i) {
                int col = static_cast<int>(i) % per_line;
                if (i > 0 && col == 0)
                    y += h + 16 * k;
                float x = c.area.x + col * (w + gap);
                link_card(x, y, w, h, chain[i]);
                // the domino falling into the next
                if (i + 1 < chain.size() && col + 1 < per_line && c.visible(y, h)) {
                    float ax = x + w + 8 * k, ay = y + h / 2;
                    ui::flow_arrow(c.p, ax, ay, gap - 16 * k, k, ui::kRed, 30.0f);
                }
            }
            y += h + 26 * k;
        }
    }
    if (!d.waiting.empty()) {
        y += section_title(c, c.area.x, y, c.T("ESPERANDO ASIENTO", "WAITING FOR A SEAT"),
                           c.T("no se anuncian hasta que se vaya el piloto", "announced once the seat's driver has gone"));
        float w = (c.area.w - 16 * k) / 2, h = 96 * k;
        for (size_t i = 0; i < d.waiting.size(); ++i) {
            float x = c.area.x + (i % 2) * (w + 16 * k), cy = y + (i / 2) * (h + 14 * k);
            link_card(x, cy, w, h, d.waiting[i]);
        }
        y += ((d.waiting.size() + 1) / 2) * (h + 14 * k);
    }
    return y - y0;
}

// ---------------------------------------------------------------------------------------------------
// The cards: a driver's or a team's, over the hub

// a line of text you can click (it opens another card)
bool link_text(ov::Paint *p, const ov::Input &in, float x, float y, const ov::TextStyle &s, const std::string &t, float max_w)
{
    std::string shown = ui::fit(p, s, t, max_w);
    float w = ui::text_w(p, s, shown), h = s.size * 1.3f;
    float top = (s.align & ov::align::middle) ? y - h / 2 : y;
    bool hov = in.inside(x, top, w, h);
    ov::TextStyle st = s;
    if (hov)
        st.rgba = 0xFFFFFFFF;
    ov::text(p, x, y, st, shown);
    if (hov)
        ov::rect(p, x, top + h - 2, w, 1.2f, 0xFFFFFFA0);
    return hov && in.clicked();
}

float card_head(ov::Paint *p, float x, float y, float w, float k, const char *label)
{
    ui::para(p, x, y + 3 * k, 7 * k, 12 * k, 3 * k, ui::kRed);
    ov::text(p, x + 14 * k, y, ui::ts(12 * k, ui::kText2, ov::align::left | ov::align::top, 1.8f * k), label);
    ov::gradient(p, x, y + 22 * k, w, 1 * k, 0xFFFFFF1E, 0xFFFFFF00, true);
    return 32 * k;
}

// a 0..1 bar with its label and value
void card_bar(ov::Paint *p, float x, float y, float w, float k, const char *label, float frac, uint32_t col, const std::string &value)
{
    ov::text(p, x, y + 9 * k, ui::ts(14 * k, ui::kText2, ov::align::left | ov::align::middle), label);
    float bx = x + 130 * k, bw = w - 130 * k - 60 * k;
    ui::bar(p, bx, y + 5 * k, bw, 8 * k, std::clamp(frac, 0.0f, 1.0f), col);
    ov::text(p, x + w, y + 9 * k, ui::ts(14 * k, ui::kText, ov::align::right | ov::align::middle), value);
}

// the value chart: this season bright, last season faint behind it
void value_chart(ov::Paint *p, float x, float y, float w, float h, float k, const UiDriverCard &c, bool es, float grow)
{
    ov::rect_rounded(p, x, y, w, h, 10 * k, 0xFFFFFF06);
    float lo = 100, hi = 0;
    for (const std::vector<float> *v : {&c.values, &c.values_last})
        for (float f : *v)
            if (f > 0) {
                lo = std::min(lo, f);
                hi = std::max(hi, f);
            }
    if (hi <= 0) {
        ov::text(p, x + w / 2, y + h / 2, ui::ts(14 * k, ui::kMuted, ov::align::center | ov::align::middle),
                 es ? "Sin carreras todavía esta temporada" : "No races yet this season");
        return;
    }
    lo = std::floor(lo - 2), hi = std::ceil(hi + 2);
    float px = x + 44 * k, pw = w - 60 * k, py = y + 16 * k, ph = h - 44 * k;
    size_t n = std::max<size_t>({c.values.size(), c.values_last.size(), 10});
    for (int g = 0; g <= 4; ++g) {
        float gy = py + ph * g / 4.0f, v = hi - (hi - lo) * g / 4.0f;
        ov::rect(p, px, gy, pw, 1 * k, 0xFFFFFF0E);
        char b[16];
        std::snprintf(b, sizeof b, "%.0f", v);
        ov::text(p, px - 8 * k, gy, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::middle), b);
    }
    auto xy = [&](size_t i, float v) {
        return std::make_pair(px + pw * static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, n - 1)), py + ph * (hi - v) / std::max(1.0f, hi - lo));
    };
    auto draw = [&](const std::vector<float> &v, uint32_t col, float t, bool dots) {
        std::vector<float> pts;
        size_t upto = static_cast<size_t>(std::ceil(static_cast<float>(v.size()) * grow));
        for (size_t i = 0; i < v.size() && i < upto; ++i)
            if (v[i] > 0) {
                auto [a, b] = xy(i, v[i]);
                pts.push_back(a);
                pts.push_back(b);
            }
        if (pts.size() >= 4)
            ov::polyline(p, pts.data(), static_cast<uint32_t>(pts.size() / 2), t, col);
        if (dots)
            for (size_t i = 0; i + 1 < pts.size(); i += 2)
                ov::circle(p, pts[i], pts[i + 1], 3 * k, col);
        if (dots && pts.size() >= 2) {
            float lx = pts[pts.size() - 2], ly = pts.back();
            ui::glow(p, lx, ly, 18 * k, ui::alpha(col, 0.6f));
            ov::circle(p, lx, ly, 5 * k, col);
        }
    };
    draw(c.values_last, 0xFFFFFF30, 1.5f * k, false);
    draw(c.values, ui::kRed, 2.5f * k, true);
    ov::text(p, px, y + h - 12 * k, ui::ts(11 * k, ui::kMuted, ov::align::left | ov::align::middle, 1 * k), es ? "R1" : "R1");
    char last[24];
    std::snprintf(last, sizeof last, "R%zu", n);
    ov::text(p, px + pw, y + h - 12 * k, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::middle, 1 * k), last);
    ov::rect(p, px + pw / 2 - 70 * k, y + h - 13 * k, 14 * k, 2 * k, ui::kRed);
    ov::text(p, px + pw / 2 - 52 * k, y + h - 12 * k, ui::ts(11 * k, ui::kText2, ov::align::left | ov::align::middle), es ? "esta" : "this year");
    ov::rect(p, px + pw / 2 + 20 * k, y + h - 13 * k, 14 * k, 2 * k, 0xFFFFFF40);
    ov::text(p, px + pw / 2 + 38 * k, y + h - 12 * k, ui::ts(11 * k, ui::kText2, ov::align::left | ov::align::middle), es ? "anterior" : "last year");
}

// the four ratings as a radar: pace up, racecraft right, awareness down, experience left; the peak he can reach
// as a faint outline behind
void ratings_radar(ov::Paint *p, float cx, float cy, float r, float k, const UiDriverCard &c, float grow, bool es)
{
    if (r < 20 * k)
        return;
    // four axes from the market's model; five when the game's ratings were read (focus, which only the game has)
    const char *names_en[5] = {"PACE", "RACECRAFT", "AWARENESS", "EXP.", "FOC."},
               *names_es[5] = {"RITMO", "CARRERA", "ATENCIÓN", "EXP.", "CONC."};
    const int n = c.rating[4] > 0 ? 5 : 4;
    if (n == 5)
        cx += 10 * k; // the upper-left label reaches further left than the four-axis one: clear the character bars
    float ax[5], ay[5];
    for (int i = 0; i < n; ++i) {
        float a = -1.5707963f + 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        ax[i] = std::cos(a);
        ay[i] = std::sin(a);
    }
    auto f = [](float v) { return std::clamp((v - 40.0f) / 60.0f, 0.04f, 1.0f); }; // 40 at the centre, 100 at the rim
    // the web: three rings and the spokes
    for (int g = 1; g <= 3; ++g) {
        float gr = r * g / 3.0f, pts[12];
        for (int i = 0; i < n; ++i) {
            pts[i * 2] = cx + ax[i] * gr;
            pts[i * 2 + 1] = cy + ay[i] * gr;
        }
        pts[n * 2] = pts[0];
        pts[n * 2 + 1] = pts[1];
        ov::polyline(p, pts, n + 1, 1 * k, g == 3 ? 0xFFFFFF26 : 0xFFFFFF12);
    }
    for (int i = 0; i < n; ++i)
        ov::line(p, cx, cy, cx + ax[i] * r, cy + ay[i] * r, 1 * k, 0xFFFFFF12);
    uint32_t col = c.col ? ui::rgb(c.col) : ui::kRed;
    auto shape = [&](const float *v, float scale, float *pts) {
        for (int i = 0; i < n; ++i) {
            pts[i * 2] = cx + ax[i] * r * f(v[i]) * scale;
            pts[i * 2 + 1] = cy + ay[i] * r * f(v[i]) * scale;
        }
        pts[n * 2] = pts[0];
        pts[n * 2 + 1] = pts[1];
    };
    float peak[12], now[12];
    shape(c.peak, 1.0f, peak);
    ov::polyline(p, peak, n + 1, 1.2f * k, 0xFFFFFF40);
    shape(c.rating, std::max(0.05f, grow), now);
    ov::polygon(p, now, n, ui::alpha(col, 0.40f));
    ov::polyline(p, now, n + 1, 2 * k, ui::shade(col, 1.3f));
    for (int i = 0; i < n; ++i)
        ov::circle(p, now[i * 2], now[i * 2 + 1], 2.8f * k, 0xFFFFFFE0);
    // the labels, with the numbers
    for (int i = 0; i < n; ++i) {
        char v[8];
        std::snprintf(v, sizeof v, "%.0f", c.rating[i]);
        float lx = cx + ax[i] * (r + 10 * k), ly = cy + ay[i] * (r + 10 * k);
        uint32_t align = (ax[i] > 0.3f ? ov::align::left : ax[i] < -0.3f ? ov::align::right : ov::align::center) |
                         (ay[i] < -0.3f ? ov::align::bottom : ay[i] > 0.3f ? ov::align::top : ov::align::middle);
        std::string label = std::string(es ? names_es[i] : names_en[i]) + " " + v;
        ov::text(p, lx, ly, ui::ts(10.5f * k, ui::kText2, align, 0.6f * k), label);
    }
}

// the frame of a card: dims the hub, slides the card up; returns the card's rect
ov::Rect card_frame(ov::Paint *p, float k, float W, float H, uint32_t col, float t)
{
    float a = ui::g_lite ? 1.0f : ui::ease_out(t / 0.22f);
    ov::rect(p, 0, 0, W, H, ui::alpha(0x000000C0, a));
    float w = std::min(W - 80 * k, 1240 * k), h = std::min(H - 80 * k, 720 * k);
    float x = std::round((W - w) / 2), y = std::round((H - h) / 2 + (1 - a) * 30 * k);
    ui::shadow(p, x, y, w, h, k, 2.0f, 0x000000FF, 14);
    ov::gradient(p, x, y, w, h, 0x171821FF, 0x0D0E13FF, false, 16 * k);
    ov::rect_rounded_outline(p, x, y, w, h, 16 * k, 1 * k, 0xFFFFFF22);
    {
        ov::ClipScope clip(p, x, y, w, 130 * k);
        ov::gradient(p, x, y, w, 130 * k, ui::rgb(col, 0x88), ui::rgb(col, 0x10), true, 16 * k);
        ui::livery(p, x, y, w, 130 * k, col, k, 1, 16 * k);
        ov::gradient(p, x, y + 60 * k, w, 70 * k, 0x17182100, 0x171821E0, false);
    }
    return {x, y, w, h};
}

// returns false when the card asks to close
void draw_driver_card(ov::Paint *p, const ov::Input &in, float k, float W, float H, const UiDriverCard &c, bool es, float t)
{
    auto T = [&](const char *a, const char *b) { return es ? a : b; };
    ov::Rect r = card_frame(p, k, W, H, c.col ? c.col : 0x3A3D4A, t);
    float x = r.x, y = r.y, w = r.w, pad = 34 * k;
    float grow = ui::g_lite ? 1.0f : ui::ease_out((t - 0.1f) / 0.6f);
    // the head: name, where he's from, his team
    ov::text(p, x + pad, y + 30 * k, ui::lifted(ui::ts(40 * k, 0xFFFFFFFF, ov::align::left | ov::align::top, 0.6f * k), k), c.name);
    std::string sub;
    if (!c.nat.empty()) {
        std::string nat = c.nat;
        for (char &ch : nat)
            if (ch == '_')
                ch = ' ';
        if (!nat.empty())
            nat[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(nat[0])));
        sub = nat + "  ·  ";
    }
    sub += std::to_string(c.age) + T(" años", " y/o");
    if (c.number > 0)
        sub += "  ·  #" + std::to_string(c.number);
    if (c.seasons > 0)
        sub += "  ·  " + std::to_string(c.seasons) + T(" temporadas en F1", " F1 seasons");
    ov::text(p, x + pad, y + 82 * k, ui::ts(16 * k, 0xFFFFFFD0), sub);
    // the team chip opens the team's card
    float rx = x + w - pad;
    if (!c.code.empty()) {
        float cw = ui::team_chip(p, rx - 120 * k, y + 34 * k, c.code, c.col, k, 30);
        (void)cw;
        if (link_text(p, in, rx - 120 * k, y + 80 * k, ui::ts(15 * k, 0xFFFFFFD0, ov::align::left | ov::align::top), c.team, 120 * k) && !c.team_id.empty())
            ui4::open_card(ui4::K_TEAM, c.team_id);
    } else {
        ov::text(p, rx, y + 40 * k, ui::ts(16 * k, 0xFFFFFFC0, ov::align::right | ov::align::top),
                 c.kind == "f2" ? "F2" : c.kind == "legend" ? T("Leyenda", "Legend") : T("Sin equipo", "No seat"));
    }
    // the game's rating (as its ratings screen shows it) when it was read, then the market's value and potential
    char v[16], line[64];
    if (c.game_rating > 0) {
        std::snprintf(v, sizeof v, "%d", c.game_rating);
        ov::text(p, rx - 160 * k, y + 22 * k, ui::lifted(ui::ts(52 * k, 0xFFFFFFFF, ov::align::right | ov::align::top), k), v);
        std::snprintf(line, sizeof line, es ? "VALORACIÓN  ·  VALOR %.0f  ·  POT. %.0f" : "RATING  ·  MARKET VALUE %.0f  ·  POT. %.0f",
                      c.value, c.potential);
        ov::text(p, rx - 160 * k, y + 86 * k, ui::ts(14 * k, 0xFFFFFFB0, ov::align::right | ov::align::top, 1 * k), line);
    } else {
        std::snprintf(v, sizeof v, "%.0f", c.value);
        ov::text(p, rx - 160 * k, y + 22 * k, ui::lifted(ui::ts(52 * k, 0xFFFFFFFF, ov::align::right | ov::align::top), k), v);
        std::snprintf(line, sizeof line, es ? "VALOR  ·  POT. %.0f" : "MARKET VALUE  ·  POT. %.0f", c.potential);
        ov::text(p, rx - 160 * k, y + 86 * k, ui::ts(14 * k, 0xFFFFFFB0, ov::align::right | ov::align::top, 1 * k), line);
    }

    float top = y + 150 * k, lx = x + pad, lw = w * 0.42f, rx2 = x + pad + lw + 40 * k, rw = x + w - pad - rx2;
    // left: the contract, the character, the season, the path
    float ly = top;
    ly += card_head(p, lx, ly, lw, k, T("CONTRATO", "CONTRACT"));
    {
        std::string line = c.contract > 0 ? std::string(T("Hasta ", "Until ")) + std::to_string(c.contract) : T("Sin contrato", "No contract");
        if (!c.salary.empty() && c.contract > 0)
            line += "  ·  " + c.salary + T(" / año", " a year");
        ov::text(p, lx, ly, ui::ts(18 * k, ui::kText), line);
        float tx = lx;
        float ty = ly + 30 * k;
        if (c.clause > 0) {
            char b[48];
            std::snprintf(b, sizeof b, "%s %d", T("CLÁUSULA: TOP", "EXIT CLAUSE: TOP"), c.clause);
            tx += ui::tag(p, tx, ty, b, 0xFF375FFF, k, 11) + 6 * k;
        }
        if (c.wants_out)
            tx += ui::tag(p, tx, ty, T("QUIERE SALIR", "WANTS OUT"), ui::kDanger, k, 11) + 6 * k;
        if (c.pressure)
            tx += ui::tag(p, tx, ty, T("BAJO PRESIÓN", "UNDER PRESSURE"), ui::kDanger, k, 11) + 6 * k;
        if (c.feud)
            tx += ui::tag(p, tx, ty, T("RIVALIDAD", "FEUD"), 0xBF5AF2FF, k, 11) + 6 * k;
        if (c.backing >= 3) {
            char b[48];
            std::snprintf(b, sizeof b, "%s %.0f M", T("PATROCINIO", "SPONSORS"), c.backing);
            tx += ui::tag(p, tx, ty, b, ui::kGreen, k, 11) + 6 * k;
        }
        // 1.1.0 phase 6: his role off the grid
        if (!c.role.empty() && tx < lx + lw - 120 * k) {
            std::string role = (c.role == "reserve" ? std::string(T("RESERVA · ", "RESERVE · ")) : std::string(T("ACADEMIA · ", "ACADEMY · "))) +
                               ui4::upper(c.role_team);
            tx += ui::tag(p, tx, ty, role, c.role == "reserve" ? 0x40C8E0FF : 0x32D74BFF, k, 11) + 6 * k;
        }
        ly += (tx > lx ? 64 : 40) * k;
    }
    ly += card_head(p, lx, ly, lw, k, T("CARÁCTER Y VALORACIONES", "CHARACTER & RATINGS"));
    {
        // the character on the left, the ratings radar on the right
        float bw = lw * 0.58f, block = 128 * k;
        if (c.ambition >= 0) {
            card_bar(p, lx, ly + 6 * k, bw, k, T("Ambición", "Ambition"), c.ambition * grow, ui::kAmber, std::to_string(static_cast<int>(std::lround(c.ambition * 100))));
            card_bar(p, lx, ly + 40 * k, bw, k, T("Lealtad", "Loyalty"), c.loyalty * grow, ui::kBlue, std::to_string(static_cast<int>(std::lround(c.loyalty * 100))));
            card_bar(p, lx, ly + 74 * k, bw, k, T("Dinero", "Money"), c.money * grow, ui::kGreen, std::to_string(static_cast<int>(std::lround(c.money * 100))));
        } else {
            ov::text(p, lx, ly + 6 * k, ui::ts(15 * k, ui::kMuted, ov::align::left | ov::align::top, 0, bw),
                     T("Las personalidades están apagadas.", "Driver personalities are off."));
        }
        ratings_radar(p, lx + bw + (lw - bw) / 2 - 10 * k, ly + block / 2 - 2 * k, std::min(46 * k, (lw - bw) / 2 - 40 * k), k, c, grow, es);
        ly += block;
    }
    ly += card_head(p, lx, ly, lw, k, T("ESTA TEMPORADA", "THIS SEASON"));
    {
        const char *labels_es[4] = {"PUNTOS", "VICTORIAS", "PODIOS", "CARRERAS"}, *labels_en[4] = {"POINTS", "WINS", "PODIUMS", "STARTS"};
        int vals[4] = {c.points, c.wins, c.podiums, c.starts};
        float tw = (lw - 30 * k) / 4;
        for (int i = 0; i < 4; ++i) {
            float tx = lx + i * (tw + 10 * k);
            ov::rect_rounded(p, tx, ly, tw, 66 * k, 9 * k, 0xFFFFFF08);
            ov::text(p, tx + tw / 2, ly + 26 * k, ui::ts(26 * k, ui::kText, ov::align::center | ov::align::middle),
                     std::to_string(static_cast<int>(std::lround(vals[i] * grow))));
            ov::text(p, tx + tw / 2, ly + 52 * k, ui::ts(10.5f * k, ui::kMuted, ov::align::center | ov::align::middle, 1.2f * k), es ? labels_es[i] : labels_en[i]);
        }
        ly += 80 * k;
        // 1.1.0 phase 5: his whole career in this market
        if (c.c_starts > 0 || c.c_titles > 0) {
            auto n = [&](int v, const char *one_es, const char *many_es, const char *one_en, const char *many_en) {
                return std::to_string(v) + " " + (v == 1 ? T(one_es, one_en) : T(many_es, many_en));
            };
            std::string line = std::string(T("CARRERA:  ", "CAREER:  ")) + n(c.c_starts, "salida", "salidas", "start", "starts") + "  ·  " +
                               n(c.c_wins, "victoria", "victorias", "win", "wins") + "  ·  " +
                               n(c.c_podiums, "podio", "podios", "podium", "podiums") + "  ·  " +
                               n(c.c_titles, "título", "títulos", "title", "titles") + "  ·  " +
                               n(c.c_teams, "equipo", "equipos", "team", "teams");
            ov::text(p, lx, ly + 4 * k, ui::ts(13.5f * k, ui::kText2, ov::align::left | ov::align::middle, 0.3f * k), ui::fit(p, ui::ts(13.5f * k), line, lw));
            ly += 24 * k;
        }
        ly += 4 * k;
    }
    ly += card_head(p, lx, ly, lw, k, T("TRAYECTORIA", "CAREER PATH"));
    if (c.path.empty()) {
        ov::text(p, lx, ly, ui::ts(15 * k, ui::kMuted), c.team.empty() ? T("Todavía sin equipo en F1.", "No F1 team yet.") : (c.team + T(", desde antes de Silly Season.", ", since before Silly Season.")));
    } else {
        float px = lx;
        for (size_t i = 0; i < c.path.size(); ++i) {
            const auto &s = c.path[i];
            if (px > lx + lw - 80 * k)
                break;
            float cw = ui::team_chip(p, px, ly, s.code, s.col, k, 24);
            ov::text(p, px + cw / 2, ly + 36 * k, ui::ts(11.5f * k, ui::kText2, ov::align::center | ov::align::middle), std::to_string(s.year));
            px += cw + 8 * k;
            if (i + 1 < c.path.size()) {
                ui::arrow_right(p, px, ly + 12 * k, 16 * k, k, 0xFFFFFF50);
                px += 24 * k;
            }
        }
    }
    // right: the value chart, the stories, the honours
    float ry = top;
    bool duel = !c.h2h_mate.empty() && c.h2h_ahead + c.h2h_behind > 0;
    ry += card_head(p, rx2, ry, rw, k, T("VALOR, CARRERA A CARRERA", "VALUE, RACE BY RACE"));
    value_chart(p, rx2, ry, rw, duel ? 196 * k : 230 * k, k, c, es, grow);
    ry += duel ? 212 * k : 248 * k;
    // 1.1.0 phase 5: this season against his team-mate, race by race
    if (duel) {
        ry += card_head(p, rx2, ry, rw, k, T("DUELO CON SU COMPAÑERO", "TEAM-MATE DUEL"));
        int n = c.h2h_ahead + c.h2h_behind;
        float f = static_cast<float>(c.h2h_ahead) / static_cast<float>(n);
        ov::text(p, rx2, ry + 10 * k, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle), std::to_string(c.h2h_ahead));
        ov::text(p, rx2 + rw, ry + 10 * k, ui::ts(15 * k, ui::kText, ov::align::right | ov::align::middle), std::to_string(c.h2h_behind));
        float bx = rx2 + 34 * k, bw = rw - 68 * k;
        ov::rect_rounded(p, bx, ry + 6 * k, bw, 8 * k, 4 * k, 0xFFFFFF26);
        ov::rect_rounded(p, bx, ry + 6 * k, std::max(8 * k, bw * f * grow), 8 * k, 4 * k, c.col ? ui::rgb(c.col) : ui::kRed);
        ov::text(p, rx2 + rw, ry + 32 * k, ui::ts(12.5f * k, ui::kMuted, ov::align::right | ov::align::middle),
                 ui::fit(p, ui::ts(12.5f * k), std::string(T("contra ", "vs ")) + c.h2h_mate + T(" (carreras por delante)", " (races ahead)"), rw));
        ry += 50 * k;
    }
    size_t max_links = duel ? 3 : 4;
    ry += card_head(p, rx2, ry, rw, k, T("EN LAS NOTICIAS", "IN THE NEWS"));
    if (c.links.empty())
        ov::text(p, rx2, ry, ui::ts(15 * k, ui::kMuted), T("Nadie habla de él ahora mismo.", "Nobody is talking about him right now."));
    for (size_t i = 0; i < c.links.size() && i < max_links; ++i)
        ov::text(p, rx2, ry + i * 26 * k, ui::ts(15 * k, ui::kText2), ui::fit(p, ui::ts(15 * k), "•  " + c.links[i], rw));
    ry += std::max<size_t>(1, std::min<size_t>(max_links, c.links.size())) * 26 * k + 12 * k;
    ry += card_head(p, rx2, ry, rw, k, T("PALMARÉS", "HONOURS"));
    if (c.awards.empty())
        ov::text(p, rx2, ry, ui::ts(15 * k, ui::kMuted), T("Todavía ninguno.", "None yet."));
    float ax = rx2;
    for (const std::string &a : c.awards) {
        if (ax > rx2 + rw - 140 * k)
            break;
        ax += ui::tag(p, ax, ry - 2 * k, a, ui::kGold, k, 11, true) + 8 * k;
    }
    ov::text(p, x + w - pad, r.y + r.h - 22 * k, ui::ts(12.5f * k, ui::kMuted, ov::align::right | ov::align::middle, 0.6f * k),
             T("Esc / B  cerrar", "Esc / B  close"));
}

void draw_team_card(ov::Paint *p, const ov::Input &in, float k, float W, float H, const UiTeamCard &c, bool es, float t)
{
    auto T = [&](const char *a, const char *b) { return es ? a : b; };
    ov::Rect r = card_frame(p, k, W, H, c.col, t);
    float x = r.x, y = r.y, w = r.w, pad = 34 * k;
    float grow = ui::g_lite ? 1.0f : ui::ease_out((t - 0.1f) / 0.6f);
    ov::text(p, x + pad, y + 30 * k, ui::lifted(ui::ts(40 * k, 0xFFFFFFFF, ov::align::left | ov::align::top, 0.6f * k), k), c.name);
    float tx = x + pad;
    if (!c.strategy.empty()) {
        ui::Tag st = ui::strategy_tag(c.strategy);
        tx += ui::tag(p, tx, y + 86 * k, T(st.es, st.en), st.col, k, 12, true) + 10 * k;
    }
    char rank[48];
    std::snprintf(rank, sizeof rank, "P%d  ·  %d %s", c.rank, c.points, T("puntos", "points"));
    ov::text(p, tx, y + 97 * k, ui::ts(15 * k, 0xFFFFFFD0, ov::align::left | ov::align::middle), rank);
    ov::text(p, x + w - pad, y + 28 * k, ui::ts(64 * k, 0xFFFFFF30, ov::align::right | ov::align::top, 2 * k), c.code);

    float top = y + 150 * k, lx = x + pad, lw = w * 0.44f, rx = x + pad + lw + 40 * k, rw = x + w - pad - rx;
    float ly = top;
    ly += card_head(p, lx, ly, lw, k, T("EL EQUIPO", "THE TEAM"));
    auto num = [](float f) { return std::to_string(static_cast<int>(std::lround(f))); };
    card_bar(p, lx, ly, lw, k, T("Coche", "Car"), (c.performance - 45) / 55 * grow, ui::kRed, num(c.performance));
    card_bar(p, lx, ly + 28 * k, lw, k, T("Prestigio", "Prestige"), (c.prestige - 45) / 55 * grow, ui::kGold, num(c.prestige));
    {
        // the budget, and how it moved this season (sponsors in and out)
        std::string money = num(c.budget);
        if (std::fabs(c.budget_delta) >= 0.5f) {
            char delta[16];
            std::snprintf(delta, sizeof delta, " (%+.0f)", c.budget_delta);
            money += delta;
        }
        card_bar(p, lx, ly + 56 * k, lw, k, T("Presupuesto", "Budget"), (c.budget - 50) / 110 * grow, ui::kGreen, money);
    }
    card_bar(p, lx, ly + 84 * k, lw, k, T("Dir. técnico", "Tech. director"), (c.td_rating - 50) / 50 * grow, 0x5E5CE6FF, num(c.td_rating));
    // 1.1.0 phase 5: this season's money, the technical director and the team principal
    ov::text(p, lx, ly + 116 * k, ui::ts(13.5f * k, ui::kMuted), ui::fit(p, ui::ts(13.5f * k), std::string(T("Dir. técnico: ", "Tech. director: ")) + c.td, lw));
    if (!c.tp.empty()) {
        std::string tp = std::string(T("Jefe de equipo: ", "Team principal: ")) + c.tp;
        if (c.tp_since > 0)
            tp += std::string(T(" (desde ", " (since ")) + std::to_string(c.tp_since) + ")";
        ov::text(p, lx, ly + 138 * k, ui::ts(13.5f * k, ui::kMuted), ui::fit(p, ui::ts(13.5f * k), tp, lw));
    }
    ly += 168 * k;
    auto seats = [&](const char *label, const std::string *ids, const std::string *names, const bool *fresh) {
        ly += card_head(p, lx, ly, lw, k, label);
        for (int s = 0; s < 2; ++s) {
            ov::rect_rounded(p, lx, ly, 26 * k, 28 * k, 6 * k, 0xFFFFFF0C);
            ov::text(p, lx + 13 * k, ly + 14 * k, ui::ts(13 * k, ui::kText2, ov::align::center | ov::align::middle), std::to_string(s + 1));
            if (names[s].empty())
                ov::text(p, lx + 40 * k, ly + 14 * k, ui::ts(16 * k, ui::kMuted, ov::align::left | ov::align::middle), T("por decidir", "to be decided"));
            else if (link_text(p, in, lx + 40 * k, ly + 14 * k, ui::ts(17 * k, ui::kText, ov::align::left | ov::align::middle), names[s], lw - 120 * k))
                ui4::open_card(ui4::K_DRIVER, ids[s]);
            if (fresh && fresh[s] && !names[s].empty())
                ui::tag(p, lx + lw - 60 * k, ly + 4 * k, T("NUEVO", "NEW"), ui::kGreen, k, 10, true);
            ly += 36 * k;
        }
        ly += 8 * k;
    };
    seats(T("ESTA TEMPORADA", "THIS SEASON"), c.now_id, c.now, nullptr);
    seats(T("LA PRÓXIMA", "NEXT SEASON"), c.next_id, c.next, c.next_new);

    float ry = top;
    ry += card_head(p, rx, ry, rw, k, T("CANTERA Y RESERVAS", "ACADEMY & RESERVES"));
    // 1.1.0 phase 6: the reserve driver first
    if (!c.reserve.empty()) {
        std::string line = std::string(T("Reserva: ", "Reserve: ")) + c.reserve;
        if (!c.reserve_shared.empty())
            line += std::string(T(" (compartido con ", " (shared with ")) + c.reserve_shared + ")";
        else if (c.reserve_until > 0)
            line += std::string(T(" (hasta ", " (until ")) + std::to_string(c.reserve_until) + ")";
        if (link_text(p, in, rx, ry, ui::ts(15 * k, 0x40C8E0FF), line, rw))
            ui4::open_card(ui4::K_DRIVER, c.reserve_id);
        ry += 28 * k;
    }
    if (c.juniors.empty() && c.reserve.empty())
        ov::text(p, rx, ry, ui::ts(15 * k, ui::kMuted), T("Nadie en la recámara.", "Nobody waiting in the wings."));
    for (size_t i = 0; i < c.juniors.size() && i < 4; ++i) {
        const UiDriverCard *jc = g_ov.card(c.juniors[i].first);
        std::string name = jc ? jc->name : c.juniors[i].first;
        if (link_text(p, in, rx, ry + i * 26 * k, ui::ts(15 * k, ui::kText2), name + "  ·  " + c.juniors[i].second, rw))
            ui4::open_card(ui4::K_DRIVER, c.juniors[i].first);
    }
    ry += (c.juniors.empty() && !c.reserve.empty() ? 0 : std::max<size_t>(1, std::min<size_t>(4, c.juniors.size())) * 26 * k) + 12 * k;
    ry += card_head(p, rx, ry, rw, k, T("EN EL MERCADO", "ON THE MARKET"));
    if (c.links.empty())
        ov::text(p, rx, ry, ui::ts(15 * k, ui::kMuted), T("Nada se mueve ahora mismo.", "Nothing moving right now."));
    for (size_t i = 0; i < c.links.size() && i < 4; ++i)
        ov::text(p, rx, ry + i * 26 * k, ui::ts(15 * k, ui::kText2), ui::fit(p, ui::ts(15 * k), "•  " + c.links[i], rw));
    ry += std::max<size_t>(1, std::min<size_t>(4, c.links.size())) * 26 * k + 12 * k;
    ry += card_head(p, rx, ry, rw, k, T("ÚLTIMOS MOVIMIENTOS", "RECENT MOVES"));
    if (c.moves.empty())
        ov::text(p, rx, ry, ui::ts(15 * k, ui::kMuted), T("Ninguno todavía.", "None yet."));
    for (size_t i = 0; i < c.moves.size() && i < 5; ++i)
        ov::text(p, rx, ry + i * 24 * k, ui::ts(14 * k, ui::kText2), ui::fit(p, ui::ts(14 * k), c.moves[i], rw));
    ry += std::max<size_t>(1, std::min<size_t>(5, c.moves.size())) * 24 * k + 12 * k;
    ry += card_head(p, rx, ry, rw, k, T("PALMARÉS", "HONOURS"));
    if (c.awards.empty())
        ov::text(p, rx, ry, ui::ts(15 * k, ui::kMuted), T("Todavía ninguno.", "None yet."));
    float ax = rx;
    for (const std::string &a : c.awards) {
        if (ax > rx + rw - 140 * k)
            break;
        ax += ui::tag(p, ax, ry - 2 * k, a, ui::kGold, k, 11, true) + 8 * k;
    }
    ov::text(p, x + w - pad, r.y + r.h - 22 * k, ui::ts(12.5f * k, ui::kMuted, ov::align::right | ov::align::middle, 0.6f * k),
             T("Esc / B  cerrar", "Esc / B  close"));
}

// the global search: any driver or team by name, from any page; Enter / A or a click opens the card
void draw_search(ov::Paint *p, const ov::Input &in, float k, float W, float H, bool es, float t)
{
    using namespace ui4;
    auto T = [&](const char *a, const char *b) { return es ? a : b; };
    if (t > 0.05f) // not the key that opened it
        type_into(in, gquery);
    struct Hit {
        int kind;
        std::string id, name, sub, code;
        uint32_t col;
        float score;
    };
    std::vector<Hit> hits;
    std::string q = lower(gquery);
    auto has = [&](const std::string &s) { return !q.empty() && lower(s).find(q) != std::string::npos; };
    for (const UiTeamCard &c : g_ov.team_cards)
        if (has(c.name) || (!q.empty() && lower(c.code) == q))
            hits.push_back({K_TEAM, c.id, c.name, T("Equipo", "Team"), c.code, c.col, 1000.0f + (lower(c.name).rfind(q, 0) == 0 ? 10.0f : 0.0f)});
    for (const UiDriverCard &c : g_ov.cards) {
        if (!q.empty() && !has(c.name) && !has(c.team))
            continue;
        std::string sub = !c.team.empty() ? c.team
                          : c.role == "reserve" ? std::string(T("Reserva de ", "Reserve, ")) + c.role_team
                          : c.kind == "f2"       ? std::string("F2")
                                                 : std::string(T("Agente libre", "Free agent"));
        // a name that starts with the search, or a surname that does, comes first
        std::string ln = lower(c.name);
        size_t sp = ln.find_last_of(' ');
        bool starts = !q.empty() && (ln.rfind(q, 0) == 0 || (sp != std::string::npos && ln.compare(sp + 1, q.size(), q) == 0));
        hits.push_back({K_DRIVER, c.id, c.name, sub, c.code, c.col, c.value + (starts ? 60.0f : 0.0f) + (has(c.name) ? 20.0f : 0.0f)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    if (hits.size() > 10)
        hits.resize(10);
    int n = static_cast<int>(hits.size());
    gsel = n ? std::clamp(gsel, 0, n - 1) : 0;
    if (in.key_typed(VK_DOWN) || in.pad_hit(ov::pad::down))
        gsel = n ? (gsel + 1) % n : 0;
    if (in.key_typed(VK_UP) || in.pad_hit(ov::pad::up))
        gsel = n ? (gsel + n - 1) % n : 0;

    float a = ui::g_lite ? 1.0f : ui::ease_out(t / 0.2f);
    ov::rect(p, 0, 0, W, H, ui::alpha(0x000000B8, a));
    float w = std::min(W - 160 * k, 820 * k), rh = 52 * k, ih = 64 * k;
    float h = ih + 20 * k + std::max(1, n) * rh + 50 * k;
    float x = std::round((W - w) / 2), y = std::round(H * 0.16f + (1 - a) * 20 * k);
    ui::shadow(p, x, y, w, h, k, 2.0f, 0x000000FF, 14);
    ov::gradient(p, x, y, w, h, 0x171821FF, 0x0D0E13FF, false, 16 * k);
    ov::rect_rounded_outline(p, x, y, w, h, 16 * k, 1 * k, 0xFFFFFF22);
    ov::gradient(p, x + 16 * k, y, w * 0.5f, 2 * k, ui::kRed, 0xE1060000, true);
    // the field
    ov::circle_outline(p, x + 34 * k, y + ih / 2 - 3 * k, 9 * k, 2 * k, ui::kText2);
    ov::line(p, x + 41 * k, y + ih / 2 + 4 * k, x + 47 * k, y + ih / 2 + 10 * k, 2.4f * k, ui::kText2);
    ov::TextStyle qs = ui::ts(24 * k, gquery.empty() ? ui::kMuted : ui::kText, ov::align::left | ov::align::middle);
    ov::text(p, x + 64 * k, y + ih / 2, qs, gquery.empty() ? std::string(T("Busca un piloto o un equipo", "Search a driver or a team")) : gquery);
    if (std::fmod(ui::clock_s(), 1.0f) < 0.55f)
        ov::rect(p, x + 66 * k + (gquery.empty() ? 0.0f : ui::text_w(p, qs, gquery)), y + ih / 2 - 14 * k, 2 * k, 28 * k, ui::kRed);
    ov::rect(p, x, y + ih, w, 1 * k, 0xFFFFFF14);
    // the results
    float ry = y + ih + 10 * k;
    if (hits.empty())
        ov::text(p, x + w / 2, ry + rh / 2, ui::ts(16 * k, ui::kMuted, ov::align::center | ov::align::middle), T("Nadie con ese nombre.", "Nobody by that name."));
    for (int i = 0; i < n; ++i) {
        const Hit &hit = hits[static_cast<size_t>(i)];
        float yy = ry + i * rh;
        bool hov = in.inside(x + 8 * k, yy, w - 16 * k, rh - 4 * k);
        if (hov && (in.x != 0 || in.y != 0))
            gsel = hov ? i : gsel;
        bool on = i == gsel;
        if (on) {
            ov::rect_rounded(p, x + 8 * k, yy, w - 16 * k, rh - 4 * k, 9 * k, 0xE106002A);
            ov::rect_rounded(p, x + 8 * k, yy + 10 * k, 3 * k, rh - 24 * k, 1.5f * k, ui::kRed);
        }
        if (hit.code.empty())
            ui::tag(p, x + 22 * k, yy + rh / 2 - 13 * k, hit.kind == K_TEAM ? T("EQUIPO", "TEAM") : T("LIBRE", "FREE"), ui::kMuted, k, 10);
        else
            ui::team_chip(p, x + 22 * k, yy + rh / 2 - 13 * k, hit.code, hit.col, k, 22);
        ov::text(p, x + 100 * k, yy + rh / 2 - 2 * k, ui::ts(18 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(p, ui::ts(18 * k), hit.name, w * 0.5f));
        ov::text(p, x + w - 24 * k, yy + rh / 2 - 2 * k, ui::ts(14 * k, ui::kText2, ov::align::right | ov::align::middle), ui::fit(p, ui::ts(14 * k), hit.sub, w * 0.35f));
        if ((hov && in.clicked()) || (on && (in.key_pressed(VK_RETURN) || in.pad_hit(ov::pad::a)))) {
            open_card(hit.kind, hit.id); // the search waits underneath: Esc goes back to it
            return;
        }
    }
    ov::text(p, x + w - 24 * k, y + h - 22 * k, ui::ts(12.5f * k, ui::kMuted, ov::align::right | ov::align::middle, 0.6f * k),
             T("Arriba/Abajo elegir  ·  Enter / A abrir  ·  Esc / B cerrar", "Up/Down pick  ·  Enter / A open  ·  Esc / B close"));
    if (in.clicked() && !in.inside(x, y, w, h) && t > 0.2f)
        close_card();
}

// the open card, over the hub; a click outside it closes it
void draw_card(ov::Paint *p, const ov::Input &in, float k, float W, float H, bool es)
{
    using namespace ui4;
    if (!modal)
        return;
    float t = std::chrono::duration<float>(Clock::now() - modal_at).count();
    if (modal == K_SEARCH) {
        draw_search(p, in, k, W, H, es, t);
        return;
    }
    int before = modal;
    std::string before_id = modal_id;
    if (modal == K_DRIVER) {
        if (const UiDriverCard *c = g_ov.card(modal_id))
            draw_driver_card(p, in, k, W, H, *c, es, t);
        else
            close_card();
    } else if (modal == K_TEAM) {
        if (const UiTeamCard *c = g_ov.team_card(modal_id))
            draw_team_card(p, in, k, W, H, *c, es, t);
        else
            close_card();
    }
    // a card opened from a list: its place in it, and arrows to step through
    if (int bi = browse_index(); bi >= 0 && modal == before) {
        float w = std::min(W - 80 * k, 1240 * k), h = std::min(H - 80 * k, 720 * k);
        float cx = (W - w) / 2 + 34 * k, cy = (H - h) / 2 + h - 24 * k;
        char pos[32];
        std::snprintf(pos, sizeof pos, "%d / %zu", bi + 1, browse.size());
        bool hl = in.inside(cx - 8 * k, cy - 14 * k, 28 * k, 28 * k), hr = in.inside(cx + 92 * k, cy - 14 * k, 28 * k, 28 * k);
        {
            uint32_t lc = hl ? ui::kText : ui::kText2; // a left arrow: the mirror of arrow_right
            ov::rect(p, cx + 6 * k, cy - 1 * k, 12 * k, 2 * k, lc);
            float pts[6] = {cx + 8 * k, cy - 5 * k, cx, cy, cx + 8 * k, cy + 5 * k};
            ov::polygon(p, pts, 3, lc);
        }
        ov::text(p, cx + 56 * k, cy, ui::ts(13.5f * k, ui::kText2, ov::align::center | ov::align::middle, 0.8f * k), pos);
        ui::arrow_right(p, cx + 92 * k, cy, 18 * k, k, hr ? ui::kText : ui::kText2);
        ov::text(p, cx + 128 * k, cy, ui::ts(12.5f * k, ui::kMuted, ov::align::left | ov::align::middle, 0.6f * k),
                 es ? "Izquierda / Derecha  ·  LB / RB" : "Left / Right  ·  LB / RB");
        if (in.clicked() && (hl || hr)) {
            browse_step(hl ? -1 : 1);
            return;
        }
    }
    // a click outside the card closes it (unless a link inside just opened another)
    if (modal == before && modal_id == before_id && in.clicked() && t > 0.2f) {
        float w = std::min(W - 80 * k, 1240 * k), h = std::min(H - 80 * k, 720 * k);
        if (!in.inside((W - w) / 2, (H - h) / 2, w, h))
            close_card();
    }
}

// ---------------------------------------------------------------------------------------------------
// The moments

// the moments (the grid reveal, the signing card, the Team HQ card) are in overlays.inl

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
    float stack = n.updates > 0 ? 30 * k : 0;
    float h = pad + 22 * k + 12 * k + th + 8 * k + bh + pad + (hero ? 10 * k : 0) + stack;
    if (!c.visible(y, h))
        return h;
    Reveal r(c);
    // the earlier updates of the story, as cards peeking out under this one
    for (int i = std::min(n.updates, 2); i >= 1; --i)
        ov::rect_rounded(c.p, x + 12 * k * i, y + h - 10 * k + 6 * k * i, w - 24 * k * i, 12 * k, 8 * k, ui::alpha(0x2A2D3AFF, 1.0f - 0.3f * i));
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
    if (n.updates > 0) {
        std::string line;
        for (const std::string &o : n.older)
            line += (line.empty() ? "" : "  ·  ") + o;
        char head[48];
        std::snprintf(head, sizeof head, "+%d %s", n.updates, n.updates == 1 ? c.T("ANTES", "EARLIER") : c.T("ANTES", "EARLIER"));
        float hy = y + h - pad - stack + 18 * k;
        float tw2 = ui::tag(c.p, px, hy - 10 * k, head, 0x8E9AAFFF, k, 10);
        ov::text(c.p, px + tw2 + 10 * k, hy, ui::ts(13 * k, ui::kMuted, ov::align::left | ov::align::middle),
                 ui::fit(c.p, ui::ts(13 * k), line, w - 2 * pad - tw2 - 20 * k));
    }
    return h;
}

// a news card's row: it opens the story's driver card, or its team's when the story has no driver
void news_pick(const Ctx &c, float x, float y, float w, float h, const UiNews &n)
{
    const std::string &id = !n.driver_id.empty() ? n.driver_id : n.team_id;
    if (id.empty())
        return;
    bool picked = pick_row(c, x, y, w, h, id, !n.driver_id.empty() ? ui4::K_DRIVER : ui4::K_TEAM);
    if (c.visible(y, h))
        pick_mark(c, x, y, w, h, picked);
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
        news_pick(c, cx, y, cw, h, n);
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
    for (size_t i = 0; i < d.news.size() && i < 3; ++i) {
        float h = news_card(c, c.area.x, ly, lw, d.news[i], true, i == 0);
        news_pick(c, c.area.x, ly, lw, h, d.news[i]);
        ly += h + 12 * k;
    }

    // the player's corner
    float ry = y + section_title(c, rx, y, c.T("TU RINCÓN", "YOUR CORNER"));
    // the team keenest on the player (Your Future)
    const UiSuitor *keenest = nullptr;
    for (const UiSuitor &s : d.future.suitors)
        if (s.status != "own" && (!keenest || s.interest > keenest->interest))
            keenest = &s;
    float extra = d.has_player && keenest ? 30 * k : 0;
    float ch = (d.has_player ? (d.press_pending ? 244 * k : 196 * k) : 130 * k) + extra;
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
            if (keenest) {
                float ky = sy + 78 * k;
                ov::rect(c.p, rx + 20 * k, ky - 12 * k, rw - 40 * k, 1 * k, 0xFFFFFF10);
                ov::text(c.p, rx + 20 * k, ky + 4 * k, ui::ts(11.5f * k, ui::kMuted, ov::align::left | ov::align::middle, 1.6f * k),
                         c.T("MÁS INTERESADO", "KEENEST ON YOU"));
                float kx = rx + 170 * k;
                kx += ui::team_chip(c.p, kx, ky - 7 * k, keenest->code, keenest->col, k, 20) + 10 * k;
                ov::text(c.p, kx, ky + 4 * k, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle),
                         ui::fit(c.p, ui::ts(15 * k), keenest->name, rx + rw - 80 * k - kx));
                ov::text(c.p, rx + rw - 20 * k, ky + 4 * k, ui::ts(15 * k, keenest->interest >= 66 ? ui::kGreen : ui::kAmber, ov::align::right | ov::align::middle),
                         std::to_string(keenest->interest) + "%");
            }
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
            if (c.hover(rx, my, rw, 44 * k)) {
                ov::rect_rounded(c.p, rx + 6 * k, my + 2 * k, rw - 12 * k, 40 * k, 8 * k, 0xFFFFFF0A);
                if (c.in.clicked())
                    ui4::open_card(ui4::K_DRIVER, u->id);
            }
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
    ry += mh + 24 * k;

    // the championship: the top three, and the player when he's further down
    if (!d.standings.empty()) {
        ry += section_title(c, rx, ry, c.T("CAMPEONATO", "CHAMPIONSHIP"));
        std::vector<size_t> rows_shown;
        for (size_t i = 0; i < d.standings.size() && i < 3; ++i)
            rows_shown.push_back(i);
        for (size_t i = 3; i < d.standings.size(); ++i)
            if (d.standings[i].player)
                rows_shown.push_back(i);
        float sh = static_cast<float>(rows_shown.size()) * 40 * k + 16 * k;
        if (c.visible(ry, sh)) {
            Reveal r(c);
            ui::card(c.p, rx, ry, rw, sh, k);
            float sy = ry + 8 * k;
            int lead = std::max(1, d.standings.front().points);
            const uint32_t medals[3] = {ui::kGold, 0xC7CCD6FF, 0xCD8A4FFF};
            for (size_t i : rows_shown) {
                const UiStanding &s = d.standings[i];
                bool hov = c.hover(rx, sy, rw, 40 * k);
                if (hov) {
                    ov::rect_rounded(c.p, rx + 6 * k, sy, rw - 12 * k, 38 * k, 8 * k, 0xFFFFFF0A);
                    if (c.in.clicked())
                        ui4::open_card(ui4::K_DRIVER, s.id);
                }
                if (s.player)
                    ov::rect_rounded(c.p, rx + 6 * k, sy, rw - 12 * k, 38 * k, 8 * k, 0xE106001E);
                ov::text(c.p, rx + 26 * k, sy + 19 * k, ui::ts(15 * k, i < 3 ? medals[i] : ui::kText2, ov::align::center | ov::align::middle),
                         std::to_string(i + 1));
                if (!s.code.empty())
                    ui::team_chip(c.p, rx + 46 * k, sy + 8 * k, s.code, s.col, k, 22);
                ov::text(c.p, rx + 114 * k, sy + 19 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle),
                         ui::fit(c.p, ui::ts(16 * k), s.name, rw - 260 * k));
                ui::bar(c.p, rx + rw - 140 * k, sy + 16 * k, 70 * k, 5 * k, static_cast<float>(s.points) / lead * c.grow(), s.col ? ui::rgb(s.col) : ui::kRed);
                ov::text(c.p, rx + rw - 20 * k, sy + 19 * k, ui::ts(15 * k, ui::kText, ov::align::right | ov::align::middle), std::to_string(s.points));
                sy += 40 * k;
            }
        }
        ry += sh;
    }
    return std::max(ly, ry) - y0;
}

float page_press(const Ctx &c, float y0, const ov::Input &in)
{
    using namespace paint_state;
    const OverlayData &d = g_ov;
    float k = c.k;
    // the answer just sent: shown until the paddock's reaction is in the news
    float since = std::chrono::duration<float>(Clock::now() - answered_at).count();
    if ((!d.press_pending || d.press_answers.empty()) && !answered_a.empty() && since < 12.0f) {
        Reveal r(c);
        ov::TextStyle q = ui::ts(20 * k, ui::kText2, ov::align::left | ov::align::top, 0, c.area.w - 180 * k);
        ov::TextStyle a = ui::ts(26 * k, ui::kText, ov::align::left | ov::align::top, 0, c.area.w - 180 * k);
        q.line_height = a.line_height = 1.3f;
        float h = 150 * k + ui::text_h(c.p, q, answered_q) + ui::text_h(c.p, a, answered_a);
        ui::card(c.p, c.area.x, y0, c.area.w, h, k, false, ui::kGreen);
        {
            ov::ClipScope clip(c.p, c.area.x, y0, c.area.w, h);
            ov::gradient(c.p, c.area.x, y0, c.area.w * 0.6f, h, 0x30D15826, 0x30D15800, true, 12 * k);
        }
        float pop = ui::ease_back(since / 0.5f);
        ov::circle(c.p, c.area.x + 70 * k, y0 + 70 * k, 30 * k * pop, ui::kGreen);
        float pts[6] = {c.area.x + 56 * k, y0 + 70 * k, c.area.x + 66 * k, y0 + 81 * k, c.area.x + 86 * k, y0 + 58 * k};
        ov::polyline(c.p, pts, 3, 4 * k * pop, 0x0B2A14FF);
        ui::tag(c.p, c.area.x + 130 * k, y0 + 30 * k, c.T("RESPUESTA ENVIADA", "ANSWER SENT"), ui::kGreen, k, 12, true);
        ov::text(c.p, c.area.x + 130 * k, y0 + 66 * k, q, answered_q);
        float ay = y0 + 66 * k + ui::text_h(c.p, q, answered_q) + 18 * k;
        ov::text(c.p, c.area.x + 130 * k, ay, ui::lifted(a, k), answered_a);
        ov::text(c.p, c.area.x + 130 * k, y0 + h - 30 * k, ui::ts(14 * k, ui::kMuted),
                 c.T("La reacción del paddock llega a las noticias.", "The paddock's reaction lands in the news."));
        return h;
    }
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
    // search, sort (the latest, the likeliest, by driver) and filter by the team they're linked with
    std::vector<const UiRumour *> list;
    std::vector<std::string> teams;
    for (const UiRumour &r : d.rumours) {
        teams.push_back(r.to);
        if (ui4::matches(r.driver, r.to, ""))
            list.push_back(&r);
    }
    static const std::vector<std::pair<const char *, const char *>> sorts = {
        {"RECIENTES", "LATEST"}, {"PROBABILIDAD", "CHANCE"}, {"PILOTO", "DRIVER"}};
    y += toolbar(c, y, sorts, teams, static_cast<int>(list.size()), "historias", "stories", false);
    if (ui4::sort_key == 1)
        std::stable_sort(list.begin(), list.end(), [](const UiRumour *a, const UiRumour *b) { return a->odds > b->odds; });
    else if (ui4::sort_key == 2)
        std::stable_sort(list.begin(), list.end(), [](const UiRumour *a, const UiRumour *b) { return a->driver < b->driver; });
    if (list.empty())
        return y - y0 + empty_state(c, y, ui::Icon::rumours, c.T("Nada coincide", "Nothing matches"),
                                    c.T("Prueba otra búsqueda u otro filtro.", "Try another search or filter."));
    const char *steps[3] = {c.T("RUMOR", "RUMOUR"), c.T("CHARLAS", "TALKS"), c.T("FIRMADO", "SIGNED")};
    for (const UiRumour *rp : list) {
        const UiRumour &r = *rp;
        float h = 86 * k;
        bool picked = pick_row(c, c.area.x, y, c.area.w, h, r.driver_id, ui4::K_DRIVER);
        if (c.visible(y, h)) {
            Reveal rv(c);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            uint32_t hot = r.stage ? ui::kAmber : 0x8E9AAFFF;
            ui::card(c.p, c.area.x, y, c.area.w, h, k, hov, hot);
            pick_mark(c, c.area.x, y, c.area.w, h, picked);
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
            // 1.1.0 phase 5: how likely it is to end in a signing
            if (r.odds > 0) {
                // a ring that fills to the chance
                float qx = sx - 74 * k, qy = y + 36 * k, qr = 21 * k;
                uint32_t oc = r.odds >= 66 ? ui::kGreen : r.odds >= 40 ? ui::kAmber : 0x8E9AAFFF;
                ov::circle_outline(c.p, qx, qy, qr, 4 * k, 0xFFFFFF14);
                float a0 = -1.5708f, a1 = a0 + 6.2832f * (static_cast<float>(r.odds) / 100.0f) * c.grow();
                ov::arc(c.p, qx, qy, qr, a0, a1, 4 * k, oc);
                if (r.odds >= 66)
                    ui::glow(c.p, qx, qy, 34 * k, ui::alpha(oc, 0.25f));
                ov::text(c.p, qx, qy, ui::ts(13 * k, ui::kText, ov::align::center | ov::align::middle), std::to_string(r.odds) + "%");
                ov::text(c.p, qx, y + 70 * k, ui::ts(10 * k, ui::kMuted, ov::align::center | ov::align::middle, 1.2f * k), c.T("PROBABILIDAD", "CHANCE"));
            }
            // who says so
            float ox = c.area.x + c.area.w - 22 * k;
            if (r.rel > 0) {
                ov::text(c.p, ox, y + 24 * k, ui::ts(15 * k, ui::kText2, ov::align::right | ov::align::middle), ui::fit(c.p, ui::ts(15 * k), r.outlet, 200 * k));
                ui::dots(c.p, ox - 52 * k, y + 54 * k, r.rel, k);
                ov::text(c.p, ox - 62 * k, y + 54 * k, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::middle, 1.2f * k),
                         c.T("FIABILIDAD", "RELIABILITY"));
            } else {
                ov::text(c.p, ox, y + 24 * k, ui::ts(15 * k, ui::kText2, ov::align::right | ov::align::middle), c.T("En el paddock", "Paddock talk"));
                ov::text(c.p, ox, y + 54 * k, ui::ts(11 * k, 0xFF9F0AFF, ov::align::right | ov::align::middle, 1.2f * k),
                         c.T("EFECTO DOMINÓ", "DOMINO EFFECT"));
            }
        }
        y += h + 10 * k;
    }
    return y - y0;
}

float move_row(const Ctx &c, float x0, float w, float y, const UiMove &m, bool with_year)
{
    float k = c.k, h = 60 * k;
    bool picked = pick_row(c, x0, y, w, h, m.driver_id, ui4::K_DRIVER); // a click opens the driver's card
    if (!c.visible(y, h))
        return h;
    Reveal r(c);
    ui::Tag tag = ui::move_tag(m.kind);
    ui::card(c.p, x0, y, w, h, k, c.hover(x0, y, w, h), tag.col);
    pick_mark(c, x0, y, w, h, picked);
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
            bool picked = pick_row(c, x, cy, w, h, u.driver_id, ui4::K_DRIVER);
            if (!c.visible(cy, h))
                continue;
            Reveal r(c);
            ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), ui::rgb(u.col));
            pick_mark(c, x, cy, w, h, picked);
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
    y += section_title(c, c.area.x, y, head,
                       d.finalised ? (d.grid_reveal ? c.T("R: repetir la presentación", "R: replay the reveal") : "")
                                   : c.T("con los fichajes de hoy", "with today's deals"));
    float gap = 14 * k, w = (c.area.w - gap) / 2, h = 142 * k;
    for (size_t i = 0; i < d.grid.size(); ++i) {
        const UiTeam &t = d.grid[i];
        float x = c.area.x + (i % 2) * (w + gap), cy = y + (i / 2) * (h + gap);
        // a driver's name opens his card; the rest of the team's box the team's
        int name_hov = -1;
        for (int s = 0; s < 2; ++s)
            if (!t.seat[s].id.empty() && c.hover(x + 54 * k, cy + 60 * k + s * 40 * k, w - 190 * k, 28 * k))
                name_hov = s;
        bool name_hit = name_hov >= 0 && c.in.clicked();
        if (name_hit)
            ui4::open_card(ui4::K_DRIVER, t.seat[name_hov].id);
        bool picked = pick_row(c, x, cy, w, h, name_hit ? std::string() : t.id, ui4::K_TEAM);
        if (!c.visible(cy, h))
            continue;
        Reveal r(c);
        bool hov = c.hover(x, cy, w, h);
        ui::card(c.p, x, cy, w, h, k, hov, ui::rgb(t.col));
        pick_mark(c, x, cy, w, h, picked);
        float lh = 48 * k;
        {
            ov::ClipScope clip(c.p, x, cy, w, lh);
            ov::rect_rounded(c.p, x, cy, w, lh + 14 * k, 12 * k, ui::rgb(t.col, 0x40));
            ui::livery(c.p, x, cy, w, lh, t.col, k, 1, 12 * k);
            ov::text(c.p, x + w - 16 * k, cy + lh / 2, ui::ts(34 * k, 0xFFFFFF26, ov::align::right | ov::align::middle, 1 * k), t.code);
        }
        ov::text(c.p, x + 18 * k, cy + lh / 2, ui::lifted(ui::ts(19 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 0.8f * k), k), t.name);
        if (!t.strategy.empty()) {
            ui::Tag st = ui::strategy_tag(t.strategy);
            float nx = x + 18 * k + ui::text_w(c.p, ui::ts(19 * k, 0xFFFFFFFF, ov::align::left, 0.8f * k), t.name) + 12 * k;
            if (nx + 150 * k < x + w - 110 * k)
                ui::tag(c.p, nx, cy + lh / 2 - 10 * k, c.T(st.es, st.en), st.col, k, 10, true);
        }
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
            uint32_t nc = seat.player ? ui::kGold : name_hov == s ? 0xFFFFFFFF : ui::kText;
            std::string shown = ui::fit(c.p, ui::ts(18 * k), seat.name, w - 190 * k);
            ov::text(c.p, x + 56 * k, sy + 14 * k, ui::ts(18 * k, nc, ov::align::left | ov::align::middle), shown);
            if (name_hov == s)
                ov::rect(c.p, x + 56 * k, sy + 25 * k, ui::text_w(c.p, ui::ts(18 * k), shown), 1.2f * k, 0xFFFFFFA0);
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
    const std::vector<UiDriver> &all = free_agents ? d.free_agents : d.drivers;
    float k = c.k, y = y0;
    if (all.empty())
        return empty_state(c, y0, free_agents ? ui::Icon::free : ui::Icon::drivers, c.T("Nadie en la lista", "Nobody on the list"),
                           c.T("Todos los pilotos tienen equipo.", "Every driver has a seat."));
    // the search, the filter and the order the player picked: worked out again only when one of them or the data
    // changes, not every frame (the list points into g_ov, which a new version replaces)
    struct Cache {
        int version = -1, sort = -1;
        bool free = false;
        std::string filter, query;
        std::vector<const UiDriver *> list;
        std::vector<std::string> teams;
    };
    static Cache cache;
    auto stale = [&] {
        return cache.version != d.version || cache.free != free_agents || cache.sort != ui4::sort_key || cache.filter != ui4::filter ||
               cache.query != ui4::query;
    };
    if (stale()) {
        cache.list.clear();
        cache.teams.clear();
        for (const UiDriver &u : all) {
            const std::string &team = free_agents ? u.last_team : u.team;
            cache.teams.push_back(team);
            if (ui4::matches(u.name, team, u.trait))
                cache.list.push_back(&u);
        }
    }
    static const std::vector<std::pair<const char *, const char *>> grid_sorts = {
        {"VALOR", "VALUE"}, {"EDAD", "AGE"}, {"CONTRATO", "CONTRACT"}, {"TENDENCIA", "TREND"}, {"NOMBRE", "NAME"}};
    static const std::vector<std::pair<const char *, const char *>> free_sorts = {
        {"POTENCIAL", "POTENTIAL"}, {"NIVEL", "LEVEL"}, {"EDAD", "AGE"}, {"NOMBRE", "NAME"}};
    y += toolbar(c, y, free_agents ? free_sorts : grid_sorts, cache.teams, static_cast<int>(cache.list.size()), "pilotos", "drivers");
    if (stale()) { // the toolbar may have changed the order or the filter: the next frame filters again
        int key = ui4::sort_key;
        std::stable_sort(cache.list.begin(), cache.list.end(), [&](const UiDriver *a, const UiDriver *b) {
            if (free_agents)
                return key == 1 ? a->value > b->value : key == 2 ? a->age < b->age : key == 3 ? a->name < b->name : a->potential > b->potential;
            return key == 1 ? a->age < b->age : key == 2 ? a->contract < b->contract : key == 3 ? a->trend > b->trend
                 : key == 4 ? a->name < b->name : a->value > b->value;
        });
        cache.version = d.version;
        cache.free = free_agents;
        cache.sort = ui4::sort_key;
        if (cache.filter != ui4::filter || cache.query != ui4::query)
            cache.version = -1; // filtered with the old filter: again next frame
        cache.filter = ui4::filter;
        cache.query = ui4::query;
    }
    const std::vector<const UiDriver *> &list = cache.list;
    if (list.empty())
        return y - y0 + empty_state(c, y, ui::Icon::drivers, c.T("Nadie coincide", "Nobody matches"),
                                    c.T("Prueba otra búsqueda u otro filtro.", "Try another search or filter."));
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
        } else {
            ov::text(c.p, cx_trend, hy, hs, c.T("PAPEL", "ROLE")); // 1.1.0 phase 6: reserve or academy driver
        }
    }
    y += 40 * k;
    int rank = 0;
    for (const UiDriver *up : list) {
        const UiDriver &u = *up;
        ++rank;
        float h = 50 * k;
        bool picked = pick_row(c, c.area.x, y, c.area.w, h, u.id, ui4::K_DRIVER);
        if (c.visible(y, h)) {
            Reveal r(c, 10);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            ov::rect_rounded(c.p, c.area.x, y, c.area.w, h, 9 * k, hov ? ui::kCardHi : (rank % 2 ? 0x1A1C25FF : 0x15171FFF));
            pick_mark(c, c.area.x, y, c.area.w, h, picked);
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
                bx += ui::tag(c.p, bx, my - 10 * k, c.T("RIVAL", "FEUD"), 0xBF5AF2FF, k, 10) + 4 * k;
            if (!u.trait.empty() && bx < cx_team - 90 * k) {
                ui::Tag tt = ui::trait_tag(u.trait);
                ui::tag(c.p, bx, my - 10 * k, c.T(tt.es, tt.en), tt.col, k, 10);
            }
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
            if (free_agents && !u.role.empty()) {
                bool reserve = u.role == "reserve";
                float tw = ui::tag(c.p, cx_trend, my - 10 * k, reserve ? c.T("RESERVA", "RESERVE") : c.T("ACADEMIA", "ACADEMY"),
                                   reserve ? 0x40C8E0FF : 0x32D74BFF, k, 10, reserve);
                ui::team_chip(c.p, cx_trend + tw + 8 * k, my - 11 * k, u.role_code, u.role_col, k, 22);
            }
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
    float y_start = y0;
    {
        std::vector<std::string> teams;
        int shown = 0;
        for (const UiDriver &u : d.drivers) {
            teams.push_back(u.team);
            shown += ui4::matches(u.name, u.team, u.trait);
        }
        y0 += toolbar(c, y0, {}, teams, shown, "pilotos", "drivers");
    }
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
            if (u.contract >= g.from && u.contract <= g.to && ui4::matches(u.name, u.team, u.trait))
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
            bool picked = pick_row(c, x, y, cw, tile_h, u->id, ui4::K_DRIVER);
            if (c.visible(y, tile_h)) {
                Reveal r(c, 10);
                bool hov = c.hover(x, y, cw, tile_h);
                ui::card(c.p, x, y, cw, tile_h, k, hov, ui::rgb(u->col));
                pick_mark(c, x, y, cw, tile_h, picked);
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
    return bottom - y_start;
}

float page_stock(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    // sorted once per version of the data
    static int sorted_version = -1;
    static std::vector<const UiDriver *> list;
    if (sorted_version != d.version) {
        sorted_version = d.version;
        list.clear();
        for (const UiDriver &u : d.drivers)
            list.push_back(&u);
        std::sort(list.begin(), list.end(), [](const UiDriver *a, const UiDriver *b) { return a->trend > b->trend; });
    }
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
        bool picked = pick_row(c, c.area.x, y, c.area.w, h, u->id, ui4::K_DRIVER);
        if (c.visible(y, h)) {
            Reveal r(c, 8);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            if (hov || picked)
                ov::rect_rounded(c.p, c.area.x, y, c.area.w, h, 7 * k, picked ? 0xE106002A : 0xFFFFFF0C);
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
                if (c.hover(bx, y, pw, ph)) { // a podium step opens its driver's card
                    ov::rect_rounded(c.p, bx - 6 * k, y + 6 * k, pw + 12 * k, ph - 6 * k, 12 * k, 0xFFFFFF08);
                    if (c.in.clicked())
                        ui4::open_from_list(ui4::K_DRIVER, u.id);
                }
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
        bool picked = pick_row(c, c.area.x, y, c.area.w, h, u.id, ui4::K_DRIVER);
        if (c.visible(y, h)) {
            Reveal r(c, 10);
            ui::card(c.p, c.area.x, y, c.area.w, h, k, c.hover(c.area.x, y, c.area.w, h), 0x64D2FFFF);
            pick_mark(c, c.area.x, y, c.area.w, h, picked);
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

// the scout's shortlist: who fits the player's team next season, and how keen each driver is
float page_scout(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    if (d.scout.empty())
        return empty_state(c, y0, ui::Icon::scout, c.T("El ojeador no tiene nombres todavía", "The scout has no names yet"),
                           c.T("La lista llega cuando hay pilotos disponibles para el año que viene.",
                               "The list fills once drivers are available for next season."));
    char head[64];
    std::snprintf(head, sizeof head, "%s %d", d.scout_own ? c.T("TU SEGUNDO ASIENTO EN", "YOUR SECOND SEAT IN") : c.T("A QUIÉN SIGUE TU EQUIPO PARA", "YOUR TEAM'S RADAR FOR"),
                  d.year + 1);
    y += section_title(c, c.area.x, y, head,
                       d.scout_own ? c.T("tú decides a quién fichas en My Team", "you sign them yourself in My Team")
                                   : c.T("según el plan del equipo", "by the team's plan"));
    std::vector<const UiScout *> list;
    std::vector<std::string> teams;
    for (const UiScout &u : d.scout) {
        teams.push_back(u.team);
        if (ui4::matches(u.name, u.team, u.trait))
            list.push_back(&u);
    }
    static const std::vector<std::pair<const char *, const char *>> sorts = {
        {"ENCAJE", "FIT"}, {"NIVEL", "LEVEL"}, {"POTENCIAL", "POTENTIAL"}, {"EDAD", "AGE"}, {"NOMBRE", "NAME"}};
    y += toolbar(c, y, sorts, teams, static_cast<int>(list.size()), "candidatos", "candidates");
    int key = ui4::sort_key;
    std::stable_sort(list.begin(), list.end(), [&](const UiScout *a, const UiScout *b) {
        return key == 1 ? a->value > b->value : key == 2 ? a->potential > b->potential : key == 3 ? a->age < b->age
             : key == 4 ? a->name < b->name : a->fit > b->fit;
    });
    const char *status_es[3] = {"LIBRE", "ACABA CONTRATO", "F2"}, *status_en[3] = {"FREE AGENT", "OUT OF CONTRACT", "F2"};
    const char *interest_es[3] = {"DIFÍCIL", "ESCUCHA", "INTERESADO"}, *interest_en[3] = {"HARD SELL", "OPEN", "KEEN"};
    const uint32_t interest_col[3] = {ui::kDanger, ui::kAmber, ui::kGreen};
    float best = d.scout.front().fit;
    int rank = 0;
    for (const UiScout *up : list) {
        const UiScout &u = *up;
        ++rank;
        float h = 74 * k;
        bool picked = pick_row(c, c.area.x, y, c.area.w, h, u.id, ui4::K_DRIVER);
        if (c.visible(y, h)) {
            Reveal r(c, 10);
            bool hov = c.hover(c.area.x, y, c.area.w, h);
            ui::card(c.p, c.area.x, y, c.area.w, h, k, hov, u.col ? ui::rgb(u.col) : 0x40C8E0FF);
            pick_mark(c, c.area.x, y, c.area.w, h, picked);
            float my = y + h / 2;
            ov::circle(c.p, c.area.x + 34 * k, my, 16 * k, rank == 1 ? ui::kGold : 0x2A2D3AFF);
            ov::text(c.p, c.area.x + 34 * k, my, ui::ts(15 * k, rank == 1 ? 0x111111FF : ui::kText, ov::align::center | ov::align::middle),
                     std::to_string(rank));
            float x = c.area.x + 70 * k;
            ov::text(c.p, x, y + 22 * k, ui::ts(19 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(19 * k), u.name, 300 * k));
            int si = u.status == "free" ? 0 : u.status == "expiring" ? 1 : 2;
            char sub[96];
            std::snprintf(sub, sizeof sub, "%s  ·  %d %s%s%s", c.T(status_es[si], status_en[si]), u.age, c.T("años", "y/o"),
                          u.team.empty() ? "" : "  ·  ", u.team.c_str());
            ov::text(c.p, x, y + 50 * k, ui::ts(13.5f * k, ui::kText2, ov::align::left | ov::align::middle), sub);
            if (!u.trait.empty()) {
                ui::Tag tt = ui::trait_tag(u.trait);
                ui::tag(c.p, x + 330 * k, y + 12 * k, c.T(tt.es, tt.en), tt.col, k, 10);
            }
            // level and potential
            float bx = c.area.x + 480 * k, bw = 200 * k;
            ov::rect_rounded(c.p, bx, my - 4 * k, bw, 8 * k, 4 * k, 0xFFFFFF12);
            ov::rect_rounded(c.p, bx, my - 4 * k, std::max(8 * k, bw * std::clamp((u.potential - 55) / 45.0f, 0.0f, 1.0f) * c.grow()), 8 * k, 4 * k,
                             0x40C8E055);
            ui::bar(c.p, bx, my - 4 * k, bw, 8 * k, (u.value - 55) / 45.0f * c.grow(), 0x40C8E0FF);
            char v[32];
            std::snprintf(v, sizeof v, "%.0f / %.0f", u.value, u.potential);
            ov::text(c.p, bx + bw + 12 * k, my, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle), v);
            // the fit: how close to the scout's first choice
            float fx = c.area.x + 820 * k;
            if (fx + 200 * k < c.area.x + c.area.w - 160 * k) {
                ov::text(c.p, fx, y + 22 * k, ui::ts(11 * k, ui::kMuted, ov::align::left | ov::align::middle, 1.2f * k), c.T("ENCAJE", "FIT"));
                float f = std::clamp(1.0f - (best - u.fit) / 25.0f, 0.1f, 1.0f);
                ui::meter(c.p, fx, y + 42 * k, 5, static_cast<int>(std::lround(f * 5)), 0x40C8E0FF, k);
            }
            // how keen he is
            int iv = std::clamp(u.interest, 0, 2);
            float tw = ui::text_w(c.p, ui::ts(12 * k), c.T(interest_es[iv], interest_en[iv])) + 24 * k;
            ui::tag(c.p, c.area.x + c.area.w - tw - 20 * k, my - 11 * k, c.T(interest_es[iv], interest_en[iv]), interest_col[iv], k, 12, true);
        }
        y += h + 8 * k;
    }
    return y - y0;
}

float page_staff(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    std::vector<const UiTeam *> list;
    for (const UiTeam &t : d.teams)
        if (t.id != "#own") // My Team: the player runs it, no technical director of the market's
            list.push_back(&t);
    std::sort(list.begin(), list.end(), [](const UiTeam *a, const UiTeam *b) { return a->td_rating > b->td_rating; });
    y += section_title(c, c.area.x, y, c.T("DIRECTORES TÉCNICOS", "TECHNICAL DIRECTORS"),
                       c.T("cada invierno mueven el nivel del coche", "they move the car's level every winter"));
    float gap = 14 * k, w = (c.area.w - gap) / 2, h = 108 * k;
    for (size_t i = 0; i < list.size(); ++i) {
        const UiTeam &t = *list[i];
        float x = c.area.x + (i % 2) * (w + gap), cy = y + (i / 2) * (h + gap);
        bool picked = pick_row(c, x, cy, w, h, t.id, ui4::K_TEAM);
        if (!c.visible(cy, h))
            continue;
        Reveal r(c);
        ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), ui::rgb(t.col));
        pick_mark(c, x, cy, w, h, picked);
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

// ---------------------------------------------------------------------------------------------------
// 1.1.0 phase 5: the player's future, the standings, the records

// a percentage as a thin bar with its number, coloured by how high it is
void odds_bar(const Ctx &c, float x, float y, float w, int pct)
{
    float k = c.k;
    uint32_t col = pct >= 66 ? ui::kGreen : pct >= 40 ? ui::kAmber : 0x8E9AAFFF;
    ui::bar(c.p, x, y - 3 * k, w - 46 * k, 6 * k, std::clamp(pct / 100.0f, 0.0f, 1.0f) * c.grow(), col);
    ov::text(c.p, x + w, y, ui::ts(15 * k, ui::kText, ov::align::right | ov::align::middle), std::to_string(pct) + "%");
}

float page_future(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    const UiFuture &f = d.future;
    float k = c.k, y = y0;
    if (!f.on)
        return empty_state(c, y0, ui::Icon::future, c.T("Tu futuro, todavía en blanco", "Your future is still a blank page"),
                           c.T("El mercado te conocerá en cuanto vuelvas a Team HQ con la parrilla cargada.",
                               "The market gets to know you once you're back at Team HQ with the grid loaded."));
    // the head: who you are, what you're worth, your contract and how things stand with your team
    {
        float h = 150 * k;
        Reveal r(c);
        uint32_t col = f.col ? ui::rgb(f.col) : ui::kRed;
        ui::card(c.p, c.area.x, y, c.area.w, h, k, false, col);
        {
            ov::ClipScope clip(c.p, c.area.x, y, c.area.w, h);
            ov::gradient(c.p, c.area.x, y, c.area.w * 0.6f, h, ui::alpha(col, 0.30f), ui::alpha(col, 0.0f), true);
            ui::glow(c.p, c.area.x + 60 * k, y + 40 * k, 220 * k, ui::alpha(col, 0.25f));
        }
        float x = c.area.x + 28 * k;
        ov::text(c.p, x, y + 24 * k, ui::lifted(ui::ts(30 * k, ui::kText, ov::align::left | ov::align::top, 0.4f * k), k),
                 ui::fit(c.p, ui::ts(30 * k), f.name, c.area.w * 0.45f));
        float cx = x;
        if (!f.code.empty())
            cx += ui::team_chip(c.p, cx, y + 70 * k, f.code, f.col, k, 24) + 10 * k;
        ov::text(c.p, cx, y + 82 * k, ui::ts(16 * k, ui::kText2, ov::align::left | ov::align::middle),
                 f.team.empty() ? std::string(c.T("Sin equipo", "No team")) : f.team);
        // the tags: your word to the press, the team's confidence, reinforcements asked for
        float tx = x, ty = y + 112 * k;
        const char *word = f.loyalty > 0 ? c.T("COMPROMETIDO", "COMMITTED") : f.loyalty < 0 ? c.T("ESCUCHA OFERTAS", "LISTENING TO OFFERS") : nullptr;
        if (word)
            tx += ui::tag(c.p, tx, ty, word, f.loyalty > 0 ? ui::kBlue : ui::kAmber, k, 11) + 8 * k;
        const char *conf = f.comfort >= 2 ? c.T("EL EQUIPO CONFÍA EN TI", "YOUR TEAM BACKS YOU")
                           : f.comfort == 1 ? c.T("BUENA RELACIÓN CON EL EQUIPO", "GOOD TERMS WITH THE TEAM")
                           : f.comfort == -1 ? c.T("TENSIÓN CON EL EQUIPO", "TENSION WITH THE TEAM")
                           : f.comfort <= -2 ? c.T("EL EQUIPO DUDA DE TI", "YOUR TEAM HAS DOUBTS")
                                             : nullptr;
        if (conf)
            tx += ui::tag(c.p, tx, ty, conf, f.comfort > 0 ? ui::kGreen : ui::kDanger, k, 11) + 8 * k;
        if (f.push)
            tx += ui::tag(c.p, tx, ty, c.T("PIDES REFUERZOS", "REINFORCEMENTS ASKED"), ui::kRed, k, 11) + 8 * k;
        // the numbers, on the right
        struct Num {
            std::string v;
            const char *es, *en;
        };
        char val[16], pot[16];
        std::snprintf(val, sizeof val, "%.0f", f.value);
        std::snprintf(pot, sizeof pot, "%.0f", f.potential);
        // the game's rating first (its ratings screen's number), when it was read
        Num nums[4] = {{f.game_rating > 0 ? std::to_string(f.game_rating) : std::string(), "VALORACIÓN", "RATING"},
                       {val, "VALOR DE MERCADO", "MARKET VALUE"},
                       {pot, "POTENCIAL", "POTENTIAL"},
                       {f.contract > 0 ? std::to_string(f.contract) : std::string("-"), "CONTRATO HASTA", "CONTRACT UNTIL"}};
        float nx = c.area.x + c.area.w - 28 * k;
        for (int i = 3; i >= 0; --i) {
            if (nums[i].v.empty())
                continue;
            ov::TextStyle vs = ui::lifted(ui::ts(38 * k, ui::kText, ov::align::right | ov::align::top), k);
            ov::text(c.p, nx, y + 34 * k, vs, nums[i].v);
            ov::text(c.p, nx, y + 88 * k, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::top, 1.4f * k), c.T(nums[i].es, nums[i].en));
            nx -= std::max(ui::text_w(c.p, vs, nums[i].v), 120 * k) + 36 * k;
        }
        y += h + 24 * k;
    }
    // left: who wants you; right: your value race by race and, in My Team, your team against the rest
    float gap = 24 * k, lw = (c.area.w - gap) * 0.52f, rx = c.area.x + lw + gap, rw = c.area.w - lw - gap;
    float ly = y, ry = y;
    ly += section_title(c, c.area.x, ly, c.T("QUIÉN TE QUIERE", "WHO WANTS YOU"), c.T("interés de cada equipo", "each team's interest"), lw);
    for (const UiSuitor &s : f.suitors) {
        float h = 52 * k;
        bool own = s.status == "own";
        bool picked = pick_row(c, c.area.x, ly, lw, h, s.id, ui4::K_TEAM);
        if (c.visible(ly, h)) {
            Reveal r(c, 8);
            ui::card(c.p, c.area.x, ly, lw, h, k, c.hover(c.area.x, ly, lw, h), own ? ui::kBlue : ui::rgb(s.col));
            pick_mark(c, c.area.x, ly, lw, h, picked);
            float x = c.area.x + 16 * k;
            ui::team_chip(c.p, x, ly + h / 2 - 11 * k, s.code, s.col, k, 22);
            ov::text(c.p, x + 70 * k, ly + h / 2, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(16 * k), s.name, 170 * k));
            const char *st_es = own ? "TE QUIERE RETENER" : s.status == "keen" ? "MUY INTERESADO" : s.status == "watching" ? "TE SIGUE" : "FRÍO";
            const char *st_en = own ? "WANTS TO KEEP YOU" : s.status == "keen" ? "KEEN" : s.status == "watching" ? "WATCHING" : "COOL";
            uint32_t sc = own ? ui::kBlue : s.status == "keen" ? ui::kGreen : s.status == "watching" ? ui::kAmber : 0x8E9AAFFF;
            float tw = ui::tag(c.p, x + 250 * k, ly + h / 2 - 10 * k, c.T(st_es, st_en), sc, k, 10);
            if (s.open && !own)
                ui::tag(c.p, x + 258 * k + tw, ly + h / 2 - 10 * k, c.T("ASIENTO LIBRE", "SEAT OPEN"), ui::kGreen, k, 10, true);
            float bx = c.area.x + lw - 190 * k;
            odds_bar(c, bx, ly + h / 2, 172 * k, s.interest);
        }
        ly += h + 8 * k;
    }
    if (f.suitors.empty()) {
        ov::text(c.p, c.area.x, ly + 6 * k, ui::ts(15 * k, ui::kMuted), c.T("Nadie pregunta por ti todavía.", "Nobody is asking about you yet."));
        ly += 36 * k;
    }
    ry += section_title(c, rx, ry, c.T("TU VALOR, CARRERA A CARRERA", "YOUR VALUE, RACE BY RACE"), {}, rw);
    if (c.visible(ry, 220 * k)) {
        Reveal r(c);
        UiDriverCard chart;
        chart.values = f.values;
        chart.values_last = f.values_last;
        value_chart(c.p, rx, ry, rw, 220 * k, k, chart, c.es, c.grow());
    }
    ry += 236 * k;
    if (f.own_team) {
        ry += section_title(c, rx, ry, c.T("TU EQUIPO FRENTE AL RESTO", "YOUR TEAM AGAINST THE REST"), {}, rw);
        float h = 118 * k;
        if (c.visible(ry, h)) {
            Reveal r(c);
            ui::card(c.p, rx, ry, rw, h, k, false, 0x5E5CE6FF);
            char rank[16];
            std::snprintf(rank, sizeof rank, "P%d", f.own_rank);
            ov::text(c.p, rx + 22 * k, ry + 22 * k, ui::lifted(ui::ts(40 * k, ui::kText, ov::align::left | ov::align::top), k), rank);
            ov::text(c.p, rx + 22 * k, ry + 80 * k, ui::ts(14 * k, ui::kText2, ov::align::left | ov::align::top),
                     std::to_string(f.own_points) + c.T(" puntos", " points"));
            float tx = rx + 150 * k;
            char line[160];
            if (!f.ahead.empty()) {
                std::snprintf(line, sizeof line, c.T("A %d pts de %s", "%d pts behind %s"), f.gap_ahead, f.ahead.c_str());
                ov::text(c.p, tx, ry + 38 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(16 * k), line, rw - 170 * k));
            } else {
                ov::text(c.p, tx, ry + 38 * k, ui::ts(16 * k, ui::kGold, ov::align::left | ov::align::middle), c.T("Lideras el campeonato de constructores", "You lead the constructors"));
            }
            if (!f.behind.empty()) {
                std::snprintf(line, sizeof line, c.T("%d pts por delante de %s", "%d pts ahead of %s"), f.gap_behind, f.behind.c_str());
                ov::text(c.p, tx, ry + 74 * k, ui::ts(16 * k, ui::kText2, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(16 * k), line, rw - 170 * k));
            }
        }
        ry += h + 16 * k;
    }
    y = std::max(ly, ry) + 16 * k;
    // what the paddock says about you
    y += section_title(c, c.area.x, y, c.T("LO QUE SE DICE DE TI", "WHAT THEY SAY ABOUT YOU"));
    if (f.stories.empty()) {
        if (c.visible(y, 40 * k))
            ov::text(c.p, c.area.x, y + 6 * k, ui::ts(16 * k, ui::kText2), c.T("Silencio en el paddock, por ahora.", "Silence in the paddock, for now."));
        y += 40 * k;
    }
    for (const UiNews &n : f.stories)
        y += news_card(c, c.area.x, y, c.area.w, n, true) + 10 * k;
    return y - y0;
}

float page_standings(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, y = y0;
    if (d.standings.empty())
        return empty_state(c, y0, ui::Icon::standings, c.T("Sin carreras todavía", "No races yet"),
                           c.T("La clasificación aparece después de la primera carrera.", "The standings appear after the first race."));
    float gap = 24 * k, lw = (c.area.w - gap) * 0.58f, rx = c.area.x + lw + gap, rw = c.area.w - lw - gap;
    float ly = y, ry = y;
    char note[48];
    std::snprintf(note, sizeof note, c.T("tras %d carreras", "after %d races"), d.round);
    ly += section_title(c, c.area.x, ly, c.T("PILOTOS", "DRIVERS"), note, lw);
    int lead = std::max(1, d.standings.front().points);
    for (size_t i = 0; i < d.standings.size(); ++i) {
        const UiStanding &s = d.standings[i];
        float h = 42 * k;
        bool picked = pick_row(c, c.area.x, ly, lw, h, s.id, ui4::K_DRIVER);
        if (c.visible(ly, h)) {
            Reveal r(c, 6);
            uint32_t pc = i == 0 ? ui::kGold : i == 1 ? 0xC7CCD6FF : i == 2 ? 0xCD8A4FFF : ui::kText2;
            ov::rect_rounded(c.p, c.area.x, ly, lw, h, 9 * k, s.player ? 0xE106002A : c.hover(c.area.x, ly, lw, h) ? 0xFFFFFF10 : 0xFFFFFF07);
            pick_mark(c, c.area.x, ly, lw, h, picked);
            ov::text(c.p, c.area.x + 26 * k, ly + h / 2, ui::ts(16 * k, pc, ov::align::center | ov::align::middle), std::to_string(i + 1));
            ov::rect_rounded(c.p, c.area.x + 48 * k, ly + 9 * k, 4 * k, h - 18 * k, 2 * k, s.col ? ui::rgb(s.col) : ui::kMuted);
            ov::text(c.p, c.area.x + 62 * k, ly + h / 2, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(16 * k), s.name, 230 * k));
            if (!s.code.empty())
                ui::team_chip(c.p, c.area.x + 300 * k, ly + h / 2 - 10 * k, s.code, s.col, k, 20);
            char wp[48];
            std::snprintf(wp, sizeof wp, c.T("%d V  ·  %d P", "%d W  ·  %d P"), s.wins, s.podiums);
            ov::text(c.p, c.area.x + 370 * k, ly + h / 2, ui::ts(13 * k, ui::kMuted, ov::align::left | ov::align::middle), wp);
            // the last five races: gold a win, silver a podium, green the points, grey the rest, red out
            float fx = c.area.x + 470 * k;
            for (size_t j = 0; j < s.last.size(); ++j) {
                int pos = s.last[j];
                uint32_t fc = pos == 1 ? ui::kGold : pos >= 2 && pos <= 3 ? 0xC7CCD6FF : pos >= 4 && pos <= 10 ? 0x1F9D45FF
                              : pos < 0 ? 0x8A2A26FF : 0x3A3D4AFF;
                float bx2 = fx + static_cast<float>(j) * 26 * k;
                ov::rect_rounded(c.p, bx2, ly + h / 2 - 11 * k, 22 * k, 22 * k, 5 * k, fc);
                ov::text(c.p, bx2 + 11 * k, ly + h / 2, ui::ts(11 * k, pos >= 1 && pos <= 3 ? 0x111111FF : ui::kText, ov::align::center | ov::align::middle),
                         pos < 0 ? std::string("X") : std::to_string(pos));
            }
            float bx = c.area.x + 610 * k, bw = lw - 610 * k - 80 * k;
            if (bw > 30 * k)
                ui::bar(c.p, bx, ly + h / 2 - 3 * k, bw, 6 * k, static_cast<float>(s.points) / lead * c.grow(), s.col ? ui::rgb(s.col) : ui::kRed);
            ov::text(c.p, c.area.x + lw - 16 * k, ly + h / 2 - (i ? 6 * k : 0), ui::ts(17 * k, ui::kText, ov::align::right | ov::align::middle),
                     std::to_string(s.points));
            if (i) // the gap to the leader
                ov::text(c.p, c.area.x + lw - 16 * k, ly + h / 2 + 11 * k, ui::ts(11 * k, ui::kMuted, ov::align::right | ov::align::middle),
                         "-" + std::to_string(d.standings.front().points - s.points));
        }
        ly += h + 6 * k;
    }
    ry += section_title(c, rx, ry, c.T("CONSTRUCTORES", "CONSTRUCTORS"), {}, rw);
    std::vector<const UiTeam *> teams;
    for (const UiTeam &t : d.teams)
        teams.push_back(&t);
    std::sort(teams.begin(), teams.end(), [](const UiTeam *a, const UiTeam *b) { return a->points != b->points ? a->points > b->points : a->rank < b->rank; });
    int tlead = teams.empty() ? 1 : std::max(1, teams.front()->points);
    for (size_t i = 0; i < teams.size(); ++i) {
        const UiTeam &t = *teams[i];
        float h = 50 * k;
        bool picked = pick_row(c, rx, ry, rw, h, t.id, ui4::K_TEAM);
        if (c.visible(ry, h)) {
            Reveal r(c, 6);
            ui::card(c.p, rx, ry, rw, h, k, c.hover(rx, ry, rw, h), ui::rgb(t.col));
            pick_mark(c, rx, ry, rw, h, picked);
            ov::text(c.p, rx + 22 * k, ry + h / 2, ui::ts(16 * k, i == 0 ? ui::kGold : ui::kText2, ov::align::center | ov::align::middle), std::to_string(i + 1));
            ui::team_chip(c.p, rx + 44 * k, ry + h / 2 - 11 * k, t.code, t.col, k, 22);
            ov::text(c.p, rx + 110 * k, ry + h / 2 - 7 * k, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(15 * k), t.name, rw - 190 * k));
            ui::bar(c.p, rx + 110 * k, ry + h / 2 + 10 * k, rw - 190 * k, 4 * k, static_cast<float>(t.points) / tlead * c.grow(), ui::rgb(t.col));
            ov::text(c.p, rx + rw - 16 * k, ry + h / 2, ui::ts(17 * k, ui::kText, ov::align::right | ov::align::middle), std::to_string(t.points));
        }
        ry += h + 8 * k;
    }
    return std::max(ly, ry) - y0;
}

// the records of this career: the most titles, wins, podiums, starts and points
float records_block(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    if (d.records.empty())
        return 0;
    float k = c.k, y = y0;
    y += section_title(c, c.area.x, y, c.T("RÉCORDS", "RECORDS"), c.T("en esta carrera deportiva", "in this career"));
    float gap = 12 * k, cols = std::min<float>(static_cast<float>(d.records.size()), 5), w = (c.area.w - gap * (cols - 1)) / cols, h = 104 * k;
    for (size_t i = 0; i < d.records.size(); ++i) {
        const UiRecord &r = d.records[i];
        float x = c.area.x + static_cast<float>(i % 5) * (w + gap), cy = y + static_cast<float>(i / 5) * (h + gap);
        if (!c.visible(cy, h))
            continue;
        Reveal rv(c);
        ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), ui::kGold);
        const char *es = r.key == "titles" ? "TÍTULOS" : r.key == "wins" ? "VICTORIAS" : r.key == "podiums" ? "PODIOS" : r.key == "starts" ? "CARRERAS" : "PUNTOS";
        const char *en = r.key == "titles" ? "TITLES" : r.key == "wins" ? "WINS" : r.key == "podiums" ? "PODIUMS" : r.key == "starts" ? "STARTS" : "POINTS";
        ov::text(c.p, x + 16 * k, cy + 20 * k, ui::ts(11 * k, ui::kGold, ov::align::left | ov::align::middle, 1.8f * k), c.T(es, en));
        ov::text(c.p, x + w - 16 * k, cy + 14 * k, ui::lifted(ui::ts(30 * k, ui::kText, ov::align::right | ov::align::top), k), std::to_string(c.count(r.value)));
        ov::text(c.p, x + 16 * k, cy + 60 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(16 * k), r.name, w - 32 * k));
        ov::text(c.p, x + 16 * k, cy + 84 * k, ui::ts(12 * k, ui::kText2, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(12 * k), r.detail, w - 32 * k));
    }
    y += std::ceil(static_cast<float>(d.records.size()) / 5.0f) * (h + gap) + 14 * k;
    return y - y0;
}

// the season awards, newest season first: a card per award
float awards_block(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    if (d.awards.empty())
        return 0;
    float k = c.k, y = y0;
    y += section_title(c, c.area.x, y, c.T("SALÓN DE LA FAMA", "HALL OF FAME"), c.T("los premios de cada temporada", "every season's awards"));
    int year = d.awards.front().year;
    std::vector<const UiAward *> row;
    for (const UiAward &a : d.awards)
        if (a.year == year)
            row.push_back(&a);
    std::reverse(row.begin(), row.end()); // champion first
    float gap = 12 * k, cols = std::min<float>(static_cast<float>(row.size()), 3), w = (c.area.w - gap * (cols - 1)) / cols, h = 92 * k;
    for (size_t i = 0; i < row.size(); ++i) {
        const UiAward &a = *row[i];
        float x = c.area.x + static_cast<float>(i % 3) * (w + gap), cy = y + static_cast<float>(i / 3) * (h + gap);
        if (!c.visible(cy, h))
            continue;
        Reveal r(c);
        bool gold = a.key == "champion" || a.key == "constructors";
        uint32_t col = gold ? ui::kGold : a.key == "rookie" ? 0x64D2FFFF : a.key == "overachiever" ? ui::kGreen : ui::kAmber;
        ui::card(c.p, x, cy, w, h, k, c.hover(x, cy, w, h), col);
        {
            ov::ClipScope clip(c.p, x, cy, w, h);
            ov::gradient(c.p, x, cy, w * 0.6f, h, ui::alpha(col, 0.16f), ui::alpha(col, 0.0f), true);
        }
        const char *es = a.key == "champion" ? "CAMPEÓN" : a.key == "constructors" ? "CONSTRUCTORES" : a.key == "rookie" ? "NOVATO DEL AÑO"
                       : a.key == "overachiever" ? "MÁS QUE SU COCHE" : "EL QUE MÁS CRECE";
        const char *en = a.key == "champion" ? "CHAMPION" : a.key == "constructors" ? "CONSTRUCTORS" : a.key == "rookie" ? "ROOKIE OF THE YEAR"
                       : a.key == "overachiever" ? "BEAT THE CAR" : "MOST IMPROVED";
        char label[64];
        std::snprintf(label, sizeof label, "%s %d", c.T(es, en), a.year);
        ov::text(c.p, x + 18 * k, cy + 22 * k, ui::ts(11.5f * k, col, ov::align::left | ov::align::middle, 1.8f * k), label);
        ov::text(c.p, x + 18 * k, cy + 50 * k, ui::ts(19 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(19 * k), a.name, w - 36 * k));
        ov::text(c.p, x + 18 * k, cy + 74 * k, ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::middle), ui::fit(c.p, ui::ts(13 * k), a.detail, w - 36 * k));
    }
    y += std::ceil(static_cast<float>(row.size()) / 3.0f) * (h + gap) + 10 * k;
    // the champions of the seasons before, one line each
    for (const UiAward &a : d.awards) {
        if (a.year == year || a.key != "champion")
            continue;
        float h2 = 34 * k;
        if (c.visible(y, h2)) {
            Reveal r(c, 8);
            ov::rect_rounded(c.p, c.area.x, y, c.area.w, h2, 8 * k, 0xFFFFFF08);
            ov::text(c.p, c.area.x + 18 * k, y + h2 / 2, ui::ts(14 * k, ui::kGold, ov::align::left | ov::align::middle), std::to_string(a.year));
            ov::text(c.p, c.area.x + 80 * k, y + h2 / 2, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle), a.name);
            ov::text(c.p, c.area.x + c.area.w - 18 * k, y + h2 / 2, ui::ts(13 * k, ui::kText2, ov::align::right | ov::align::middle), a.detail);
        }
        y += h2 + 6 * k;
    }
    return y - y0 + 14 * k;
}

float page_history(const Ctx &c, float y0)
{
    const OverlayData &d = g_ov;
    float k = c.k, start = y0;
    float ah = records_block(c, y0); // the records, then the awards, above the timeline
    ah += awards_block(c, y0 + ah);
    y0 += ah;
    float y = y0;
    if (d.history.empty())
        return ah + empty_state(c, y0, ui::Icon::history, c.T("La historia está por escribir", "History is yet to be written"),
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
    return y - start;
}

// the season bar in the header: where the season stands, race by race, with the market's windows on it (the
// mid-season changes, deadline day) and the grid's close at the end
void draw_season_bar(ov::Paint *p, const OverlayData &d, float x, float y, float w, float k)
{
    int R = std::max(1, d.rounds), done = std::clamp(d.round, 0, R);
    auto T = [&](const char *es, const char *en) { return d.es ? es : en; };
    char head[48], next[48];
    std::snprintf(head, sizeof head, "%s %d / %d", T("CARRERA", "ROUND"), done, R);
    if (d.finalised || done >= R)
        std::snprintf(next, sizeof next, "%s", T("TEMPORADA TERMINADA", "SEASON OVER"));
    else
        std::snprintf(next, sizeof next, "%s %d", T("PRÓXIMA: CARRERA", "NEXT: ROUND"), done + 1);
    ov::text(p, x, y, ui::ts(11.5f * k, ui::kText2, ov::align::left | ov::align::top, 2.2f * k), head);
    ov::text(p, x + w, y, ui::ts(11.5f * k, ui::kMuted, ov::align::right | ov::align::top, 1.6f * k), next);
    float ty = y + 24 * k, th = 6 * k;
    auto at = [&](float r) { return x + w * std::clamp(r / static_cast<float>(R), 0.0f, 1.0f); };
    ov::rect_rounded(p, x, ty, w, th, th / 2, 0xFFFFFF12);
    // the windows: mid-season changes (40% to 65% of the season), deadline day (the last two races)
    struct Band {
        float from, to;
        uint32_t col;
        const char *es, *en;
    };
    std::vector<Band> bands;
    if (d.midseason_on)
        bands.push_back({std::ceil(0.40f * R) - 1, std::floor(0.65f * R), ui::kAmber, "CAMBIOS", "MID-SEASON"});
    if (d.deadline_on && R >= 8)
        bands.push_back({static_cast<float>(R - 3), static_cast<float>(R - 1), ui::kRed, "DEADLINE", "DEADLINE"});
    for (const Band &b : bands) {
        float bx = at(b.from), bw = at(b.to) - bx;
        ov::rect_rounded(p, bx, ty - 2 * k, bw, th + 4 * k, 3 * k, ui::alpha(b.col, 0.22f));
        ov::text(p, bx + bw / 2, ty + th + 6 * k, ui::ts(9.5f * k, ui::alpha(b.col, 0.9f), ov::align::center | ov::align::top, 1.2f * k),
                 T(b.es, b.en));
    }
    // the races run
    float fw = at(static_cast<float>(done)) - x;
    if (fw > 0)
        ov::gradient(p, x, ty, std::max(th, fw), th, 0xE1060050, ui::kRed, true, th / 2);
    for (int r = 1; r < R; ++r)
        ov::rect(p, at(static_cast<float>(r)) - 0.5f * k, ty + 1 * k, 1 * k, th - 2 * k, r <= done ? 0xFFFFFF30 : 0xFFFFFF14);
    // the grid's close: a flag at the end
    float ex = x + w;
    ov::rect(p, ex - 1 * k, ty - 7 * k, 2 * k, th + 14 * k, d.finalised ? ui::kGold : 0xFFFFFF40);
    // where the season is now
    if (done < R) {
        float cx = at(static_cast<float>(done) + 0.5f);
        ui::live_dot(p, cx, ty + th / 2, 4.5f * k, 0xFFFFFFFF, 1.6f);
    }
}

// the headline ticker under the header: the latest stories scroll past like a broadcast crawl
void draw_ticker(ov::Paint *p, const OverlayData &d, float x, float y, float w, float h, float k)
{
    ov::rect(p, x, y, w, h, 0x00000040);
    ov::rect(p, x, y, w, 1 * k, 0xFFFFFF10);
    ov::rect(p, x, y + h - 1 * k, w, 1 * k, 0xFFFFFF10);
    bool dl = d.deadline && !d.finalised;
    float tagw = (dl ? 196 : 150) * k;
    ui::para(p, x, y, tagw + 14 * k, h, 14 * k, ui::kRed);
    ui::para(p, x, y, tagw + 14 * k, h * 0.45f, 6 * k, 0xFFFFFF14);
    float dot_x = x + 26 * k;
    ov::circle(p, dot_x, y + h / 2, 4.5f * k, 0xFFFFFF60 + static_cast<uint32_t>(0x9F * ui::pulse(1.2f)));
    const char *label = d.deadline && !d.finalised ? (d.deadline == 1 ? "DEADLINE · 1 TO GO" : "DEADLINE · 2 TO GO")
                                                   : d.es ? "ÚLTIMA HORA" : "LATEST";
    ov::text(p, dot_x + 14 * k, y + h / 2, ui::ts(d.deadline && !d.finalised ? 12.5f * k : 14 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 1.6f * k),
             label);
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
        std::string line = n.body.substr(0, n.body.find('\n')); // a list (the grid, the awards): its first line
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
    float k = ov::ui_scale(p) * std::clamp(d.scale, 0.8f, 1.25f); // the "Panel size" option
    auto T = [&](const char *es_text, const char *en_text) { return d.es ? es_text : en_text; };

    // a card or the search box takes the keys first
    bool card_open = ui4::modal != 0;
    if (card_open && (in.key_pressed(VK_ESCAPE) || in.pad_hit(ov::pad::b))) {
        ui4::close_card();
        card_open = ui4::modal != 0;
    }
    // a card opened from a list: Left/Right (LB/RB) step to the list's previous or next one
    if (card_open && (ui4::modal == ui4::K_DRIVER || ui4::modal == ui4::K_TEAM)) {
        int step = (in.key_typed(VK_RIGHT) || in.pad_hit(ov::pad::rb) || in.pad_hit(ov::pad::right)) ? 1
                   : (in.key_typed(VK_LEFT) || in.pad_hit(ov::pad::lb) || in.pad_hit(ov::pad::left))  ? -1
                                                                                                       : 0;
        if (step)
            ui4::browse_step(step);
    }
    // the global search: / (Y on a pad) on any page without a table of its own
    bool table_page = tab == P_DRIVERS || tab == P_FREE || tab == P_CONTRACTS || tab == P_SCOUT || tab == P_RUMOURS;
    if (!card_open && !ui4::typing && !table_page &&
        (in.key_pressed(VK_OEM_2) || in.key_pressed(VK_DIVIDE) || (in.pad_hit(ov::pad::y) && !in.pad_held(ov::pad::back)))) {
        ui4::open_search();
        card_open = true;
    }
    if (ui4::typing) {
        ui4::type_into(in);
        if (in.key_pressed(VK_RETURN))
            ui4::typing = false;
        if (in.key_pressed(VK_ESCAPE)) {
            ui4::typing = false;
            ui4::query.clear();
        }
    }
    bool keys_free = !ui4::typing && !card_open && !in.key_pressed(VK_ESCAPE);
    bool table = table_page;
    if (keys_free && table) {
        if (in.key_pressed(VK_OEM_2) || in.key_pressed(VK_DIVIDE))
            ui4::typing = true;
        if (in.key_pressed('S') || in.pad_hit(ov::pad::x))
            ui4::sort_next = true;
        if (in.key_pressed('F') || (in.pad_hit(ov::pad::y) && !in.pad_held(ov::pad::back)))
            ui4::filter_next = true;
    }
    if (keys_free && tab == P_GRID && d.finalised && d.grid_reveal && in.key_pressed('R')) {
        ui4::reveal_on = true; // paint() closes the hub and shows the grid
        ui4::reveal_manual = true;
        ui4::reveal_at = Clock::now();
    }

    // tabs: Q/E, Left/Right, Tab, LB/RB
    int old_tab = tab;
    if (keys_free && (in.key_pressed(VK_RIGHT) || in.key_pressed('E') || in.pad_hit(ov::pad::rb) || in.pad_hit(ov::pad::right) ||
                      (in.key_pressed(VK_TAB) && !in.key_down(VK_SHIFT))))
        tab = (tab + 1) % kPageCount;
    if (keys_free && (in.key_pressed(VK_LEFT) || in.key_pressed('Q') || in.pad_hit(ov::pad::lb) || in.pad_hit(ov::pad::left) ||
                      (in.key_pressed(VK_TAB) && in.key_down(VK_SHIFT))))
        tab = (tab + kPageCount - 1) % kPageCount;

    if (tab != old_tab) {
        // the new page slides in from the side it lies on (Q/E wrap round the ends)
        int fwd = (tab - old_tab + kPageCount) % kPageCount;
        tab_dir = fwd <= kPageCount / 2 ? 1 : -1;
    }

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
        const char *status = d.finalised  ? T("PARRILLA CERRADA", "GRID CONFIRMED")
                             : d.real     ? T("TEMPORADA REAL", "THE REAL SEASON")
                             : d.deadline ? "DEADLINE DAY"
                                          : T("MERCADO ABIERTO", "TRANSFER WINDOW OPEN");
        char sub[48];
        if (d.deadline && !d.finalised)
            std::snprintf(sub, sizeof sub, "%s", d.deadline == 1 ? T("ÚLTIMA CARRERA", "LAST RACE") : T("QUEDAN 2 CARRERAS", "2 RACES LEFT"));
        else
            std::snprintf(sub, sizeof sub, "%s %d", d.real ? T("LA HISTORIA HACIA", "THE STORY TO") : T("FICHAJES PARA", "MOVES FOR"),
                          d.year + 1);
        ov::TextStyle ss_ = ui::ts(13.5f * k, ui::kText, ov::align::right | ov::align::top, 1.8f * k);
        float sw_ = std::max(ui::text_w(p, ss_, status), 150 * k);
        float bx = lx - 26 * k - sw_ - 40 * k, bw = sw_ + 40 * k + 14 * k;
        ov::rect_rounded(p, bx, py + 24 * k, bw, 52 * k, 10 * k, 0xFFFFFF08);
        ov::rect_rounded_outline(p, bx, py + 24 * k, bw, 52 * k, 10 * k, 1 * k, 0xFFFFFF14);
        uint32_t scol = d.finalised ? ui::kGold : d.deadline ? ui::kRed : ui::kGreen;
        if (d.deadline && !d.finalised) {
            // deadline day: the chip burns red and pulses
            ov::rect_rounded(p, bx, py + 24 * k, bw, 52 * k, 10 * k, ui::alpha(0xE1060000 | 0x30, 0.5f + 0.5f * ui::pulse(1.1f)));
            ov::rect_rounded_outline(p, bx, py + 24 * k, bw, 52 * k, 10 * k, 1.2f * k, 0xE10600C0);
            ui::glow(p, bx + bw / 2, py + 50 * k, bw * 0.7f, ui::alpha(0xE1060060, 0.6f + 0.4f * ui::pulse(1.1f)));
        }
        if (d.finalised)
            ov::circle(p, bx + 22 * k, py + 50 * k, 5 * k, scol);
        else
            ui::live_dot(p, bx + 22 * k, py + 50 * k, 5 * k, scol);
        ov::text(p, bx + bw - 14 * k, py + 33 * k, ss_, status);
        ov::text(p, bx + bw - 14 * k, py + 55 * k, ui::ts(11.5f * k, ui::kMuted, ov::align::right | ov::align::top, 1.8f * k), sub);
        // the season bar, between the wordmark and the window's status, when there's room for it
        float sbx = px + 560 * k, sbw = bx - 40 * k - sbx;
        if (sbw >= 260 * k)
            draw_season_bar(p, d, sbx, py + 30 * k, std::min(sbw, 700 * k), k);
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
        hint({T("Arriba", "Up"), T("Abajo", "Down")}, ui4::rows_last > 0 ? T("elegir", "pick") : T("desplazar", "scroll"));
        if (tab == P_PRESS && d.press_pending)
            hint({"1", "2", "3"}, T("responder", "answer"));
        if (table_page)
            hint({"/", "S", "F"}, T("buscar, ordenar, filtrar", "search, sort, filter"));
        else
            hint({"/", "Y"}, T("buscar a cualquiera", "find anyone"));
        hint({"Enter", "A"}, T("ficha", "details"));
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
    // the items' spacing: 44 px, closer together when the pages don't fit the panel's height (17 pages at 1080p)
    constexpr int kGroupCount = static_cast<int>(std::size(kGroups));
    float heads = kGroupCount * 22 * k + (kGroupCount - 1) * 10 * k;
    float step = std::clamp((fy - 12 * k - menu_top - heads) / kPageCount, 30 * k, 44 * k);
    {
        float y = menu_top;
        int g = 0;
        for (int i = 0; i < kPageCount; ++i) {
            if (g < kGroupCount && kGroups[g].first == i) {
                y += (g ? 10 * k : 0);
                ov::text(p, px + 30 * k, y + 8 * k, ui::ts(11 * k, ui::kMuted, ov::align::left | ov::align::middle, 2.4f * k),
                         T(kGroups[g].es, kGroups[g].en));
                y += 22 * k;
                ++g;
            }
            item_y[i] = y;
            y += step;
        }
    }
    float ix = px + 14 * k, iw = sw - 28 * k, ih = step - 4 * k;
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
        bool hov = in.inside(ix, iy, iw, ih) && !card_open;
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
        if (i == P_FUTURE) { // the teams keen on the player
            int keen = static_cast<int>(std::count_if(d.future.suitors.begin(), d.future.suitors.end(),
                                                      [](const UiSuitor &s) { return s.status == "keen"; }));
            if (keen > 0)
                badge = std::to_string(keen), bcol = 0x1F9D45FF;
        }
        if (i == P_STANDINGS) // the player's place in the championship
            for (size_t r = 0; r < d.standings.size(); ++r)
                if (d.standings[r].player) {
                    badge = "P" + std::to_string(r + 1), bcol = 0x3A3D4AFF;
                    break;
                }
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
        // each page keeps where it was scrolled to
        if (old_tab >= 0 && old_tab < static_cast<int>(page_scroll.size()))
            page_scroll[static_cast<size_t>(old_tab)] = scroll_target;
        scroll = scroll_target = page_scroll[static_cast<size_t>(tab)];
        tab_at = Clock::now();
        press_sel = 0;
        dragging = false;
        ui4::reset_page();
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
    bool picking = keys_free && ui4::rows_last > 0; // Up/Down pick a row on a page that has them
    if (picking) {
        int last = static_cast<int>(ui4::rows_last) - 1;
        if (in.key_typed(VK_DOWN) || in.pad_hit(ov::pad::down)) {
            ui4::sel = std::min(ui4::sel + 1, last);
            ui4::sel_moved = true;
        }
        if (in.key_typed(VK_UP) || in.pad_hit(ov::pad::up)) {
            ui4::sel = std::max(ui4::sel - 1, 0);
            ui4::sel_moved = true;
            if (ui4::sel == 0)
                scroll_target = 0;
        }
        if ((in.key_pressed(VK_RETURN) || in.pad_hit(ov::pad::a)) && ui4::sel >= 0)
            ui4::sel_open = true;
    }
    if (!pressing && !picking && !card_open && !ui4::typing) {
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
    if (!card_open) {
        scroll_target -= in.wheel * 80 * k;
        // the right stick scrolls, faster the further it's pushed
        if (in.pad_connected && std::fabs(in.pad_ry) > 0.2f)
            scroll_target -= in.pad_ry * std::fabs(in.pad_ry) * 1600 * k * dt;
    }
    // the scroll bar: hold it with the mouse and drag
    if (content_h > area.h + 1 && !card_open) {
        float track_x = px + pw - 18 * k, frac = area.h / content_h, th = std::max(40 * k, area.h * frac);
        if (in.clicked() && in.inside(track_x - 10 * k, area.y, 24 * k, area.h))
            dragging = true;
        if (!in.held())
            dragging = false;
        if (dragging) {
            float f = std::clamp((in.y - area.y - th / 2) / std::max(1.0f, area.h - th), 0.0f, 1.0f);
            scroll_target = scroll = f * (content_h - area.h);
        }
    } else {
        dragging = false;
    }
    scroll_target = std::clamp(scroll_target, 0.0f, std::max(0.0f, content_h - area.h));
    scroll += (scroll_target - scroll) * std::min(1.0f, dt * 14);
    if (std::fabs(scroll_target - scroll) < 0.5f)
        scroll = scroll_target;

    {
        // a little room around the area so shadows and glows aren't cut flat at the sides
        ov::ClipScope clip(p, area.x - 12 * k, area.y, area.w + 24 * k, area.h);
        static const ov::Input no_input; // the page under an open card doesn't react
        Ctx c{p, card_open ? no_input : in, area, k, d.es, t_tab};
        float y0 = area.y - scroll;
        float h = 0;
        ui4::rows.clear();
        // a new page slides in from its side
        float slide = ui::g_lite ? 0.0f : (1 - ui::ease_out(t_tab / 0.3f)) * 48 * k * static_cast<float>(tab_dir);
        if (slide != 0)
            ov::push_translate(p, slide, 0);
        switch (tab) {
        case P_PADDOCK: h = page_paddock(c, y0); break;
        case P_NEWS: h = page_news(c, y0); break;
        case P_PRESS: h = page_press(c, y0, in); break;
        case P_FUTURE: h = page_future(c, y0); break;
        case P_RUMOURS: h = page_rumours(c, y0); break;
        case P_DEALS: h = page_deals(c, y0); break;
        case P_TRANSFERS: h = page_transfers(c, y0); break;
        case P_GRID: h = page_grid(c, y0); break;
        case P_STANDINGS: h = page_standings(c, y0); break;
        case P_DRIVERS: h = page_drivers(c, y0, false); break;
        case P_CONTRACTS: h = page_contracts(c, y0); break;
        case P_STOCK: h = page_stock(c, y0); break;
        case P_F2: h = page_f2(c, y0); break;
        case P_SCOUT: h = page_scout(c, y0); break;
        case P_STAFF: h = page_staff(c, y0); break;
        case P_FREE: h = page_drivers(c, y0, true); break;
        case P_HISTORY: h = page_history(c, y0); break;
        default: break;
        }
        if (slide != 0)
            ov::pop_transform(p);
        content_h = h + 16 * k;
        // the picked row stays in view
        ui4::rows_last = ui4::rows.size();
        if (!card_open)
            ui4::rows_prev = ui4::rows; // under an open card the page still lists its rows, but the card's list stays
        if (ui4::sel >= static_cast<int>(ui4::rows_last))
            ui4::sel = static_cast<int>(ui4::rows_last) - 1;
        if (ui4::sel_moved && ui4::sel >= 0) {
            const ui4::Row &row = ui4::rows[static_cast<size_t>(ui4::sel)];
            if (row.y < area.y + 12 * k)
                scroll_target -= area.y + 12 * k - row.y;
            else if (row.y + row.h > area.y + area.h - 12 * k)
                scroll_target += row.y + row.h - (area.y + area.h - 12 * k);
        }
        ui4::sel_moved = false;
        ui4::sel_open = false;
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
    draw_card(p, in, k, W, H, d.es);
}

// The overlays outside the panel and their manager (paint)
#include "overlays.inl"
