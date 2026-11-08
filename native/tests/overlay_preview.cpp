// Draws the F7 market hub and the breaking-news banner on the host, into SVG files, with a market the
// engine plays for a few rounds. The core's overlay is replaced by a small SVG writer, so the layout can be
// checked without the game. Text widths are an estimate of the game's font (Titillium Web SemiBold).
//
//   overlay_preview.exe <data/baseline.json> <out dir> [en|es] [rounds]
#define SILLY_SEASON_PREVIEW 1
#include "../src/plugin.cpp"

#include <cstdarg>
#include <filesystem>

namespace preview {

std::string out;
int next_id = 0;

std::string col(uint32_t rgba, const char *what)
{
    char b[96];
    std::snprintf(b, sizeof b, "%s=\"#%06X\" %s-opacity=\"%.3f\"", what, rgba >> 8, what, (rgba & 0xFF) / 255.0);
    return b;
}

float char_w(uint32_t c)
{
    if (c == ' ')
        return 0.24f;
    if (c == 'i' || c == 'l' || c == 'j' || c == '.' || c == ',' || c == ':' || c == ';' || c == '\'' || c == '!' || c == '|')
        return 0.24f;
    if (c == 'I')
        return 0.27f;
    if (c == 'm' || c == 'w')
        return 0.78f;
    if (c == 'M' || c == 'W')
        return 0.85f;
    if (c >= 'A' && c <= 'Z')
        return 0.62f;
    if (c >= '0' && c <= '9')
        return 0.53f;
    if (c == 'f' || c == 't' || c == 'r')
        return 0.36f;
    return 0.5f;
}

std::vector<uint32_t> codepoints(std::string_view s)
{
    std::vector<uint32_t> cps;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n > 1) {
            cp = c & (0x3F >> (n - 1));
            for (int k = 1; k < n && i + k < s.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        cps.push_back(cp);
        i += n;
    }
    return cps;
}

float line_w(std::string_view s, float px, float tracking)
{
    float w = 0;
    auto cps = codepoints(s);
    for (uint32_t c : cps)
        w += char_w(c >= 0xC0 && c < 0x180 ? 'a' : c) * px + tracking;
    return w;
}

std::vector<std::string> wrap(std::string_view s, float px, float tracking, float max_w)
{
    std::vector<std::string> lines;
    size_t at = 0;
    while (at <= s.size()) {
        size_t nl = s.find('\n', at);
        std::string_view para = s.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        if (max_w <= 0) {
            lines.emplace_back(para);
        } else {
            std::string line;
            size_t p = 0;
            while (p <= para.size()) {
                size_t sp = para.find(' ', p);
                std::string_view word = para.substr(p, sp == std::string_view::npos ? std::string_view::npos : sp - p);
                std::string trial = line.empty() ? std::string(word) : line + " " + std::string(word);
                if (!line.empty() && line_w(trial, px, tracking) > max_w) {
                    lines.push_back(line);
                    line = std::string(word);
                } else {
                    line = trial;
                }
                if (sp == std::string_view::npos)
                    break;
                p = sp + 1;
            }
            lines.push_back(line);
        }
        if (nl == std::string_view::npos)
            break;
        at = nl + 1;
    }
    return lines;
}

std::string esc(std::string_view s)
{
    std::string o;
    for (char c : s)
        o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

float W = 1920, H = 1080;

int available() { return 1; }
int on_paint(SwoftyPaintFn, void *, uint32_t *) { return SWOFTY_ASSET_OK; }
int remove_(uint32_t) { return SWOFTY_ASSET_OK; }
void size(const SwoftyPaint *, float *w, float *h)
{
    *w = W;
    *h = H;
}
void rect(SwoftyPaint *, float x, float y, float w, float h, uint32_t c)
{
    char b[160];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" ", x, y, w, h);
    out += b + col(c, "fill") + "/>\n";
}
void rect_outline(SwoftyPaint *, float x, float y, float w, float h, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", x + t / 2,
                  y + t / 2, w - t, h - t, t);
    out += b + col(c, "stroke") + "/>\n";
}
void line(SwoftyPaint *, float x0, float y0, float x1, float y1, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke-width=\"%.1f\" stroke-linecap=\"round\" ", x0, y0,
                  x1, y1, t);
    out += b + col(c, "stroke") + "/>\n";
}
void measure_styled(const SwoftyPaint *, const SwoftyTextStyle *s, const char *t, float *w, float *h)
{
    auto lines = wrap(t, s->px, s->tracking, s->max_width);
    float mw = 0;
    for (auto &l : lines)
        mw = std::max(mw, line_w(l, s->px, s->tracking));
    *w = mw;
    *h = s->px + (lines.size() - 1) * s->px * s->line_height;
}
void text_styled(SwoftyPaint *, float x, float y, const SwoftyTextStyle *s, const char *t)
{
    auto lines = wrap(t, s->px, s->tracking, s->max_width);
    float h = s->px + (lines.size() - 1) * s->px * s->line_height;
    uint32_t ha = s->align & 0x0f, va = s->align & 0xf0;
    float top = va == ov::align::middle ? y - h / 2 : va == ov::align::bottom ? y - h : va == ov::align::baseline ? y - s->px * 0.78f : y;
    const char *anchor = ha == ov::align::center ? "middle" : ha == ov::align::right ? "end" : "start";
    for (size_t i = 0; i < lines.size(); ++i) {
        char b[260];
        std::snprintf(b, sizeof b,
                      "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%.1f\" letter-spacing=\"%.2f\" text-anchor=\"%s\" ", x,
                      top + i * s->px * s->line_height + s->px * 0.80f, s->px, s->tracking, anchor);
        std::string shadow;
        if (s->shadow_rgba & 0xFF) {
            char f[200];
            std::snprintf(f, sizeof f, " style=\"filter: drop-shadow(%.1fpx %.1fpx %.1fpx rgba(%u,%u,%u,%.2f))\"", s->shadow_dx, s->shadow_dy,
                          s->shadow_blur / 2, s->shadow_rgba >> 24, (s->shadow_rgba >> 16) & 0xFF, (s->shadow_rgba >> 8) & 0xFF,
                          (s->shadow_rgba & 0xFF) / 255.0);
            shadow = f;
        }
        out += b + col(s->rgba, "fill") + shadow + ">" + esc(lines[i]) + "</text>\n";
    }
}
void text(SwoftyPaint *p, float x, float y, float px, uint32_t c, const char *t)
{
    SwoftyTextStyle s{};
    s.px = px;
    s.rgba = c;
    s.line_height = 1.2f;
    text_styled(p, x, y, &s, t);
}
void measure(const SwoftyPaint *p, float px, const char *t, float *w, float *h)
{
    SwoftyTextStyle s{};
    s.px = px;
    s.line_height = 1.2f;
    measure_styled(p, &s, t, w, h);
}
// the addon's two textures, drawn as their SVG look-alikes: the glow as a radial gradient, the shadow as a blur
int load_image(const char *id, const char *)
{
    std::string_view i = id;
    return i == "ss_glow" || i == "ss_shadow" ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}
