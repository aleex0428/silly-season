// The overlays outside the F7 panel: the breaking-news banner, the bombshell signing, the farewell card, deadline
// day's strip, the report after a race, the press question at Team HQ, the alerts of teams keen on the player, the
// awards ceremony, the season's cover, the grid reveal and the Team HQ card. One manager shows them in turn by
// priority, never two at once and never during a session. Included by hub.inl, after the panel; paint thread only.

namespace ovl {
using Clock = std::chrono::steady_clock;

// what is on screen now (the full-screen ones take the keys; the others leave them to the game)
enum class Show { none, awards, cover, press, signing, farewell, strip, report, banner };
Show current = Show::none;
Clock::time_point at;

UiFarewell farewell;
UiStrip strip;
UiAlert alert;
bool alert_on = false;
Clock::time_point alert_at;
int press_shown = 0;   // the press question already shown on screen (its id)
int report_shown = 0;  // the race report already shown (its id)
int awards_local = 0;  // the season whose ceremony this session showed (until the flow thread saves it)
int cover_local = 0;
int press_answered = -1; // the answer just picked on screen (shown for a moment)
Clock::time_point press_answered_at;

float secs(Clock::time_point t) { return std::chrono::duration<float>(Clock::now() - t).count(); }

// "Max Verstappen" -> "MV"
std::string initials(const std::string &name)
{
    std::string out;
    bool start = true;
    for (char ch : name) {
        if (ch == ' ') {
            start = true;
            continue;
        }
        if (start && static_cast<unsigned char>(ch) < 0x80 && std::isalpha(static_cast<unsigned char>(ch)))
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        start = false;
    }
    if (out.size() > 2)
        out = std::string(1, out.front()) + out.back();
    return out;
}

// the team of a story (by id), from this season's teams
const UiTeam *team_by_id(const std::string &id)
{
    if (id.empty())
        return nullptr;
    for (const UiTeam &t : g_ov.teams)
        if (t.id == id)
            return &t;
    return nullptr;
}

ui::Icon kind_icon(const std::string &k)
{
    if (k == "official" || k == "talks") return ui::Icon::deals;
    if (k == "retirement" || k == "farewell") return ui::Icon::history;
    if (k == "sacking" || k == "standin" || k == "standin_return") return ui::Icon::drivers;
    if (k == "press" || k == "press_reply") return ui::Icon::press;
    if (k == "principal" || k == "staff") return ui::Icon::staff;
    if (k == "swap" || k == "domino") return ui::Icon::chain;
    if (k == "sponsor") return ui::Icon::stock;
    if (k == "academy" || k == "talents" || k == "scout") return ui::Icon::scout;
    if (k == "reserve") return ui::Icon::free;
    if (k == "f2") return ui::Icon::f2;
    if (k == "media" || k == "awards") return ui::Icon::news;
    return ui::Icon::rumours;
}

// sparks bursting out of a point: n of them, t seconds after the burst
void sparks(ov::Paint *p, float cx, float cy, float t, float k, uint32_t col, int n = 18, float reach = 220)
{
    if (ui::g_lite || t < 0 || t > 1.1f)
        return;
    float e = ui::ease_out(t / 1.1f);
    for (int i = 0; i < n; ++i) {
        float a = 6.2832f * static_cast<float>(i) / static_cast<float>(n) + 0.37f * static_cast<float>(i % 3);
        float len = reach * k * (0.55f + 0.45f * static_cast<float>((i * 7) % 5) / 4.0f);
        float x0 = cx + std::cos(a) * len * e * 0.75f, y0 = cy + std::sin(a) * len * e * 0.75f;
        float x1 = cx + std::cos(a) * len * e, y1 = cy + std::sin(a) * len * e;
        ov::line(p, x0, y0, x1, y1, 2.2f * k, ui::alpha(col, 1 - e));
    }
}

// the line along the bottom of an overlay that runs out with its time
void time_left(ov::Paint *p, float x, float y, float w, float k, float left, uint32_t col)
{
    ov::rect(p, x, y, w, 3 * k, 0xFFFFFF10);
    ov::gradient(p, x, y, w * std::clamp(left, 0.0f, 1.0f), 3 * k, ui::alpha(col, 0.3f), col, true);
}

// a full-screen backdrop: the game dims, a light from the top, the weave
void backdrop(ov::Paint *p, float W, float H, float k, float a, uint32_t light)
{
    ov::rect(p, 0, 0, W, H, ui::alpha(0x05060BF2, a));
    ui::glow(p, W * 0.5f, H * 0.05f, 1100 * k, ui::alpha(light, a));
    ui::hatch(p, 0, 0, W, H, k, ui::alpha(0xFFFFFF04, a), 14 * k);
}

// the "skip" keys of a full-screen overlay
bool skip_pressed(const ov::Input &in)
{
    return in.key_pressed(VK_ESCAPE) || in.key_pressed(VK_RETURN) || in.pad_hit(ov::pad::a) || in.pad_hit(ov::pad::b) || in.clicked();
}
} // namespace ovl