int image(SwoftyPaint *, const char *id, float x, float y, float w, float h, uint32_t c)
{
    if (std::string_view(id) != "ss_glow")
        return SWOFTY_ASSET_INVALID;
    int n = next_id++;
    char b[600];
    std::snprintf(b, sizeof b,
                  "<defs><radialGradient id=\"r%d\"><stop offset=\"0\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"0.35\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"1\" stop-color=\"#%06X\" stop-opacity=\"0\"/></radialGradient></defs>"
                  "<ellipse cx=\"%.1f\" cy=\"%.1f\" rx=\"%.1f\" ry=\"%.1f\" fill=\"url(#r%d)\"/>\n",
                  n, c >> 8, (c & 0xFF) / 255.0, c >> 8, (c & 0xFF) / 255.0 * 0.4, c >> 8, x + w / 2, y + h / 2, w / 2, h / 2, n);
    out += b;
    return SWOFTY_ASSET_OK;
}
void rect_rounded(SwoftyPaint *, float x, float y, float w, float h, float r, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" ", x, y, w, h,
                  std::min(r, std::min(w, h) / 2));
    out += b + col(c, "fill") + "/>\n";
}
void rect_rounded_outline(SwoftyPaint *, float x, float y, float w, float h, float r, float t, uint32_t c)
{
    char b[220];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ",
                  x + t / 2, y + t / 2, w - t, h - t, std::max(0.0f, r - t / 2), t);
    out += b + col(c, "stroke") + "/>\n";
}
void rect_gradient(SwoftyPaint *, float x, float y, float w, float h, float r, uint32_t from, uint32_t to, uint32_t flags)
{
    int id = next_id++;
    bool hz = flags == SWOFTY_GRADIENT_HORIZONTAL;
    char b[400];
    std::snprintf(b, sizeof b,
                  "<defs><linearGradient id=\"g%d\" x1=\"0\" y1=\"0\" x2=\"%d\" y2=\"%d\"><stop offset=\"0\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"1\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/></linearGradient></defs>\n",
                  id, hz ? 1 : 0, hz ? 0 : 1, from >> 8, (from & 0xFF) / 255.0, to >> 8, (to & 0xFF) / 255.0);
    out += b;
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"url(#g%d)\"/>\n", x, y, w, h,
                  std::min(r, std::min(w, h) / 2), id);
    out += b;
}
void circle(SwoftyPaint *, float cx, float cy, float r, uint32_t c)
{
    char b[160];
    std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" ", cx, cy, r);
    out += b + col(c, "fill") + "/>\n";
}
void circle_outline(SwoftyPaint *, float cx, float cy, float r, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", cx, cy, r - t / 2, t);
    out += b + col(c, "stroke") + "/>\n";
}
void arc(SwoftyPaint *, float cx, float cy, float r, float a0, float a1, float t, uint32_t c)
{
    char b[300];
    if (a1 - a0 >= 6.2831f) {
        std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", cx, cy, r, t);
    } else {
        float x0 = cx + std::cos(a0) * r, y0 = cy + std::sin(a0) * r, x1 = cx + std::cos(a1) * r, y1 = cy + std::sin(a1) * r;
        std::snprintf(b, sizeof b, "<path d=\"M%.1f %.1f A%.1f %.1f 0 %d 1 %.1f %.1f\" fill=\"none\" stroke-width=\"%.1f\" ", x0, y0, r, r,
                      a1 - a0 > 3.14159f ? 1 : 0, x1, y1, t);
    }
    out += b + col(c, "stroke") + "/>\n";
}
void polyline(SwoftyPaint *, const float *xy, uint32_t n, float t, uint32_t c, uint32_t flags)
{
    std::string pts;
    for (uint32_t i = 0; i < n; ++i) {
        char b[48];
        std::snprintf(b, sizeof b, "%.1f,%.1f ", xy[2 * i], xy[2 * i + 1]);
        pts += b;
    }
    char b[64];
    std::snprintf(b, sizeof b, "\" fill=\"none\" stroke-width=\"%.1f\" stroke-linejoin=\"round\" ", t);
    out += std::string(flags ? "<polygon points=\"" : "<polyline points=\"") + pts + b + col(c, "stroke") + "/>\n";
}
void polygon(SwoftyPaint *, const float *xy, uint32_t n, uint32_t c)
{
    std::string pts;
    for (uint32_t i = 0; i < n; ++i) {
        char b[48];
        std::snprintf(b, sizeof b, "%.1f,%.1f ", xy[2 * i], xy[2 * i + 1]);
        pts += b;
    }
    out += "<polygon points=\"" + pts + "\" " + col(c, "fill") + "/>\n";
}
int image_uv(SwoftyPaint *, const char *, float, float, float, float, float, float, float, float, uint32_t) { return SWOFTY_ASSET_UNAVAILABLE; }
int image_nine(SwoftyPaint *, const char *id, float x, float y, float w, float h, float, float, float, float, float scale, uint32_t c)
{
    if (std::string_view(id) != "ss_shadow")
        return SWOFTY_ASSET_INVALID;
    // the texture's shape sits 40 px in from each side and is blurred by about 14 px
    int n = next_id++;
    float m = 40 * scale, blur = 7 * scale;
    char b[460];
    std::snprintf(b, sizeof b,
                  "<defs><filter id=\"f%d\" x=\"-50%%\" y=\"-50%%\" width=\"200%%\" height=\"200%%\"><feGaussianBlur stdDeviation=\"%.1f\"/></filter></defs>"
                  "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" filter=\"url(#f%d)\" ",
                  n, blur, x + m, y + m, std::max(1.0f, w - 2 * m), std::max(1.0f, h - 2 * m), 14 * scale, n);
    out += b + col(c, "fill") + "/>\n";
    return SWOFTY_ASSET_OK;
}
int image_size(const char *, float *, float *) { return SWOFTY_ASSET_UNAVAILABLE; }
void push_clip(SwoftyPaint *, float x, float y, float w, float h)
{
    int id = next_id++;
    char b[240];
    std::snprintf(b, sizeof b, "<defs><clipPath id=\"c%d\"><rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/></clipPath></defs><g clip-path=\"url(#c%d)\">\n",
                  id, x, y, w, h, id);
    out += b;
}
void pop_clip(SwoftyPaint *) { out += "</g>\n"; }
void push_transform(SwoftyPaint *, const float *m)
{
    char b[160];
    std::snprintf(b, sizeof b, "<g transform=\"matrix(%f %f %f %f %f %f)\">\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    out += b;
}
void pop_transform(SwoftyPaint *) { out += "</g>\n"; }
void push_opacity(SwoftyPaint *, float o)
{
    char b[64];
    std::snprintf(b, sizeof b, "<g opacity=\"%.3f\">\n", o);
    out += b;
}
void pop_opacity(SwoftyPaint *) { out += "</g>\n"; }
void safe_area(const SwoftyPaint *, float *x, float *y, float *w, float *h)
{
    *x = std::round(W * 0.035f);
    *y = std::round(H * 0.035f);
    *w = W - 2 * *x;
    *h = H - 2 * *y;
}
float ui_scale(const SwoftyPaint *) { return std::min(W / 1920, H / 1080); }
float mouse_x = -1, mouse_y = -1;
int input(const SwoftyPaint *, SwoftyInput *o)
{
    o->x = mouse_x;
    o->y = mouse_y;
    return SWOFTY_ASSET_OK;
}
int focus(int) { return SWOFTY_ASSET_OK; }

const SwoftyOverlay table = {available, on_paint, remove_, size, rect, rect_outline, line, text, measure, load_image, image,
                             rect_rounded, rect_rounded_outline, rect_gradient, circle, circle_outline, arc, polyline, polygon,
                             text_styled, measure_styled, image_uv, image_nine, image_size, push_clip, pop_clip, push_transform,
                             pop_transform, push_opacity, pop_opacity, safe_area, ui_scale, input, focus};

void log_(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}
int no_setting(const char *, uint32_t, SwoftySettingValue *) { return SWOFTY_ASSET_UNAVAILABLE; }

void begin()
{
    out.clear();
    char b[400];
    std::snprintf(b, sizeof b,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\" "
                  "font-family=\"'Titillium Web', sans-serif\" font-weight=\"600\">\n"
                  "<style>@import url('https://fonts.googleapis.com/css2?family=Titillium+Web:wght@600&amp;display=block');</style>\n",
                  W, H, W, H);
    out += b;
    // a stand-in for the game behind the overlay: Team HQ's dark red menus
    out += "<defs><linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"0\" stop-color=\"#3a0d18\"/>"
           "<stop offset=\"0.5\" stop-color=\"#140a12\"/><stop offset=\"1\" stop-color=\"#5a1020\"/></linearGradient></defs>"
           "<rect width=\"100%\" height=\"100%\" fill=\"url(#bg)\"/>\n"
           "<text x=\"120\" y=\"240\" font-size=\"120\" fill=\"#ffffff\" fill-opacity=\"0.10\">98 VAL</text>\n";
}

void save(const std::filesystem::path &file)
{
    out += "</svg>\n";
    std::ofstream(file, std::ios::binary) << out;
}

} // namespace preview