// ---------------------------------------------------------------------------------------------------
// The breaking-news banner: the brand block (or the driver's initials in his team's colour), the red label with
// the story's icon, the white strip, then the story over the team's livery

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
    size_t waiting = g_ov.banners.size();
    // more stories queued: each one stays a little less
    const float in_s = 0.8f, hold_s = waiting >= 2 ? 4.5f : waiting == 1 ? 6.0f : 7.5f, out_s = 0.5f;
    if (t > in_s + hold_s + out_s) {
        banner_on = false;
        return;
    }
    bool es = g_ov.es;
    float out = t > in_s + hold_s ? ui::ease_out((t - in_s - hold_s) / out_s) : 0;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = std::min(1100 * k, safe.w), pad = 22 * k;
    bool press = banner.kind == "press";
    ui::Tag kind = ui::news_tag(banner.kind);
    uint32_t accent = press ? ui::kAmber : kind.col;
    bool hard = !press && (banner.kind == "official" || banner.kind == "retirement" || banner.kind == "sacking" || banner.kind == "collapse");
    const UiTeam *team = ovl::team_by_id(!banner.to.empty() ? banner.to : banner.from);
    const UiDriverCard *who = banner.driver.empty() ? nullptr : g_ov.card(banner.driver);
    std::string text = banner.body;
    if (press)
        text += es ? "   (F7 para responder)" : "   (F7 to answer)";
    float logo_w = 104 * k;
    ov::TextStyle body = ui::ts(19 * k, 0xDDDFE6FF, ov::align::left | ov::align::top, 0, w - logo_w - 2 * pad - 12 * k);
    body.line_height = 1.3f;
    ov::TextStyle title = ui::lifted(ui::ts(26 * k, 0xFFFFFFFF, ov::align::left | ov::align::top, 0.4f * k, w - logo_w - 2 * pad - 60 * k), k);
    float th = ui::text_h(p, title, banner.title), bh = ui::text_h(p, body, text);
    float tag_h = 42 * k, h = tag_h + 16 * k + th + 10 * k + bh + pad + 6 * k;
    float x = safe.x, y = safe.y + safe.h - h - 70 * k;
    float a_logo = ui::ease_out(t / 0.3f), a_tag = ui::ease_out((t - 0.12f) / 0.3f), a_strip = ui::ease_out((t - 0.24f) / 0.3f),
          a_body = ui::ease_out((t - 0.32f) / 0.42f), a_text = ui::ease_out((t - 0.5f) / 0.3f);
    ov::OpacityScope fade(p, 1 - out);
    ov::push_translate(p, -out * 60 * k, 0);
    auto slide = [&](float a) { return (1 - a) * -40 * k; };

    // the story panel; the stories waiting behind it peek out above
    float by = y + tag_h, bhh = h - tag_h;
    if (a_body > 0 && waiting > 0) {
        ov::OpacityScope wf(p, a_body);
        for (size_t i = std::min<size_t>(waiting, 2); i >= 1; --i) {
            float gx = x + logo_w + 18 * k * static_cast<float>(i) + slide(a_body), gy = by - 9 * k * static_cast<float>(i);
            ov::rect_rounded(p, gx, gy, w - logo_w - 36 * k * static_cast<float>(i), 20 * k, 6 * k, ui::alpha(0x23252FF0, 1.0f - 0.25f * static_cast<float>(i)));
        }
        char more[32];
        std::snprintf(more, sizeof more, "+%zu %s", waiting, es ? "MÁS" : "MORE");
        ui::tag(p, x + w - 100 * k + slide(a_body), by - 30 * k, more, accent, k, 11, true);
    }
    if (a_body > 0) {
        ov::OpacityScope bf(p, a_body);
        float bx = x + slide(a_body);
        ui::glow(p, bx + w * 0.3f, y + h * 0.6f, w * 0.55f, ui::alpha(accent, 0.12f));
        ui::shadow(p, bx, by, w, bhh, k, 1.6f);
        ov::gradient(p, bx, by, w, bhh, 0x15161FF4, 0x0C0D12F4, false, 0);
        {
            ov::ClipScope clip(p, bx, by, w, bhh);
            // the team's livery along the right of the story, its code as a watermark
            if (team) {
                ui::livery(p, bx + w * 0.62f, by, w * 0.38f, bhh, team->col, k, 0.55f, 0);
                ov::gradient(p, bx + w * 0.62f, by, w * 0.2f, bhh, 0x15161FF4, 0x15161F00, true);
                ov::text(p, bx + w - 18 * k, by + bhh / 2 + 6 * k, ui::ts(84 * k, 0xFFFFFF16, ov::align::right | ov::align::middle, 3 * k), team->code);
            }
            ov::gradient(p, bx + logo_w, by, w * 0.6f, bhh, ui::alpha(accent, 0.14f), ui::alpha(accent, 0), true);
            ui::hatch(p, bx + w * 0.55f, by, w * 0.45f, bhh, k, 0xFFFFFF05, 10 * k);
        }
        ov::rect(p, bx, by, w, 3 * k, accent);
        ovl::time_left(p, bx, by + bhh - 3 * k, w, k, 1 - (t - in_s) / hold_s, accent);
        if (a_text > 0) {
            ov::OpacityScope tf(p, a_text);
            float tx = bx + logo_w + pad - (1 - a_text) * 16 * k;
            ov::text(p, tx, by + 16 * k, title, banner.title);
            ov::text(p, tx, by + 16 * k + th + 10 * k, body, text);
        }
    }
    // the block on the left: the driver's initials in his team's colour, or the brand
    if (a_logo > 0) {
        ov::OpacityScope lf(p, a_logo);
        float bx = x + slide(a_logo);
        ov::gradient(p, bx, y, logo_w, h, 0x1C1D27FF, 0x0B0C10FF, false, 0);
        ui::hatch(p, bx, y, logo_w, h, k, 0xFFFFFF08, 8 * k);
        if (who) {
            uint32_t col = team ? ui::rgb(team->col) : ui::kRed;
            float cx = bx + logo_w / 2, cy = y + h / 2 + 8 * k, r = 32 * k;
            ui::glow(p, cx, cy, r * 2.4f, ui::alpha(col, 0.45f));
            ov::circle(p, cx, cy, r, col);
            ov::circle_outline(p, cx, cy, r + 4 * k, 1.5f * k, ui::alpha(0xFFFFFFFF, 0.35f));
            float sweep = std::fmod(ui::clock_s() * 1.4f, 6.2832f);
            ov::arc(p, cx, cy, r + 4 * k, sweep, sweep + 1.1f, 2 * k, 0xFFFFFFC0);
            ov::text(p, cx, cy, ui::ts(24 * k, team ? ui::ink_on(team->col) : 0xFFFFFFFF, ov::align::center | ov::align::middle, 1 * k),
                     ovl::initials(who->name));
            ui::logo(p, bx + 12 * k, y + 12 * k, 20 * k, k);
        } else {
            ui::glow(p, bx + logo_w / 2, y + h / 2, logo_w * 0.6f, 0xE1060050);
            ui::logo(p, bx + logo_w / 2 - 34 * k, y + h / 2 - 24 * k, 48 * k, k);
        }
        ov::rect(p, bx + logo_w - 3 * k, y, 3 * k, h, team ? ui::rgb(team->col) : ui::kRed);
    }
    // the label: BREAKING NEWS (or PRESS, DEADLINE DAY), with the story's icon and a shine running over it
    bool deadline = !press && (banner.kind == "deadline" || (g_ov.deadline > 0 && !g_ov.finalised && banner.kind == "official"));
    const char *label = press ? (es ? "PRENSA" : "PRESS") : deadline ? "DEADLINE DAY" : (es ? "ÚLTIMA HORA" : "BREAKING NEWS");
    ov::TextStyle ls = ui::ts(18 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 2.0f * k);
    float lw = ui::text_w(p, ls, label) + 88 * k;
    if (a_tag > 0) {
        ov::OpacityScope tf(p, a_tag);
        float lx = x + logo_w + slide(a_tag);
        uint32_t lc = press ? 0xC77F00FF : hard ? ui::kRed : 0xC2050AFF;
        ui::para(p, lx, y, lw + 14 * k, tag_h, 14 * k, lc);
        ui::para(p, lx, y, lw + 14 * k, tag_h * 0.45f, 6 * k, 0xFFFFFF1E);
        ui::live_dot(p, lx + 24 * k, y + tag_h / 2, 4.5f * k, 0xFFFFFFFF, 1.2f);
        ui::icon(p, ovl::kind_icon(banner.kind), lx + 48 * k, y + tag_h / 2, 16 * k, 0xFFFFFFFF, k * 0.9f);
        ov::text(p, lx + 64 * k, y + tag_h / 2, ui::lifted(ls, k), label);
        float sh = std::fmod(t + 0.4f, 3.0f) / 0.9f;
        if (sh < 1) {
            float sw = 26 * k, sx = lx + 14 * k + (lw - 30 * k) * sh;
            ui::para(p, sx, y, sw, tag_h, 14 * k, 0xFFFFFF30);
        }
    }
    // the kind and the brand on a white strip next to it, the team's chip at its end
    if (a_strip > 0) {
        ov::OpacityScope sf(p, a_strip);
        ov::TextStyle ks = ui::ts(14.5f * k, 0x111218FF, ov::align::left | ov::align::middle, 1.6f * k);
        std::string kind_text = std::string("SILLY SEASON  ·  ") + (es ? kind.es : kind.en);
        float kw = ui::text_w(p, ks, kind_text) + 40 * k;
        float sx = x + logo_w + lw + 2 * k + slide(a_strip);
        ui::para(p, sx, y + 6 * k, kw + 14 * k, tag_h - 6 * k, 12 * k, 0xF2F3F7FF);
        ui::para(p, sx + kw - 2 * k, y + 6 * k, 14 * k, tag_h - 6 * k, 12 * k, accent);
        ov::text(p, sx + 22 * k, y + tag_h / 2 + 3 * k, ks, kind_text);
        if (team)
            ui::team_chip(p, sx + kw + 24 * k, y + 10 * k, team->code, team->col, k, 26);
    }
    ov::pop_transform(p);
}

// ---------------------------------------------------------------------------------------------------
// A bombshell signing: the two teams crash together from the sides, gold sparks fly, the driver's value counts up and
// the new team's boss has his say

bool draw_signing(ov::Paint *p)
{
    using namespace ui4;
    float t = std::chrono::duration<float>(Clock::now() - signing_at).count();
    constexpr float in_s = 0.7f, hold_s = 7.0f, out_s = 0.5f;
    if (t > in_s + hold_s + out_s)
        return false;
    const UiSigning &sg = signing;
    bool es = g_ov.es;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = std::min(1140 * k, safe.w - 40 * k), h = 340 * k;
    float x = safe.x + (safe.w - w) / 2, y = safe.y + safe.h - h - 90 * k;
    float out = t > in_s + hold_s ? ui::ease_out((t - in_s - hold_s) / out_s) : 0;
    float a = ui::ease_out(t / in_s);
    ov::OpacityScope fade(p, a * (1 - out));
    ui::shadow(p, x, y, w, h, k, 2.0f);
    ov::gradient(p, x, y, w, h, 0x15161FF8, 0x0B0C10F8, false, 16 * k);
    // the two liveries crash in from the sides and meet in the middle
    float meet = ui::g_lite ? 1.0f : ui::ease_back(std::min(1.0f, t / 0.55f));
    float half = w * 0.5f, off = (1 - meet) * half;
    {
        ov::ClipScope clip(p, x, y, w, h);
        if (sg.from_col)
            ov::gradient(p, x - off, y, half, h, ui::rgb(sg.from_col, 0x80), ui::rgb(sg.from_col, 0x10), true);
        ov::gradient(p, x + half + off, y, half, h, ui::rgb(sg.to_col, 0x10), ui::rgb(sg.to_col, 0xA8), true);
        ui::livery(p, x + w - 320 * k + off, y, 320 * k, h, sg.to_col, k, 0.75f, 0);
        ov::gradient(p, x + w - 320 * k + off, y, 140 * k, h, 0x15161FE0, 0x15161F00, true); // blended in, no hard edge
        // the seam where they meet: a slanted bright cut
        ui::para(p, x + half - 30 * k, y, 60 * k, h, 40 * k, ui::alpha(0xFFFFFFFF, 0.06f + 0.10f * std::max(0.0f, 1 - (t - 0.55f) / 0.8f)));
        ui::hatch(p, x, y, w, h, k, 0xFFFFFF05, 12 * k);
        // the impact: a flash
        float flash = t > 0.5f ? std::max(0.0f, 1 - (t - 0.5f) / 0.35f) : 0.0f;
        if (flash > 0 && !ui::g_lite)
            ov::rect(p, x, y, w, h, ui::alpha(0xFFFFFFFF, 0.35f * flash));
    }
    ovl::sparks(p, x + w / 2, y + h * 0.45f, t - 0.5f, k, ui::kGold, 22, 260);
    ov::rect_rounded_outline(p, x, y, w, h, 16 * k, 1.2f * k, ui::alpha(ui::kGold, 0.6f));
    ov::rect(p, x + 16 * k, y, w - 32 * k, 3 * k, ui::kGold);
    // the label
    ui::para(p, x + 30 * k, y - 18 * k, 300 * k, 38 * k, 12 * k, ui::kGold);
    ov::text(p, x + 56 * k, y + 1 * k, ui::ts(17 * k, 0x111111FF, ov::align::left | ov::align::middle, 2.2f * k), es ? "BOMBAZO" : "BOMBSHELL SIGNING");
    float ta = ui::ease_out((t - 0.45f) / 0.5f);
    ov::OpacityScope tf(p, ta);
    // the driver, big, and his value counting up on the right
    ov::text(p, x + w / 2, y + 78 * k, ui::lifted(ui::ts(56 * k, 0xFFFFFFFF, ov::align::center | ov::align::middle, 1.2f * k), k, 0x000000C0), sg.driver);
    {
        float vx = x + w - 50 * k, vy = y + 60 * k;
        char v[8];
        std::snprintf(v, sizeof v, "%.0f", sg.value * ui::ease_out((t - 0.6f) / 1.2f));
        ov::text(p, vx, vy, ui::lifted(ui::ts(46 * k, ui::kGold, ov::align::right | ov::align::middle), k), v);
        ov::text(p, vx, vy + 34 * k, ui::ts(11 * k, 0xFFFFFFC0, ov::align::right | ov::align::middle, 1.6f * k), es ? "VALOR" : "VALUE");
    }
    // from -> to
    float cy = y + 154 * k, cx = x + w / 2;
    float fw = sg.from_code.empty() ? 0 : ui::text_w(p, ui::ts(15 * k), sg.from_code) + 26 * k;
    if (!sg.from_code.empty())
        ui::team_chip(p, cx - 60 * k - fw, cy - 15 * k, sg.from_code, sg.from_col, k, 30);
    ui::flow_arrow(p, cx - 40 * k, cy, 80 * k, k, ui::kGold, 50.0f);
    ui::team_chip(p, cx + 60 * k, cy - 15 * k, sg.to_code, sg.to_col, k, 30);
    ov::text(p, cx, cy + 46 * k, ui::ts(22 * k, ui::kText, ov::align::center | ov::align::middle, 1 * k), (es ? "FICHA POR " : "SIGNS FOR ") + sg.to);
    char facts[160];
    std::snprintf(facts, sizeof facts, es ? "Contrato hasta %d   ·   %s al año   ·   %d años" : "Contract until %d   ·   %s a year   ·   %d y/o",
                  sg.until, sg.salary.c_str(), sg.age);
    ov::text(p, cx, cy + 82 * k, ui::ts(16 * k, ui::kText2, ov::align::center | ov::align::middle), facts);
    if (!sg.quote.empty())
        ov::text(p, cx, cy + 122 * k, ui::ts(15 * k, 0xFFFFFFB0, ov::align::center | ov::align::middle), ui::fit(p, ui::ts(15 * k), sg.quote, w - 120 * k));
    ovl::time_left(p, x, y + h - 3 * k, w, k, 1 - (t - in_s) / hold_s, ui::kGold);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// A farewell: a driver who retires gets a card of his own, his career counting up

bool draw_farewell(ov::Paint *p)
{
    float t = ovl::secs(ovl::at);
    constexpr float in_s = 0.7f, hold_s = 7.5f, out_s = 0.6f;
    if (t > in_s + hold_s + out_s)
        return false;
    const UiFarewell &f = ovl::farewell;
    bool es = g_ov.es;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = std::min(1000 * k, safe.w - 40 * k), h = 290 * k;
    float x = safe.x + (safe.w - w) / 2, y = safe.y + safe.h - h - 90 * k;
    float out = t > in_s + hold_s ? ui::ease_out((t - in_s - hold_s) / out_s) : 0;
    ov::OpacityScope fade(p, ui::ease_out(t / in_s) * (1 - out));
    uint32_t violet = 0xBF5AF2FF;
    ui::shadow(p, x, y, w, h, k, 2.0f);
    ov::gradient(p, x, y, w, h, 0x18141FF8, 0x0B0A10F8, false, 16 * k);
    {
        ov::ClipScope clip(p, x, y, w, h);
        if (f.col) {
            ui::livery(p, x, y, w * 0.45f, h, f.col, k, 0.6f, 0);
            ov::gradient(p, x, y, w * 0.6f, h, 0x18141F00, 0x18141FF8, true);
        }
        ui::glow(p, x + w / 2, y, 500 * k, ui::alpha(violet, 0.18f));
        // a slow light that crosses the card
        float sweep = std::fmod(t * 0.35f, 1.4f) - 0.2f;
        ui::para(p, x + w * sweep, y, 120 * k, h, 80 * k, 0xFFFFFF08);
    }
    ov::rect_rounded_outline(p, x, y, w, h, 16 * k, 1.2f * k, ui::alpha(violet, 0.55f));
    ov::rect(p, x + 16 * k, y, w - 32 * k, 3 * k, violet);
    ui::para(p, x + 30 * k, y - 18 * k, 230 * k, 38 * k, 12 * k, violet);
    ov::text(p, x + 56 * k, y + 1 * k, ui::ts(17 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 2.4f * k), es ? "DESPEDIDA" : "FAREWELL");
    float ta = ui::ease_out((t - 0.3f) / 0.6f);
    ov::OpacityScope tf(p, ta);
    ov::text(p, x + w / 2, y + 74 * k, ui::lifted(ui::ts(50 * k, 0xFFFFFFFF, ov::align::center | ov::align::middle, 1.2f * k), k), f.driver);
    char sub[96];
    std::snprintf(sub, sizeof sub, es ? "%d años  ·  %s" : "%d  ·  %s", f.age, f.team.empty() ? (es ? "su último equipo" : "his last team") : f.team.c_str());
    ov::text(p, x + w / 2, y + 116 * k, ui::ts(16 * k, ui::kText2, ov::align::center | ov::align::middle, 0.8f * k), sub);
    // the numbers of his career, counting up
    const int vals[5] = {f.seasons, f.starts, f.wins, f.podiums, f.titles};
    const char *es_l[5] = {"TEMPORADAS", "SALIDAS", "VICTORIAS", "PODIOS", "TÍTULOS"}, *en_l[5] = {"SEASONS", "STARTS", "WINS", "PODIUMS", "TITLES"};
    float gw = (w - 120 * k) / 5, gx = x + 60 * k, gy = y + 160 * k;
    float grow = ui::ease_out((t - 0.7f) / 1.6f);
    for (int i = 0; i < 5; ++i) {
        float cx = gx + gw * (static_cast<float>(i) + 0.5f);
        if (i)
            ov::rect(p, gx + gw * static_cast<float>(i), gy + 6 * k, 1 * k, 60 * k, 0xFFFFFF14);
        ov::text(p, cx, gy + 26 * k, ui::lifted(ui::ts(38 * k, i == 4 && vals[i] > 0 ? ui::kGold : 0xFFFFFFFF, ov::align::center | ov::align::middle), k),
                 std::to_string(static_cast<int>(std::lround(vals[i] * grow))));
        ov::text(p, cx, gy + 62 * k, ui::ts(11 * k, ui::kMuted, ov::align::center | ov::align::middle, 1.6f * k), es ? es_l[i] : en_l[i]);
    }
    ovl::time_left(p, x, y + h - 3 * k, w, k, 1 - (t - in_s) / hold_s, violet);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// Deadline day opens: a red band across the screen with the countdown, stripes running through it

bool draw_strip(ov::Paint *p)
{
    float t = ovl::secs(ovl::at);
    constexpr float in_s = 0.5f, hold_s = 5.5f, out_s = 0.5f;
    if (t > in_s + hold_s + out_s)
        return false;
    bool es = g_ov.es;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p);
    float h = 150 * k, y = H * 0.30f;
    float out = t > in_s + hold_s ? ui::ease_out((t - in_s - hold_s) / out_s) : 0;
    float open = ui::ease_out(t / in_s);
    ov::OpacityScope fade(p, 1 - out);
    float bh = h * open;
    float by = y + (h - bh) / 2;
    ov::rect(p, 0, by, W, bh, 0xB00400E8);
    {
        ov::ClipScope clip(p, 0, by, W, bh);
        // stripes running across
        float off = std::fmod(ui::clock_s() * 160 * k, 80 * k);
        if (!ui::g_lite)
            for (float sx = -80 * k + off; sx < W; sx += 80 * k)
                ui::para(p, sx, by, 26 * k, bh, 50 * k, 0xFFFFFF0C);
        ov::gradient(p, 0, by, W, bh * 0.5f, 0xFFFFFF14, 0xFFFFFF00);
    }
    ov::rect(p, 0, by, W, 3 * k, 0xFFFFFFC0);
    ov::rect(p, 0, by + bh - 3 * k, W, 3 * k, 0xFFFFFFC0);
    if (open < 0.6f)
        return true;
    float ta = ui::ease_out((t - 0.3f) / 0.4f);
    ov::OpacityScope tf(p, ta);
    float cx = W / 2 - (1 - ta) * 80 * k;
    ov::text(p, cx - 40 * k, y + h / 2 - 10 * k, ui::lifted(ui::ts(72 * k, 0xFFFFFFFF, ov::align::right | ov::align::middle, 4 * k), k, 0x00000090),
             "DEADLINE DAY");
    // the countdown
    int left = std::max(1, ovl::strip.left);
    float rx = W / 2 + 20 * k;
    ov::text(p, rx, y + h / 2 - 22 * k, ui::lifted(ui::ts(64 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle), k), std::to_string(left));
    ov::text(p, rx + 54 * k, y + h / 2 - 34 * k, ui::ts(16 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 2 * k),
             left == 1 ? (es ? "CARRERA" : "RACE") : (es ? "CARRERAS" : "RACES"));
    ov::text(p, rx + 54 * k, y + h / 2 - 12 * k, ui::ts(16 * k, 0xFFFFFFC0, ov::align::left | ov::align::middle, 2 * k), es ? "PARA CERRAR" : "TO GO");
    ov::text(p, W / 2, y + h - 30 * k, ui::ts(16 * k, 0xFFFFFFE0, ov::align::center | ov::align::middle, 0, W - 200 * k),
             ui::fit(p, ui::ts(16 * k), ovl::strip.body, W - 200 * k));
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The press question at Team HQ: the question and its three answers, 1 2 3 to answer, F7 for the panel. It leaves
// the keys to the game (only 1-3 are read); it goes after a while, and the question waits in the panel

bool draw_press_popup(ov::Paint *p, const ov::Input &in)
{
    float t = ovl::secs(ovl::at);
    const OverlayData &d = g_ov;
    bool answered = ovl::press_answered >= 0;
    float end = answered ? ovl::secs(ovl::press_answered_at) : 0;
    if ((!answered && (t > 25.0f || !d.press_pending)) || (answered && end > 1.6f))
        return false;
    if (!answered && t > 0.4f)
        for (int i = 0; i < static_cast<int>(d.press_answers.size()) && i < 3; ++i)
            if (in.key_pressed('1' + i) || in.key_pressed(VK_NUMPAD1 + i)) {
                ovl::press_answered = i;
                ovl::press_answered_at = ovl::Clock::now();
                choose_answer(i);
                break;
            }
    bool es = d.es;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = std::min(940 * k, safe.w - 40 * k);
    ov::TextStyle q = ui::ts(22 * k, ui::kText, ov::align::left | ov::align::top, 0, w - 150 * k);
    q.line_height = 1.25f;
    std::string question = answered ? paint_state::answered_q : d.press_question;
    std::vector<std::string> answers = d.press_answers;
    if (answered && answers.empty())
        answers.push_back(paint_state::answered_a);
    float qh = ui::text_h(p, q, question);
    float h = 70 * k + qh + 24 * k + static_cast<float>(std::min<size_t>(answers.size(), 3)) * 46 * k + 34 * k;
    float x = safe.x + (safe.w - w) / 2, y = safe.y + safe.h - h - 80 * k;
    float a = ui::ease_out(t / 0.5f), out = answered ? ui::ease_out((end - 1.1f) / 0.5f) : t > 24.4f ? (t - 24.4f) / 0.6f : 0.0f;
    ov::OpacityScope fade(p, a * (1 - std::clamp(out, 0.0f, 1.0f)));
    ov::push_translate(p, 0, (1 - a) * 40 * k);
    ui::shadow(p, x, y, w, h, k, 2.0f);
    ov::gradient(p, x, y, w, h, 0x1B1810F6, 0x0D0C09F6, false, 16 * k);
    {
        ov::ClipScope clip(p, x, y, w, h);
        ov::gradient(p, x, y, w * 0.7f, h, 0xFFB02020, 0xFFB02000, true);
        ui::glow(p, x + 60 * k, y + 50 * k, 200 * k, 0xFFB02040);
        ov::text(p, x + w - 30 * k, y - 40 * k, ui::ts(240 * k, 0xFFFFFF0A, ov::align::right | ov::align::top), "\xE2\x80\x9D");
    }
    ov::rect_rounded_outline(p, x, y, w, h, 16 * k, 1.2f * k, ui::alpha(ui::kAmber, 0.5f + 0.3f * ui::pulse(1.6f)));
    // the microphone, live
    ov::circle(p, x + 52 * k, y + 46 * k, 26 * k, 0x1A1408FF);
    ov::circle_outline(p, x + 52 * k, y + 46 * k, 26 * k, 2 * k, ui::kAmber);
    ov::arc(p, x + 52 * k, y + 46 * k, 33 * k, ui::clock_s() * 1.3f, ui::clock_s() * 1.3f + 1.2f, 2 * k, 0xFFB02090);
    ui::icon(p, ui::Icon::press, x + 52 * k, y + 46 * k, 22 * k, ui::kAmber, k);
    ui::tag(p, x + 96 * k, y + 22 * k, es ? "RUEDA DE PRENSA" : "PRESS CONFERENCE", ui::kAmber, k, 12, true);
    ov::text(p, x + 96 * k, y + 60 * k, q, question);
    float ay = y + 60 * k + qh + 18 * k;
    for (size_t i = 0; i < answers.size() && i < 3; ++i) {
        bool picked = answered && (static_cast<int>(i) == ovl::press_answered || answers.size() == 1);
        float rx = x + 96 * k, rw = w - 120 * k;
        if (picked) {
            ov::rect_rounded(p, rx - 10 * k, ay - 4 * k, rw + 10 * k, 40 * k, 8 * k, 0x30D15830);
            ov::rect_rounded_outline(p, rx - 10 * k, ay - 4 * k, rw + 10 * k, 40 * k, 8 * k, 1.5f * k, ui::kGreen);
        }
        float kw = ui::keycap(p, rx, ay + 4 * k, std::to_string(i + 1), k);
        ov::text(p, rx + kw + 14 * k, ay + 16 * k, ui::ts(17 * k, answered && !picked ? ui::kMuted : ui::kText, ov::align::left | ov::align::middle),
                 ui::fit(p, ui::ts(17 * k), answers[i], rw - kw - 20 * k));
        ay += 46 * k;
    }
    ov::text(p, x + w - 24 * k, y + h - 24 * k, ui::ts(13 * k, ui::kMuted, ov::align::right | ov::align::middle, 0.6f * k),
             answered ? (es ? "Respuesta enviada" : "Answer sent") : (es ? "1 · 2 · 3  responder   ·   F7  abrir el panel" : "1 · 2 · 3  answer   ·   F7  open the panel"));
    if (!answered)
        ovl::time_left(p, x, y + h - 3 * k, w, k, 1 - t / 25.0f, ui::kAmber);
    ov::pop_transform(p);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The report after a race: on the right of Team HQ, what changed since the last visit

bool draw_report(ov::Paint *p)
{
    float t = ovl::secs(ovl::at);
    constexpr float hold = 9.0f;
    if (t > hold + 0.6f)
        return false;
    const UiReport &r = g_ov.report;
    bool es = g_ov.es;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = 460 * k, h = 300 * k + static_cast<float>(r.headlines.size()) * 26 * k;
    float a = ui::ease_out(t / 0.45f) * (t > hold ? 1 - (t - hold) / 0.6f : 1.0f);
    float x = safe.x + safe.w - w - 24 * k + (1 - a) * 60 * k, y = safe.y + safe.h * 0.16f;
    ov::OpacityScope fade(p, a);
    ui::shadow(p, x, y, w, h, k, 1.6f);
    ov::gradient(p, x, y, w, h, 0x181922F4, 0x0E0F14F4, false, 14 * k);
    ov::rect_rounded_outline(p, x, y, w, h, 14 * k, 1 * k, 0xFFFFFF1C);
    ov::rect(p, x + 14 * k, y, w * 0.55f, 3 * k, ui::kRed);
    ui::logo(p, x + 18 * k, y + 16 * k, 22 * k, k);
    char head[64];
    std::snprintf(head, sizeof head, es ? "DESPUÉS DE LA CARRERA %d / %d" : "AFTER ROUND %d / %d", r.round, r.rounds);
    ov::text(p, x + 56 * k, y + 27 * k, ui::ts(13.5f * k, ui::kText, ov::align::left | ov::align::middle, 1.8f * k), head);
    // the player's value and its move
    float vy = y + 70 * k;
    ov::text(p, x + 20 * k, vy, ui::ts(11.5f * k, ui::kMuted, ov::align::left | ov::align::top, 1.6f * k), es ? "TU VALOR" : "YOUR VALUE");
    char v[16];
    std::snprintf(v, sizeof v, "%.1f", r.value);
    ov::text(p, x + 20 * k, vy + 18 * k, ui::lifted(ui::ts(46 * k, ui::kText), k), v);
    if (std::fabs(r.delta) >= 0.05f) {
        bool up = r.delta > 0;
        float dx = x + 20 * k + ui::text_w(p, ui::ts(46 * k), v) + 18 * k;
        ui::triangle(p, dx + 6 * k, vy + 46 * k, 7 * k, up, up ? ui::kGreen : ui::kDanger);
        char dv[16];
        std::snprintf(dv, sizeof dv, "%+.1f", r.delta);
        ov::text(p, dx + 20 * k, vy + 46 * k, ui::ts(20 * k, up ? ui::kGreen : ui::kDangerText, ov::align::left | ov::align::middle), dv);
    }
    if (r.position > 0) {
        char pos[16];
        std::snprintf(pos, sizeof pos, "P%d", r.position);
        ov::text(p, x + w - 20 * k, vy + 18 * k, ui::lifted(ui::ts(40 * k, ui::kGold, ov::align::right | ov::align::top), k), pos);
        ov::text(p, x + w - 20 * k, vy, ui::ts(11.5f * k, ui::kMuted, ov::align::right | ov::align::top, 1.6f * k), es ? "CAMPEONATO" : "CHAMPIONSHIP");
    }
    // three numbers of the market
    struct Stat {
        int v;
        const char *es, *en;
        uint32_t col;
    };
    const Stat stats[3] = {{r.rumours, "RUMORES", "RUMOURS", 0x8E9AAFFF}, {r.signings, "OFICIALES", "OFFICIAL", ui::kGreen},
                           {r.keen, "TE QUIEREN", "KEEN ON YOU", ui::kRed}};
    float sy = vy + 92 * k, sw = (w - 40 * k) / 3;
    for (int i = 0; i < 3; ++i) {
        float sx = x + 20 * k + sw * static_cast<float>(i);
        ov::rect_rounded(p, sx, sy, sw - 10 * k, 64 * k, 9 * k, ui::alpha(stats[i].col, 0.10f));
        ov::rect(p, sx, sy, sw - 10 * k, 2 * k, stats[i].col);
        ov::text(p, sx + (sw - 10 * k) / 2, sy + 26 * k, ui::ts(26 * k, ui::kText, ov::align::center | ov::align::middle),
                 std::to_string(static_cast<int>(std::lround(stats[i].v * ui::ease_out((t - 0.3f) / 0.8f)))));
        ov::text(p, sx + (sw - 10 * k) / 2, sy + 50 * k, ui::ts(10.5f * k, ui::kMuted, ov::align::center | ov::align::middle, 1.2f * k),
                 es ? stats[i].es : stats[i].en);
    }
    // the round's headlines
    float hy = sy + 86 * k;
    for (const std::string &line : r.headlines) {
        ui::para(p, x + 20 * k, hy - 5 * k, 6 * k, 10 * k, 3 * k, ui::kRed);
        ov::text(p, x + 34 * k, hy, ui::ts(14.5f * k, ui::kText2, ov::align::left | ov::align::middle), ui::fit(p, ui::ts(14.5f * k), line, w - 54 * k));
        hy += 26 * k;
    }
    float kx = x + 20 * k + ui::keycap(p, x + 20 * k, y + h - 38 * k, "F7", k) + 8 * k;
    ov::text(p, kx, y + h - 26 * k, ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::middle), es ? "todo en el panel" : "everything in the panel");
    ovl::time_left(p, x, y + h - 3 * k, w, k, 1 - t / hold, ui::kRed);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// A team became keen on the player: a small alert at the top right

bool draw_alert(ov::Paint *p)
{
    float t = ovl::secs(ovl::alert_at);
    constexpr float hold = 5.5f;
    if (t > hold + 0.5f)
        return false;
    const UiAlert &al = ovl::alert;
    bool es = g_ov.es;
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    float w = 420 * k, h = 78 * k;
    float a = ui::ease_out(t / 0.35f) * (t > hold ? 1 - (t - hold) / 0.5f : 1.0f);
    float x = safe.x + safe.w - w - 24 * k, y = safe.y + 24 * k - (1 - a) * 30 * k;
    ov::OpacityScope fade(p, a);
    uint32_t col = ui::rgb(al.col);
    ui::shadow(p, x, y, w, h, k, 1.4f);
    ov::gradient(p, x, y, w, h, 0x181922F4, 0x0E0F14F4, false, 12 * k);
    {
        ov::ClipScope clip(p, x, y, w, h);
        ui::livery(p, x, y, 140 * k, h, al.col, k, 0.7f, 12 * k);
        ov::gradient(p, x + 60 * k, y, 120 * k, h, 0x18192200, 0x181922F4, true);
    }
    ov::rect_rounded_outline(p, x, y, w, h, 12 * k, 1 * k, ui::alpha(col, 0.6f));
    ui::team_chip(p, x + 16 * k, y + h / 2 - 13 * k, al.code, al.col, k, 26);
    ov::text(p, x + 100 * k, y + 26 * k, ui::ts(11.5f * k, ui::kText2, ov::align::left | ov::align::middle, 1.8f * k), es ? "TU FUTURO" : "YOUR FUTURE");
    std::string line = es ? al.team + " va a por ti" : al.team + " want you";
    ov::text(p, x + 100 * k, y + 52 * k, ui::ts(18 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(p, ui::ts(18 * k), line, w - 190 * k));
    // the interest as a ring
    float cx = x + w - 42 * k, cy = y + h / 2, r = 22 * k;
    ov::circle_outline(p, cx, cy, r, 4 * k, 0xFFFFFF14);
    ov::arc(p, cx, cy, r, -1.5708f, -1.5708f + 6.2832f * al.interest / 100.0f * ui::ease_out((t - 0.2f) / 0.8f), 4 * k, ui::kGreen);
    ov::text(p, cx, cy, ui::ts(13 * k, ui::kText, ov::align::center | ov::align::middle), std::to_string(al.interest) + "%");
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The awards ceremony: one award after another, big in the middle; the ones handed out line up below

bool draw_awards(ov::Paint *p, const ov::Input &in)
{
    float t = ovl::secs(ovl::at);
    const OverlayData &d = g_ov;
    std::vector<const UiAward *> list;
    for (const UiAward &a : d.awards)
        if (a.year == d.awards_pending)
            list.push_back(&a);
    std::reverse(list.begin(), list.end()); // the champion first
    if (list.empty())
        return false;
    float step = 2.2f, total = 1.0f + step * static_cast<float>(list.size()) + 3.5f;
    if ((t > 0.8f && ovl::skip_pressed(in)) || t > total)
        return false;
    bool es = d.es;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p);
    float a = ui::ease_out(t / 0.4f), out = std::max(0.0f, (t - total + 0.6f) / 0.6f);
    ov::OpacityScope fade(p, a * (1 - out));
    ovl::backdrop(p, W, H, k, 1, 0xFFD60A24);
    char title[48];
    std::snprintf(title, sizeof title, "%s %d", es ? "PREMIOS DE LA TEMPORADA" : "SEASON AWARDS", d.awards_pending);
    ov::text(p, W / 2, H * 0.08f, ui::lifted(ui::ts(48 * k, 0xFFFFFFFF, ov::align::center | ov::align::top, 4 * k), k, 0xFFD60A60), title);
    ov::gradient(p, W / 2 - 240 * k, H * 0.08f + 66 * k, 480 * k, 2 * k, 0xFFD60A00, ui::kGold, true);
    // the award being handed out now
    int now = std::clamp(static_cast<int>((t - 1.0f) / step), 0, static_cast<int>(list.size()) - 1);
    float lt = t - 1.0f - step * static_cast<float>(now);
    auto title_of = [&](const std::string &key) {
        return key == "champion" ? (es ? "CAMPEÓN DEL MUNDO" : "WORLD CHAMPION") : key == "constructors" ? (es ? "CAMPEONES DE CONSTRUCTORES" : "CONSTRUCTORS' CHAMPIONS")
             : key == "rookie" ? (es ? "NOVATO DEL AÑO" : "ROOKIE OF THE YEAR") : key == "overachiever" ? (es ? "MÁS QUE SU COCHE" : "BEAT THE CAR")
                                                                                                         : (es ? "EL QUE MÁS CRECE" : "MOST IMPROVED");
    };
    if (t > 1.0f) {
        const UiAward &aw = *list[static_cast<size_t>(now)];
        float ia = ui::ease_back(lt / 0.5f), cy = H * 0.42f;
        ov::OpacityScope af(p, std::min(1.0f, lt / 0.3f));
        bool gold = aw.key == "champion" || aw.key == "constructors";
        uint32_t col = gold ? ui::kGold : aw.key == "rookie" ? 0x64D2FFFF : aw.key == "overachiever" ? ui::kGreen : ui::kAmber;
        ui::glow(p, W / 2, cy, 420 * k * ia, ui::alpha(col, 0.35f));
        // the trophy: a ring of light
        ov::circle_outline(p, W / 2, cy - 70 * k, 46 * k * ia, 3 * k, col);
        ov::arc(p, W / 2, cy - 70 * k, 56 * k * ia, ui::clock_s() * 1.2f, ui::clock_s() * 1.2f + 2.0f, 2 * k, ui::alpha(col, 0.7f));
        ui::icon(p, ui::Icon::standings, W / 2, cy - 70 * k, 40 * k * ia, col, k * 1.4f);
        ovl::sparks(p, W / 2, cy - 70 * k, lt - 0.2f, k, col, 20, 240);
        ov::text(p, W / 2, cy + 10 * k, ui::ts(16 * k, col, ov::align::center | ov::align::middle, 3 * k), title_of(aw.key));
        ov::text(p, W / 2, cy + 64 * k, ui::lifted(ui::ts(60 * k * (0.85f + 0.15f * ia), 0xFFFFFFFF, ov::align::center | ov::align::middle, 1 * k), k), aw.name);
        ov::text(p, W / 2, cy + 116 * k, ui::ts(18 * k, ui::kText2, ov::align::center | ov::align::middle), aw.detail);
    }
    // the awards handed out so far, in a row at the bottom
    float rw = 230 * k, gap = 14 * k, n = static_cast<float>(list.size());
    float rx = W / 2 - (rw * n + gap * (n - 1)) / 2, ry = H * 0.78f;
    for (size_t i = 0; i < list.size(); ++i) {
        float given = t - 1.0f - step * static_cast<float>(i);
        if (given <= 0)
            continue;
        float ga = ui::ease_out(given / 0.4f);
        ov::OpacityScope gf(p, ga);
        float x = rx + static_cast<float>(i) * (rw + gap);
        bool on = static_cast<int>(i) == now;
        ui::card(p, x, ry, rw, 74 * k, k, on, ui::kGold);
        ov::text(p, x + 14 * k, ry + 22 * k, ui::ts(10.5f * k, ui::kGold, ov::align::left | ov::align::middle, 1.4f * k),
                 ui::fit(p, ui::ts(10.5f * k), title_of(list[i]->key), rw - 28 * k));
        ov::text(p, x + 14 * k, ry + 50 * k, ui::ts(17 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(p, ui::ts(17 * k), list[i]->name, rw - 28 * k));
    }
    ov::text(p, W / 2, H - 46 * k, ui::ts(14 * k, ui::alpha(ui::kText2, 0.5f + 0.5f * ui::pulse(1.6f)), ov::align::center | ov::align::middle, 1.6f * k),
             es ? "Esc / A  continuar" : "Esc / A  continue");
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The season's cover: the year in big outlined figures, every team's plan, and the winter's moves

bool draw_cover(ov::Paint *p, const ov::Input &in)
{
    float t = ovl::secs(ovl::at);
    const OverlayData &d = g_ov;
    constexpr float total = 11.0f;
    if ((t > 0.8f && ovl::skip_pressed(in)) || t > total)
        return false;
    bool es = d.es;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p);
    float a = ui::ease_out(t / 0.5f), out = std::max(0.0f, (t - total + 0.6f) / 0.6f);
    ov::OpacityScope fade(p, a * (1 - out));
    ovl::backdrop(p, W, H, k, 1, 0xE1060030);
    // the year, huge and outlined, sliding in
    char year[8];
    std::snprintf(year, sizeof year, "%d", d.cover_pending);
    float ya = ui::ease_out((t - 0.1f) / 0.8f);
    ov::TextStyle ys = ui::ts(260 * k, 0xFFFFFF22, ov::align::left | ov::align::top, 8 * k);
    ys.outline = 0xE10600C0;
    ys.outline_px = 2 * k;
    ov::text(p, 70 * k - (1 - ya) * 120 * k, H * 0.08f, ys, year);
    ov::text(p, 80 * k, H * 0.08f + 270 * k, ui::lifted(ui::ts(30 * k, 0xFFFFFFFF, ov::align::left | ov::align::top, 6 * k), k),
             es ? "LA TEMPORADA" : "THE SEASON");
    ov::text(p, 82 * k, H * 0.08f + 314 * k, ui::ts(15 * k, ui::kText2, ov::align::left | ov::align::top, 2.4f * k),
             es ? "SILLY SEASON  ·  QUIÉN VA A POR QUÉ" : "SILLY SEASON  ·  WHO IS AFTER WHAT");
    // two numbers of the season ahead
    {
        int title = static_cast<int>(std::count_if(d.teams.begin(), d.teams.end(), [](const UiTeam &x) { return x.strategy == "title"; }));
        const int vals[2] = {static_cast<int>(d.cover_moves.size()), title};
        const char *es_l[2] = {"FICHAJES DEL INVIERNO", "VAN A POR EL TÍTULO"}, *en_l[2] = {"WINTER MOVES", "GOING FOR THE TITLE"};
        float sy = H * 0.08f + 380 * k, grow = ui::ease_out((t - 0.8f) / 1.2f);
        for (int i = 0; i < 2; ++i) {
            float sx = 80 * k + static_cast<float>(i) * 230 * k;
            ov::rect(p, sx, sy, 3 * k, 74 * k, i ? ui::kGold : ui::kRed);
            ov::text(p, sx + 16 * k, sy - 4 * k, ui::lifted(ui::ts(54 * k, 0xFFFFFFFF), k), std::to_string(static_cast<int>(std::lround(vals[i] * grow))));
            ov::text(p, sx + 16 * k, sy + 62 * k, ui::ts(11.5f * k, ui::kText2, ov::align::left | ov::align::middle, 1.6f * k), es ? es_l[i] : en_l[i]);
        }
    }
    ui::logo(p, 80 * k, H - 110 * k, 44 * k, k);
    // the teams' plans, a column of chips
    float px = W * 0.47f, py = H * 0.10f, rowh = 44 * k;
    ov::text(p, px, py, ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::top, 2.4f * k), es ? "EL PLAN DE CADA EQUIPO" : "EVERY TEAM'S PLAN");
    py += 32 * k;
    int i = 0;
    for (const UiTeam &tm : d.teams) {
        float ia = ui::ease_out((t - 0.6f - 0.12f * static_cast<float>(i)) / 0.4f);
        if (ia <= 0)
            break;
        ov::OpacityScope tf(p, ia);
        float ry = py + static_cast<float>(i) * rowh;
        ov::rect_rounded(p, px, ry, W * 0.24f, rowh - 6 * k, 8 * k, 0xFFFFFF08);
        ui::team_chip(p, px + 10 * k, ry + 6 * k, tm.code, tm.col, k, 26);
        ov::text(p, px + 84 * k, ry + (rowh - 6 * k) / 2, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle),
                 ui::fit(p, ui::ts(15 * k), tm.name, W * 0.24f - 200 * k));
        if (!tm.strategy.empty()) {
            ui::Tag st = ui::strategy_tag(tm.strategy);
            float tw = ui::text_w(p, ui::ts(10 * k), es ? st.es : st.en) + 40 * k;
            ui::tag(p, px + W * 0.24f - tw - 6 * k, ry + 9 * k, es ? st.es : st.en, st.col, k, 10, true);
        }
        ++i;
    }
    // the winter's moves
    float mx = W * 0.74f, my = H * 0.10f;
    ov::text(p, mx, my, ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::top, 2.4f * k), es ? "LOS FICHAJES DEL INVIERNO" : "THE WINTER'S MOVES");
    my += 32 * k;
    if (d.cover_moves.empty())
        ov::text(p, mx, my + 10 * k, ui::ts(15 * k, ui::kMuted), es ? "La misma parrilla que el año pasado." : "The same grid as last year.");
    for (size_t j = 0; j < d.cover_moves.size(); ++j) {
        const UiMove &m = d.cover_moves[j];
        float ma = ui::ease_out((t - 1.6f - 0.15f * static_cast<float>(j)) / 0.4f);
        if (ma <= 0)
            break;
        ov::OpacityScope mf(p, ma);
        float ry = my + static_cast<float>(j) * 56 * k;
        ov::text(p, mx, ry + 12 * k, ui::ts(16 * k, ui::kText, ov::align::left | ov::align::middle), ui::fit(p, ui::ts(16 * k), m.driver, W * 0.22f));
        float cx = mx;
        if (!m.from_code.empty())
            cx += ui::team_chip(p, cx, ry + 26 * k, m.from_code, m.from_col, k, 20) + 6 * k;
        else
            cx += ui::tag(p, cx, ry + 27 * k, es ? "NUEVO" : "NEW", ui::kGreen, k, 9) + 6 * k;
        ui::arrow_right(p, cx, ry + 36 * k, 26 * k, k, ui::kMuted);
        ui::team_chip(p, cx + 32 * k, ry + 26 * k, m.to_code, m.to_col, k, 20);
    }
    ov::text(p, W / 2, H - 46 * k, ui::ts(14 * k, ui::alpha(ui::kText2, 0.5f + 0.5f * ui::pulse(1.6f)), ov::align::center | ov::align::middle, 1.6f * k),
             es ? "Esc / A  continuar" : "Esc / A  continue");
    return true;
}

// ---------------------------------------------------------------------------------------------------
// Next season's grid, team by team, once it's final: who comes in (and from where), who goes, and a summary

bool draw_reveal(ov::Paint *p, const ov::Input &in)
{
    using namespace ui4;
    const OverlayData &d = g_ov;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p);
    float t = std::chrono::duration<float>(Clock::now() - reveal_at).count();
    size_t n = d.grid.size();
    float step = 0.42f, all_in = 1.4f + step * static_cast<float>(n);
    bool skip = t > 0.6f && ovl::skip_pressed(in);
    if (skip || t > all_in + 7.0f)
        return false;
    float a = ui::ease_out(t / 0.4f), out = std::max(0.0f, (t - all_in - 6.4f) / 0.6f);
    ov::OpacityScope fade(p, a * (1 - out));
    ovl::backdrop(p, W, H, k, 1, 0xE106002C);
    char title[48];
    std::snprintf(title, sizeof title, "%s %d", d.es ? "LA PARRILLA" : "THE GRID", d.year + 1);
    float ta = ui::ease_out((t - 0.2f) / 0.6f);
    ov::text(p, W / 2, H * 0.06f + (1 - ta) * 20 * k, ui::lifted(ui::ts(58 * k, ui::alpha(0xFFFFFFFF, ta), ov::align::center | ov::align::top, 4 * k), k, 0xE1060080),
             title);
    ov::text(p, W / 2, H * 0.06f + 74 * k, ui::ts(15 * k, ui::alpha(ui::kText2, ta), ov::align::center | ov::align::top, 3 * k),
             d.es ? "SILLY SEASON  ·  PARRILLA CONFIRMADA" : "SILLY SEASON  ·  GRID CONFIRMED");
    ov::gradient(p, W / 2 - 260 * k * ta, H * 0.06f + 104 * k, 520 * k * ta, 2 * k, 0xE1060000, ui::kRed, true);
    // where each driver is now, and who leaves the grid
    auto now_team = [&](const std::string &id) -> const UiTeam * {
        for (const UiTeam &tm : d.teams)
            for (const UiSeat &s : tm.seat)
                if (!id.empty() && s.id == id)
                    return &tm;
        return nullptr;
    };
    std::set<std::string> next_ids;
    for (const UiTeam &tm : d.grid)
        for (const UiSeat &s : tm.seat)
            if (!s.id.empty())
                next_ids.insert(s.id);
    int fresh = 0, rookies = 0, gone = 0;
    for (const UiTeam &tm : d.grid)
        for (const UiSeat &s : tm.seat)
            if (s.fresh) {
                ++fresh;
                if (const UiDriverCard *c = d.card(s.id); c && c->seasons == 0)
                    ++rookies;
            }
    for (const UiTeam &tm : d.teams)
        for (const UiSeat &s : tm.seat)
            gone += !s.id.empty() && !next_ids.count(s.id) ? 1 : 0;
    float cols = 2, gap = 18 * k, cw = std::min(780 * k, (W - 160 * k - gap) / cols), ch = 92 * k;
    float rows_n = std::ceil(static_cast<float>(n) / cols), total_h = rows_n * ch + (rows_n - 1) * 12 * k;
    float x0 = (W - (cw * cols + gap)) / 2, y0 = std::max(H * 0.06f + 130 * k, (H - total_h) / 2 + 20 * k);
    for (size_t i = 0; i < n; ++i) {
        const UiTeam &tm = d.grid[i];
        float ti = t - 1.2f - step * static_cast<float>(i);
        if (ti <= 0)
            continue;
        float ia = ui::ease_out(ti / 0.45f);
        float x = x0 + static_cast<float>(i % 2) * (cw + gap), y = y0 + static_cast<float>(i / 2) * (ch + 12 * k);
        ov::OpacityScope tf(p, ia);
        ov::push_translate(p, (i % 2 ? 1 : -1) * (1 - ia) * 80 * k, 0);
        ui::shadow(p, x, y, cw, ch, k, 1.2f);
        ov::gradient(p, x, y, cw, ch, 0x181922FF, 0x101118FF, false, 12 * k);
        {
            ov::ClipScope clip(p, x, y, cw, ch);
            ov::gradient(p, x, y, cw * 0.55f, ch, ui::rgb(tm.col, 0x70), ui::rgb(tm.col, 0x00), true);
            ui::livery(p, x, y, 220 * k, ch, tm.col, k, 0.8f, 12 * k);
            ov::text(p, x + cw - 14 * k, y + ch / 2, ui::ts(54 * k, 0xFFFFFF10, ov::align::right | ov::align::middle, 2 * k), tm.code);
        }
        ov::rect(p, x, y, 5 * k, ch, ui::rgb(tm.col));
        ov::text(p, x + 22 * k, y + ch / 2, ui::lifted(ui::ts(22 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 0.6f * k), k),
                 ui::fit(p, ui::ts(22 * k), tm.name, 190 * k));
        for (int s = 0; s < 2; ++s) {
            const UiSeat &seat = tm.seat[s];
            float sy = y + 26 * k + s * 40 * k, sx = x + 240 * k;
            float sa = ui::ease_out((ti - 0.2f - 0.15f * s) / 0.35f);
            if (sa <= 0)
                continue;
            ov::OpacityScope sf(p, sa);
            uint32_t col = seat.player ? ui::kGold : ui::kText;
            std::string name = seat.name.empty() ? std::string(d.es ? "por decidir" : "to be decided") : ui::fit(p, ui::ts(20 * k), seat.name, cw - 420 * k);
            ov::text(p, sx, sy, ui::ts(20 * k, col, ov::align::left | ov::align::middle), name);
            if (seat.fresh) {
                // where he comes from
                float fx = sx + ui::text_w(p, ui::ts(20 * k), name) + 12 * k;
                if (const UiTeam *from = now_team(seat.id); from && from->id != tm.id) {
                    ui::team_chip(p, fx, sy - 10 * k, from->code, from->col, k, 20);
                    ui::arrow_right(p, fx + 62 * k, sy, 20 * k, k, ui::kGreen);
                } else {
                    ui::tag(p, fx, sy - 10 * k, d.es ? "DEBUTA" : "ROOKIE", 0x64D2FFFF, k, 9);
                }
                ui::glow(p, x + cw - 70 * k, sy, 36 * k, 0x30D15850);
                ui::tag(p, x + cw - 100 * k, sy - 11 * k, d.es ? "NUEVO" : "NEW", ui::kGreen, k, 11, true);
            } else if (seat.player) {
                ui::tag(p, x + cw - 100 * k, sy - 11 * k, d.es ? "TÚ" : "YOU", ui::kGold, k, 11, true);
            }
        }
        // who leaves this team
        std::string out_names;
        if (const UiTeam *now = [&]() -> const UiTeam * {
                for (const UiTeam &x2 : d.teams)
                    if (x2.id == tm.id)
                        return &x2;
                return nullptr;
            }())
            for (const UiSeat &s : now->seat)
                if (!s.id.empty() && s.id != tm.seat[0].id && s.id != tm.seat[1].id)
                    out_names += (out_names.empty() ? "" : ", ") + s.name;
        if (!out_names.empty() && ti > 0.6f)
            ov::text(p, x + 22 * k, y + ch - 14 * k, ui::ts(11.5f * k, 0xFF6A63FF, ov::align::left | ov::align::middle, 0.6f * k),
                     ui::fit(p, ui::ts(11.5f * k), (d.es ? "SALE: " : "OUT: ") + out_names, 200 * k)); // under the team's name
        ov::pop_transform(p);
    }
    // the summary once every team is in
    if (t > all_in) {
        float sa = ui::ease_out((t - all_in) / 0.5f);
        ov::OpacityScope sf(p, sa);
        float sy = std::min(H - 110 * k, y0 + total_h + 26 * k);
        // singular or plural, and only the parts that happened
        std::string line;
        auto part = [&](int v, const char *one_en, const char *many_en, const char *one_es, const char *many_es) {
            if (v <= 0)
                return;
            if (!line.empty())
                line += "   ·   ";
            line += std::to_string(v) + " " + (d.es ? (v == 1 ? one_es : many_es) : (v == 1 ? one_en : many_en));
        };
        part(fresh, "NEW FACE", "NEW FACES", "CARA NUEVA", "CARAS NUEVAS");
        part(rookies, "ROOKIE", "ROOKIES", "DEBUTANTE", "DEBUTANTES");
        part(gone, "WITHOUT A SEAT", "WITHOUT A SEAT", "SIN ASIENTO", "SIN ASIENTO");
        if (line.empty())
            line = d.es ? "SIN CAMBIOS EN LA PARRILLA" : "AN UNCHANGED GRID";
        float lw = ui::text_w(p, ui::ts(18 * k, 0xFFFFFFFF, ov::align::center, 2.4f * k), line) + 80 * k;
        ui::para(p, W / 2 - lw / 2, sy, lw, 44 * k, 16 * k, 0xE10600D0);
        ov::text(p, W / 2, sy + 22 * k, ui::ts(18 * k, 0xFFFFFFFF, ov::align::center | ov::align::middle, 2.4f * k), line);
        ov::text(p, W / 2, H - 40 * k, ui::ts(14 * k, ui::alpha(ui::kText2, ui::pulse(1.6f) * 0.5f + 0.5f), ov::align::center | ov::align::middle, 1.6f * k),
                 d.es ? "Esc / A  continuar" : "Esc / A  continue");
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------
// The Team HQ card: unread news and the last three headlines, the deadline countdown as a ring, the player's place

void draw_widget(ov::Paint *p)
{
    using namespace ui4;
    const OverlayData &d = g_ov;
    float t = std::chrono::duration<float>(Clock::now() - widget_at).count();
    constexpr float hold = 12.0f;
    if (t > hold + 0.6f)
        return;
    float a = ui::ease_out(t / 0.4f) * (t > hold ? 1 - (t - hold) / 0.6f : 1.0f);
    ov::Rect safe = ov::safe_area(p);
    float k = ov::ui_scale(p);
    size_t lines = std::min<size_t>(3, d.news.size());
    float w = 440 * k, h = 112 * k + static_cast<float>(std::max<size_t>(1, lines)) * 26 * k;
    float x = safe.x + safe.w - w - 24 * k + (1 - a) * 40 * k, y = safe.y + safe.h * 0.2f;
    ov::OpacityScope fade(p, a);
    ui::shadow(p, x, y, w, h, k, 1.4f);
    ov::gradient(p, x, y, w, h, 0x181922F2, 0x0E0F14F2, false, 12 * k);
    ov::rect_rounded_outline(p, x, y, w, h, 12 * k, 1 * k, 0xFFFFFF1C);
    ov::rect(p, x + 12 * k, y, w * 0.5f, 2 * k, ui::kRed);
    ui::logo(p, x + 16 * k, y + 16 * k, 22 * k, k);
    ov::text(p, x + 56 * k, y + 27 * k, ui::ts(15 * k, ui::kText, ov::align::left | ov::align::middle, 1.8f * k), "SILLY SEASON");
    float tx = x + w - 16 * k;
    if (d.unread > 0) {
        char b[32];
        std::snprintf(b, sizeof b, d.es ? "%d NUEVAS" : "%d NEW", std::min(d.unread, 99));
        float bw = ui::text_w(p, ui::ts(11 * k), b) + 40 * k;
        ui::tag(p, tx - bw, y + 16 * k, b, ui::kRed, k, 11, true);
        tx -= bw + 8 * k;
    }
    // the player's place in the championship
    for (size_t r = 0; r < d.standings.size(); ++r)
        if (d.standings[r].player) {
            char pos[16];
            std::snprintf(pos, sizeof pos, "P%zu", r + 1);
            ov::text(p, tx - 4 * k, y + 27 * k, ui::ts(15 * k, ui::kGold, ov::align::right | ov::align::middle, 1 * k), pos);
            break;
        }
    // the last headlines, the newest bright
    float ly = y + 62 * k;
    if (lines == 0)
        ov::text(p, x + 16 * k, ly, ui::ts(16 * k, ui::kText2, ov::align::left | ov::align::middle), d.es ? "El paddock está tranquilo" : "The paddock is quiet");
    for (size_t i = 0; i < lines; ++i) {
        const UiNews &n = d.news[i];
        ui::para(p, x + 16 * k, ly - 6 * k, 6 * k, 12 * k, 3 * k, ui::news_tag(n.kind).col);
        ov::TextStyle s = ui::ts(i == 0 ? 16.5f * k : 14.5f * k, i == 0 ? ui::kText : ui::kText2, ov::align::left | ov::align::middle);
        ov::text(p, x + 30 * k, ly, s, ui::fit(p, s, n.title, w - 110 * k));
        ly += 26 * k;
    }
    // the deadline countdown as a ring, at the right
    if (d.deadline && !d.finalised) {
        float cx = x + w - 40 * k, cy = y + 62 * k + 13 * k, r = 20 * k;
        ui::glow(p, cx, cy, 40 * k, ui::alpha(ui::kRed, 0.3f + 0.2f * ui::pulse(1.2f)));
        ov::circle_outline(p, cx, cy, r, 4 * k, 0xFFFFFF14);
        ov::arc(p, cx, cy, r, -1.5708f, -1.5708f + 6.2832f * (d.deadline == 1 ? 0.5f : 1.0f), 4 * k, ui::kRed);
        ov::text(p, cx, cy, ui::ts(16 * k, ui::kText, ov::align::center | ov::align::middle), std::to_string(d.deadline));
        ov::text(p, cx, cy + 32 * k, ui::ts(9 * k, 0xFF6A63FF, ov::align::center | ov::align::middle, 1.2f * k), "DEADLINE");
    }
    float fy = y + h - 26 * k;
    float kx = x + 16 * k + ui::keycap(p, x + 16 * k, fy - 12 * k, "F7", k) + 8 * k;
    ov::text(p, kx, fy, ui::ts(13 * k, ui::kText2, ov::align::left | ov::align::middle), d.es ? "abre el mercado" : "open the market hub");
}

// ---------------------------------------------------------------------------------------------------
// The manager: one overlay at a time, the most important first. Full-screen moments take the keys; the rest leave
// them to the game. Nothing shows during a session or while the panel is open.

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
    bool hq = g_at_hq.load();
    using ovl::Show;

    // next season's grid, revealed team by team once it's final (or replayed from the Grid page)
    if (!ui4::reveal_on && !open && usable && g_ov.reveal_pending && g_ov.grid_reveal && hq && ovl::current == Show::none) {
        ui4::reveal_on = true;
        ui4::reveal_manual = false;
        ui4::reveal_at = now;
        ov::focus(true);
    }
    if (ui4::reveal_on) {
        if (!usable || !draw_reveal(p, in)) {
            ui4::reveal_on = false;
            ov::focus(false);
            if (!ui4::reveal_manual) {
                g_reveal_done = g_ov.year;
                g_ov.reveal_pending = false;
            }
        }
        g_board_open = false;
        return;
    }
    bool toggle = in.key_pressed(VK_F7) || (in.pad_held(ov::pad::back) && in.pad_hit(ov::pad::y));
    bool back = in.key_pressed(VK_ESCAPE) || in.pad_hit(ov::pad::b);
    bool inner = ui4::modal != 0 || ui4::typing; // Esc closes the card or the search first
    bool full = ovl::current == Show::awards || ovl::current == Show::cover;
    if (open && (!usable || toggle || (back && !inner))) {
        open = false;
        ov::focus(false);
        ui4::modal = 0;
        ui4::modal_back.clear();
        ui4::typing = false;
        page_scroll[static_cast<size_t>(tab)] = scroll_target; // found where it was next time
    } else if (!open && usable && toggle && !full) {
        open = true;
        ov::focus(true);
        scroll = scroll_target = page_scroll[static_cast<size_t>(tab)];
        press_sel = 0;
        menu_y = -1;
        opened_at = tab_at = now;
        // a waiting question opens on the press page
        if (g_ov.press_pending) {
            tab = P_PRESS;
            scroll = scroll_target = 0;
        }
        g_seen_request = true;
        if (ovl::current == Show::press)
            ovl::current = Show::none; // the panel takes the question
    }
    g_board_open = open;
    if (open) {
        draw_board(p, in, dt);
        if (ui4::reveal_on) { // R on the Grid page: the hub makes way for the reveal
            g_board_open = false;
            ui4::modal = 0;
            ui4::modal_back.clear();
        }
        return;
    }
    if (!usable) {
        if (full)
            ov::focus(false);
        ovl::current = Show::none;
        return;
    }

    // nothing on screen: the next one, the most important first
    if (ovl::current == Show::none) {
        if (hq && g_ov.awards_pending && ovl::awards_local != g_ov.awards_pending) {
            ovl::current = Show::awards;
            ovl::awards_local = g_ov.awards_pending;
            ov::focus(true);
        } else if (hq && g_ov.cover_pending && ovl::cover_local != g_ov.cover_pending) {
            ovl::current = Show::cover;
            ovl::cover_local = g_ov.cover_pending;
            ov::focus(true);
        } else if (hq && g_ov.press_popup && g_ov.press_pending && ovl::press_shown != g_ov.press_id) {
            ovl::current = Show::press;
            ovl::press_shown = g_ov.press_id;
            ovl::press_answered = -1;
        } else if (!banner_on && !g_ov.signings.empty()) {
            ovl::current = Show::signing;
            ui4::signing = std::move(g_ov.signings.front());
            g_ov.signings.pop_front();
            ui4::signing_at = now;
        } else if (!banner_on && !g_ov.farewells.empty()) {
            ovl::current = Show::farewell;
            ovl::farewell = std::move(g_ov.farewells.front());
            g_ov.farewells.pop_front();
        } else if (!banner_on && !g_ov.strips.empty()) {
            ovl::current = Show::strip;
            ovl::strip = std::move(g_ov.strips.front());
            g_ov.strips.pop_front();
        } else if (hq && g_ov.race_report && g_ov.report.id > ovl::report_shown && !banner_on && g_ov.banners.empty()) {
            ovl::current = Show::report;
            ovl::report_shown = g_ov.report.id;
        } else if (banner_on || !g_ov.banners.empty()) {
            ovl::current = Show::banner;
        }
        if (ovl::current != Show::none && ovl::current != Show::banner && ovl::current != Show::signing)
            ovl::at = now;
    }
    // the one on screen
    bool alive = true;
    switch (ovl::current) {
    case Show::awards:
        alive = draw_awards(p, in);
        if (!alive) {
            g_awards_done = ovl::awards_local;
            ov::focus(false);
        }
        break;
    case Show::cover:
        alive = draw_cover(p, in);
        if (!alive) {
            g_cover_done = ovl::cover_local;
            ov::focus(false);
        }
        break;
    case Show::press: alive = draw_press_popup(p, in); break;
    case Show::signing: alive = draw_signing(p); break;
    case Show::farewell: alive = draw_farewell(p); break;
    case Show::strip: alive = draw_strip(p); break;
    case Show::report: alive = draw_report(p); break;
    case Show::banner:
        draw_banner(p);
        alive = banner_on;
        break;
    default: break;
    }
    if (!alive)
        ovl::current = Show::none;
    full = ovl::current == Show::awards || ovl::current == Show::cover;

    // the alerts of teams keen on the player, at the top right (not over a full-screen moment)
    if (!full) {
        if (!ovl::alert_on && !g_ov.alerts.empty()) {
            ovl::alert = std::move(g_ov.alerts.front());
            g_ov.alerts.pop_front();
            ovl::alert_on = true;
            ovl::alert_at = now;
        }
        if (ovl::alert_on && !draw_alert(p))
            ovl::alert_on = false;
    }
    // the Team HQ card: when the player arrives, and when news comes in, with nothing else on screen
    if (hq && !ui4::widget_hq)
        ui4::widget_at = now;
    if (hq && ui4::widget_unread >= 0 && g_ov.unread > ui4::widget_unread)
        ui4::widget_at = now;
    ui4::widget_hq = hq;
    ui4::widget_unread = g_ov.unread;
    if (hq && g_ov.hq_widget && ovl::current == Show::none && !ovl::alert_on)
        draw_widget(p);
}