static void race(ss::State &st, uint64_t &seed)
{
    auto rnd = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(seed >> 11) / 9007199254740992.0;
    };
    std::vector<std::pair<double, std::string>> order;
    for (const ss::Team &t : st.teams)
        for (const std::string &who : t.seats)
            if (const ss::Driver *d = st.driver(who))
                order.push_back({t.performance * 0.7 + d->overall() * 0.5 + rnd() * 18.0, who});
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); ++i)
        ss::record_result(st, order[i].second, static_cast<int>(i + 1));
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("overlay_preview <baseline.json> <out dir> [en|es] [rounds]\n");
        return 2;
    }
    // the overlay's v2/v3 calls ask the core module for its overlay version
    if (!LoadLibraryW(L"swofty_core.dll")) {
        std::printf("swofty_core.dll (the preview's stand-in) is missing next to the exe\n");
        return 1;
    }
    static SwoftyApi api{};
    api.version = SWOFTY_API_VERSION;
    api.log = preview::log_;
    api.setting = preview::no_setting;
    api.overlay = &preview::table;
    swofty::api = &api;
    ui::load_textures();

    std::string lang = argc > 3 ? argv[3] : "en";
    int rounds = argc > 4 ? std::atoi(argv[4]) : 6;
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss_;
    ss_ << f.rdbuf();
    json::Error err;
    json::Value baseline = json::parse(ss_.str(), &err);
    ss::Settings s;
    s.lang = lang;
    s.rumours = ss::Rumours::many;
    s.intensity = ss::Intensity::lively;
    if (!ss::init(g_state, baseline, "f1_career_2025", 20251106, nullptr)) {
        std::printf("init failed\n");
        return 1;
    }
    g_state.career = "preview";
    g_state.player_name = lang == "en" ? "You" : "Tú";
    g_loaded = true;
    // the player in a Williams, like a Driver Career start
    {
        std::vector<ss::GridCar> cars;
        for (const ss::Team &t : g_state.teams)
            for (int k = 0; k < 2; ++k)
                cars.push_back({t.id == "williams" && k == 1 ? std::string("player") : t.seats[static_cast<size_t>(k)], t.id,
                                t.id == "williams" && k == 1});
        ss::sync_grid(g_state, cars);
    }
    ss::season_preview(g_state, s);
    uint64_t seed = 7;
    for (int r = 1; r <= rounds; ++r) {
        race(g_state, seed);
        ss::complete_round(g_state, s, r, 24);
    }
    g_state.seen_news = g_state.next_news_id - 4; // a few unread
    // a save round trip, then the chosen language: what the plugin does after loading a career
    ss::from_json(g_state, ss::to_json(g_state));
    g_state.player_name = lang == "en" ? "You" : "Tú";
    ss::localize(g_state, lang != "en");
    update_overlay(s);
    std::printf("news %zu, rumours %zu, deals %zu, press %s\n", g_ov.news.size(), g_ov.rumours.size(), g_ov.deals.size(),
                g_ov.press_pending ? "yes" : "no");

    std::filesystem::path dir = argv[2];
    std::filesystem::create_directories(dir);
    // every text the player can read, for a language check
    {
        ss::refresh_mails(g_state, s);
        std::ofstream t(dir / "texts.txt", std::ios::binary);
        for (const ss::NewsItem &n : g_state.news)
            t << n.title << "\n" << n.body << "\n\n";
        if (const ss::State::Press *q = ss::press_pending(g_state)) {
            t << q->question << "\n";
            for (const std::string &a : q->answers)
                t << a << "\n";
        }
        for (const ss::State::Mail *m : {&g_state.mail_preview, &g_state.mail_radar, &g_state.mail_grid})
            t << m->subject << "\n" << m->body << "\n\n";
        t << ss::report_rumours(g_state, s) << ss::report_deals(g_state, s) << ss::report_contracts(g_state, s)
          << ss::report_grid(g_state, s) << ss::report_history(g_state, s) << ss::report_driver_market(g_state, s)
          << ss::report_news(g_state, s) << ss::report_stock(g_state, s) << ss::report_f2(g_state, s)
          << ss::report_staff(g_state, s);
    }
    using namespace paint_state;
    auto past = Clock::now() - std::chrono::seconds(2);
    ov::Input in;
    for (int i = 0; i < kPageCount; ++i) {
        preview::begin();
        tab = i;
        menu_y = -1;
        scroll = scroll_target = 0;
        opened_at = tab_at = past;
        preview::mouse_x = i == P_RUMOURS ? 900.0f : -1;
        preview::mouse_y = i == P_RUMOURS ? 380.0f : -1;
        draw_board(reinterpret_cast<ov::Paint *>(1), in, 0.016f);
        char name[32];
        std::snprintf(name, sizeof name, "hub_%02d.svg", i);
        preview::save(dir / name);
    }
    // the banner, two seconds in
    preview::begin();
    ss::NewsItem n;
    n.kind = "official";
    n.title = lang == "en" ? "OFFICIAL: NEW SIGNING" : "OFICIAL: NUEVO FICHAJE";
    n.body = lang == "en" ? "Carlos Sainz joins Ferrari from Williams on a deal until 2027. The team calls it \"the missing piece\"."
                          : "Carlos Sainz ficha por Ferrari desde Williams con contrato hasta 2027. El equipo lo llama \"la pieza que faltaba\".";
    g_ov.banners.push_back(n);
    banner_on = false;
    draw_banner(reinterpret_cast<ov::Paint *>(1));
    banner_at = Clock::now() - std::chrono::milliseconds(2500);
    preview::out.clear();
    preview::begin();
    draw_banner(reinterpret_cast<ov::Paint *>(1));
    preview::save(dir / "banner.svg");
    std::printf("wrote %d hub pages and the banner to %s\n", kPageCount, dir.string().c_str());
    return 0;
}
