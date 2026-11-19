// A fake game for the host tests of plugin.cpp: a SwoftyApi whose tables do what the core's do as far as the addon
// can tell. Events and hooks keep their handlers and fire them when the test says; storage and the career data live
// in memory and count their reads and writes; the career operations answer from the fake career's rows (set_seat with
// the core's rules: a race driver or a free agent takes a seat, a reserve is refused); the telemetry feed is a real
// named file mapping; texts, popups and options are kept for the test to read. The overlay and the game database are
// left out (the addon then says "not available", as on a core without them).
//
// Included after ../src/plugin.cpp (it needs the SDK headers the plugin includes and the plugin's own globals).
// The SDK's swofty/fake_core.hpp only records calls, so it serves here for the registry alone.
#pragma once
#include "fake_career.hpp"

#include <array>
#include <map>

#include <swofty/fake_core.hpp>

namespace fake {

struct Game;
Game *game = nullptr;

struct Option {
    uint32_t type = 0;
    int32_t toggle = 0;
    double slider = 0;
    std::string choice;
};

struct Game {
    SwoftyApi api{};
    SwoftyEvents events_t{};
    SwoftyHooks hooks_t{};
    SwoftyServices services_t{};
    SwoftyStorage storage_t{};
    SwoftyPopup popup_t{};
    SwoftyLang lang_t{};
    SwoftyTelemetry telemetry_t{};
    SwoftyCareerOps ops_t{};
    SwoftyCareerSaves saves_t{};
    swofty::fake::Core registry{"alexs.silly-season"};

    // ---- events, hooks and frames
    struct Sub {
        std::string event;
        SwoftyEventFn fn;
        void *user;
    };
    struct Hook {
        std::string point;
        uint32_t phase;
        SwoftyHookFn fn;
        void *user;
    };
    std::vector<Sub> subs;
    std::vector<Hook> hooks;
    std::vector<std::pair<SwoftyFrameFn, void *>> frame_fns;
    std::vector<void (*)()> on_load;
    uint32_t tokens = 0;
    uint64_t frame = 0;
    double clock = 0;
    std::map<std::string, std::string> ctx; // the moment's context, each part as JSON

    // ---- storage and the career data
    bool storage_on = true;
    std::map<std::string, std::string> files;
    std::map<std::string, int> reads, writes;
    int removes = 0;
    int write_delay_ms = 0;
    std::set<std::string> fail_writes;               // 15 Nov: storage paths whose writes fail
    bool fail_data_set = false;                      // 15 Nov: the core's career data refuses writes
    bool feed_late = false;                          // 15 Nov: the telemetry feed isn't there yet
    std::string career_id;                          // what career::saves::current() answers
    std::string career_index;                       // 1.1.51: what career::saves::index() answers ("": unavailable)
    std::map<std::string, std::string> career_data;  // "<career>|<addon>" -> JSON
    int data_sets = 0, data_gets = 0, needs = 0;

    // ---- the career operations
    int season_index = 0, round = 1, rounds = 24;
    bool round_known = true;
    bool seats_writable = true;                      // false: My Team (the core has no contracts manager there)
    std::set<uint32_t> refuse;                       // drivers (stat ids) the game refuses whatever their row says
    bool refuse_all = false;                         // the game refuses every seat change
    bool seats_ignored = false;                      // 1.1.3: the game says ok to a seat change but the rows don't move
    int busy_after_writes = 0;                       // 1.1.3: after this many writes the rows read as busy (0: never)
    int seats_ok = 0, seats_refused = 0;
    std::vector<std::string> seat_log;
    std::map<uint32_t, std::array<float, 4>> ratings;
    std::map<uint32_t, int> numbers;                 // driver stat -> racing number
    std::map<std::string, uint32_t> driver_stats, team_stats; // what "game/<id>" resolves to

    // ---- texts, popups, options, services
    std::map<std::string, std::string> texts;
    int lang_sets = 0, lang_refreshes = 0;
    std::vector<std::pair<std::string, std::string>> popups;
    std::map<std::string, Option> options;
    struct Watch {
        std::string id;
        SwoftySettingFn fn;
        void *user;
    };
    std::vector<Watch> watches;
    std::vector<std::string> published;
    uint32_t pause = 0; // the game's pause reasons (swofty::view::paused())

    // ---- telemetry
    bool telemetry_on = true;
    HANDLE feed_map = nullptr;
    SwoftyTelemetryFeed *feed = nullptr;

    explicit Game(const json::Value &manifest)
    {
        game = this;
        // the options as the manifest declares them
        if (const json::Array *list = manifest["options"].if_array())
            for (const json::Value &o : *list) {
                std::string id(o["id"].as_string());
                Option opt;
                std::string_view type = o["type"].as_string();
                if (type == "toggle") {
                    opt.type = SWOFTY_SETTING_TOGGLE;
                    opt.toggle = o["default"].as_bool(false) ? 1 : 0;
                    registry.option(id, json::Value(opt.toggle != 0));
                } else if (type == "slider") {
                    opt.type = SWOFTY_SETTING_SLIDER;
                    opt.slider = o["default"].as_double(0);
                    registry.option(id, json::Value(opt.slider));
                } else {
                    opt.type = SWOFTY_SETTING_CHOICE;
                    opt.choice = std::string(o["default"].as_string());
                    registry.option(id, json::Value(opt.choice));
                }
                options[id] = opt;
            }

        events_t = {e_available, e_subscribe, e_on_frame, e_unsubscribe};
        hooks_t.version = SWOFTY_HOOKS_VERSION;
        hooks_t.available = e_available;
        hooks_t.live = [](const char *, uint32_t *v) {
            if (v)
                *v = 1;
            return int(SWOFTY_ASSET_OK);
        };
        hooks_t.attach = [](const char *point, uint32_t phase, int32_t, SwoftyHookFn fn, void *user, uint32_t *token) {
            return game->attach(point, phase, fn, user, token);
        };
        hooks_t.detach = [](uint32_t) { return int(SWOFTY_ASSET_OK); };
        hooks_t.define = [](const char *, uint32_t) { return int(SWOFTY_ASSET_OK); };
        hooks_t.fire = [](const char *point, void *payload, int *replaced) {
            game->hook(point, payload);
            if (replaced)
                *replaced = 0;
            return int(SWOFTY_ASSET_OK);
        };
        hooks_t.attach_in = [](const char *point, uint32_t phase, int32_t, SwoftyText, SwoftyHookFn fn, void *user,
                               uint32_t *token) { return game->attach(point, phase, fn, user, token); };
        hooks_t.define_in = [](const char *, uint32_t, uint32_t, SwoftyText) { return int(SWOFTY_ASSET_OK); };
        hooks_t.context = h_context;
        hooks_t.context_in = [](const void *, SwoftyText, int *out) {
            if (out)
                *out = 0;
            return int(SWOFTY_ASSET_OK);
        };

        services_t.version = SWOFTY_SERVICES_VERSION;
        services_t.available = e_available;
        services_t.publish = [](const char *name, uint32_t, const void *, size_t) {
            game->published.emplace_back(name ? name : "");
            return int(SWOFTY_ASSET_OK);
        };
        services_t.when = [](const char *, uint32_t, size_t, SwoftyServiceFn, void *) { return int(SWOFTY_ASSET_OK); };

        storage_t.available = [] { return game->storage_on ? 1 : 0; };
        storage_t.read = s_read;
        storage_t.write = [](const char *path, const void *data, size_t len) {
            if (game->write_delay_ms)
                Sleep(static_cast<DWORD>(game->write_delay_ms)); // a slow disk, to see the addon's "timing" lines
            if (game->fail_writes.count(path))
                return int(SWOFTY_ASSET_UNAVAILABLE); // 15 Nov: a write that fails (a full disk, a locked file)
            game->files[path].assign(static_cast<const char *>(data), len);
            ++game->writes[path];
            return int(SWOFTY_ASSET_OK);
        };
        storage_t.append = [](const char *path, const void *data, size_t len) {
            game->files[path].append(static_cast<const char *>(data), len);
            ++game->writes[path];
            return int(SWOFTY_ASSET_OK);
        };
        storage_t.remove = [](const char *path) {
            ++game->removes;
            return int(game->files.erase(path) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID);
        };
        storage_t.size = [](const char *path, uint64_t *out) {
            auto it = game->files.find(path);
            if (it == game->files.end())
                return int(SWOFTY_ASSET_INVALID);
            *out = it->second.size();
            return int(SWOFTY_ASSET_OK);
        };
        storage_t.list = [](const char *, char *, size_t, size_t *) { return int(SWOFTY_ASSET_UNAVAILABLE); };

        popup_t.available = e_available;
        popup_t.show = [](const char *title, const char *body) {
            game->popups.push_back({title ? title : "", body ? body : ""});
            return int(SWOFTY_ASSET_OK);
        };

        lang_t.available = e_available;
        lang_t.override_set = [](const char *, const char *key, const char *text) {
            game->texts[key] = text ? text : "";
            ++game->lang_sets;
            return int(SWOFTY_ASSET_OK);
        };
        lang_t.override_clear = [](const char *, const char *key) {
            game->texts.erase(key);
            return int(SWOFTY_ASSET_OK);
        };
        lang_t.override_clear_all = [] {
            game->texts.clear();
            return int(SWOFTY_ASSET_OK);
        };
        lang_t.refresh = [] {
            ++game->lang_refreshes;
            return int(SWOFTY_ASSET_OK);
        };

        // the reads answered unavailable in the game all session long (launcher 1.8.38); the feed is what works
        telemetry_t.available = [] { return game->telemetry_on ? 1 : 0; };
        telemetry_t.session = [](SwoftySessionState *) { return int(SWOFTY_ASSET_UNAVAILABLE); };
        telemetry_t.car = [](uint32_t, SwoftyCarState *) { return int(SWOFTY_ASSET_UNAVAILABLE); };
        telemetry_t.participant = [](uint32_t, SwoftyParticipant *) { return int(SWOFTY_ASSET_UNAVAILABLE); };
        telemetry_t.shared_memory = [](const char **name) {
            if (!game->feed || game->feed_late)
                return int(SWOFTY_ASSET_UNAVAILABLE);
            *name = "Local\\SillySeasonTestFeed";
            return int(SWOFTY_ASSET_OK);
        };
        feed_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SwoftyTelemetryFeed),
                                      L"Local\\SillySeasonTestFeed");
        feed = feed_map ? static_cast<SwoftyTelemetryFeed *>(MapViewOfFile(feed_map, FILE_MAP_WRITE, 0, 0, sizeof(SwoftyTelemetryFeed)))
                        : nullptr;
        if (feed) {
            std::memset(feed, 0, sizeof *feed);
            feed->version = SWOFTY_TELEMETRY_VERSION;
            feed->size = sizeof *feed;
        }

        ops_t.version = SWOFTY_CAREER_OPS_VERSION;
        ops_t.available = e_available;
        ops_t.driver_stat = [](const char *ref, uint32_t *out) { return lookup(game->driver_stats, ref, out); };
        ops_t.team_stat = [](const char *ref, uint32_t *out) { return lookup(game->team_stats, ref, out); };
        ops_t.current_round = [](int32_t *index, int32_t *round, int32_t *rounds) {
            if (!game->round_known)
                return int(SWOFTY_ASSET_UNAVAILABLE);
            *index = game->season_index;
            *round = game->round;
            *rounds = game->rounds;
            return int(SWOFTY_ASSET_OK);
        };
        ops_t.set_seat = o_set_seat;
        ops_t.set_driver_ratings = [](uint32_t stat, const uint16_t *, const float *values, size_t count) {
            std::array<float, 4> v{};
            for (size_t i = 0; i < count && i < 4; ++i)
                v[i] = values[i];
            game->ratings[stat] = v;
            return int(SWOFTY_ASSET_OK);
        };
        ops_t.set_car_number = [](uint32_t stat, int32_t number, uint32_t) {
            for (const auto &[who, n] : game->numbers)
                if (n == number && who != stat)
                    return int(SWOFTY_REFUSED);
            game->numbers[stat] = number;
            return int(SWOFTY_ASSET_OK);
        };
        swofty::career::detail::set_ops_table(&ops_t);

        saves_t.version = SWOFTY_CAREER_SAVES_VERSION;
        saves_t.available = e_available;
        saves_t.current = [](char *id, size_t cap, size_t *len) { return put(game->career_id, id, cap, len); };
        saves_t.index = [](char *json, size_t cap, size_t *len) {
            if (game->career_index.empty())
                return int(SWOFTY_ASSET_UNAVAILABLE);
            return put(game->career_index, json, cap, len);
        };
        saves_t.need = [](const char *, const char *, const char *) {
            ++game->needs;
            return int(SWOFTY_ASSET_OK);
        };
        saves_t.data_get = [](const char *career, const char *addon, char *out, size_t cap, size_t *len, uint32_t *version) {
            ++game->data_gets;
            auto it = game->career_data.find(std::string(career) + "|" + addon);
            if (it == game->career_data.end())
                return int(SWOFTY_ASSET_INVALID);
            if (version)
                *version = 1;
            return put(it->second, out, cap, len);
        };
        saves_t.data_set = [](const char *career, const char *addon, uint32_t, SwoftyText text) {
            if (game->fail_data_set)
                return int(SWOFTY_ASSET_UNAVAILABLE);
            game->career_data[std::string(career) + "|" + addon].assign(text.data, text.size);
            ++game->data_sets;
            return int(SWOFTY_ASSET_OK);
        };
        saves_t.data_remove = [](const char *career, const char *addon) {
            return int(game->career_data.erase(std::string(career) + "|" + addon) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID);
        };
        swofty::career::detail::set_saves_table(&saves_t);

        api.version = SWOFTY_API_VERSION;
        api.log = log_;
        api.module_base = [] { return uintptr_t(0); };
        api.scan = [](const char *, const char *, uintptr_t *) { return 0; };
        api.resolve = [](const char *) { return uintptr_t(0); };
        api.register_mixins = [](const SwoftyMixin *m, size_t n) {
            for (size_t i = 0; i < n; ++i)
                if (SWOFTY_MIXIN_KIND(m[i].kind) == SWOFTY_MIXIN_ON_LOAD)
                    game->on_load.push_back(reinterpret_cast<void (*)()>(m[i].handler));
            return 1;
        };
        api.fields = [](const void *, size_t) -> void * { return nullptr; };
        api.setting = [](const char *id, uint32_t type, SwoftySettingValue *out) {
            auto it = game->options.find(id);
            if (it == game->options.end() || it->second.type != type)
                return 0;
            out->type = type;
            out->toggle = it->second.toggle;
            out->slider = it->second.slider;
            out->choice = it->second.choice.c_str();
            return 1;
        };
        api.watch_setting = [](const char *id, SwoftySettingFn fn, void *user) {
            game->watches.push_back({id, fn, user});
            return 1;
        };
        api.events = &events_t;
        api.hooks = &hooks_t;
        api.services = &services_t;
        api.storage = &storage_t;
        api.career = &career;
        api.popup = &popup_t;
        api.lang = &lang_t;
        api.telemetry = &telemetry_t;
        api.registry = registry.registry_table();
        swofty::api = &api;
    }
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    ~Game()
    {
        swofty::career::detail::set_ops_table(nullptr);
        swofty::career::detail::set_saves_table(nullptr);
        if (feed)
            UnmapViewOfFile(feed);
        if (feed_map)
            CloseHandle(feed_map);
        game = nullptr;
    }

    // ---- what the test drives

    // a lifecycle or telemetry event (events::subscribe)
    void fire(const char *name, int64_t a = 0, int64_t b = 0, const void *data = nullptr)
    {
        SwoftyEvent ev{name, frame, clock, a, b, data};
        for (size_t i = 0; i < subs.size(); ++i)
            if (subs[i].event == name)
                subs[i].fn(&ev, subs[i].user);
    }
    // a hook point (events::on<E>() and events::on("name")), with the context set by the calls below
    void hook(const char *point, void *payload = nullptr)
    {
        struct {
            uint32_t size, payload_version;
        } none{8, 1};
        if (!payload)
            payload = &none;
        for (uint32_t phase : {uint32_t(SWOFTY_PHASE_BEFORE), uint32_t(SWOFTY_PHASE_REPLACE), uint32_t(SWOFTY_PHASE_AFTER)})
            for (size_t i = 0; i < hooks.size(); ++i)
                if (hooks[i].phase == phase && hooks[i].point == point) {
                    SwoftyHookCall call{sizeof(SwoftyHookCall), phase, point, payload, 0, &ctx};
                    hooks[i].fn(hooks[i].user, &call);
                }
    }
    size_t handlers(const char *point) const
    {
        size_t n = 0;
        for (const Hook &h : hooks)
            n += h.point == point;
        for (const Sub &s : subs)
            n += s.event == point;
        return n;
    }
    // rendered frames for this long
    void frames(double seconds, double dt = 1.0 / 60.0)
    {
        for (double t = 0; t < seconds; t += dt) {
            for (size_t i = 0; i < frame_fns.size(); ++i)
                frame_fns[i].first(dt, frame_fns[i].second);
            ++frame;
            clock += dt;
        }
    }
    // the player picks an option's value (Team HQ rows, the launcher): the addon's SWOFTY_ON_CHANGE runs
    void pick(const std::string &id, const std::string &choice)
    {
        Option &o = options[id];
        o.type = SWOFTY_SETTING_CHOICE;
        o.choice = choice;
        notify(id);
    }
    void toggle(const std::string &id, bool on)
    {
        Option &o = options[id];
        o.type = SWOFTY_SETTING_TOGGLE;
        o.toggle = on ? 1 : 0;
        notify(id);
    }

    void set_mode(swofty::events::Mode m) { ctx["mode"] = swofty::to_json(m); }
    void set_career(const std::string &id, const std::string &group)
    {
        swofty::events::CareerView v;
        v.id = id;
        v.data_group = group; // the rest comes empty from the core (year and round 0)
        ctx["career"] = swofty::to_json(v);
    }
    void no_career() { ctx.erase("career"); }
    void set_session(swofty::events::SessionType type)
    {
        swofty::events::SessionView v;
        v.type = type;
        ctx["session"] = swofty::to_json(v);
    }
    void set_participants(const std::vector<swofty::events::Entrant> &list) { ctx["participants"] = swofty::to_json(list); }

    // the feed, written as the core does (the sequence is odd while it changes)
    template <class F> void write_feed(F &&change)
    {
        if (!feed)
            return;
        ++feed->seq;
        change(*feed);
        ++feed->seq;
    }

    // how many lines of the log hold this text
    static size_t logged(std::string_view text, size_t from = 0)
    {
        size_t n = 0;
        for (size_t i = from; i < log_lines.size(); ++i)
            n += log_lines[i].find(text) != std::string::npos;
        return n;
    }
    static std::string last_line(std::string_view text)
    {
        for (size_t i = log_lines.size(); i-- > 0;)
            if (log_lines[i].find(text) != std::string::npos)
                return log_lines[i];
        return {};
    }

private:
    static int e_available(void) { return 1; }
    static int e_subscribe(const char *event, SwoftyEventFn fn, void *user, uint32_t *token)
    {
        game->subs.push_back({event ? event : "", fn, user});
        if (token)
            *token = ++game->tokens;
        return SWOFTY_ASSET_OK;
    }
    static int e_on_frame(SwoftyFrameFn fn, void *user, uint32_t *token)
    {
        game->frame_fns.push_back({fn, user});
        if (token)
            *token = ++game->tokens;
        return SWOFTY_ASSET_OK;
    }
    static int e_unsubscribe(uint32_t) { return SWOFTY_ASSET_OK; }

    int attach(const char *point, uint32_t phase, SwoftyHookFn fn, void *user, uint32_t *token)
    {
        if (!fn || !point || !*point || phase > SWOFTY_PHASE_AFTER)
            return SWOFTY_ASSET_INVALID;
        hooks.push_back({point, phase, fn, user});
        if (token)
            *token = ++tokens;
        return SWOFTY_ASSET_OK;
    }

    static int put(const std::string &s, char *out, size_t cap, size_t *len)
    {
        if (len)
            *len = s.size();
        if (s.size() > cap)
            return SWOFTY_TOO_SMALL;
        std::memcpy(out, s.data(), s.size());
        return SWOFTY_ASSET_OK;
    }
    static int h_context(const void *context, const char *part, char *out, size_t cap, size_t *len)
    {
        const auto *parts = static_cast<const std::map<std::string, std::string> *>(context);
        auto it = parts ? parts->find(part ? part : "") : std::map<std::string, std::string>::const_iterator{};
        if (!parts || it == parts->end())
            return SWOFTY_ASSET_UNAVAILABLE;
        return put(it->second, out, cap, len);
    }
    static int s_read(const char *path, void *buf, size_t cap, size_t *len)
    {
        auto it = game->files.find(path);
        if (it == game->files.end())
            return SWOFTY_ASSET_INVALID;
        ++game->reads[path];
        return put(it->second, static_cast<char *>(buf), cap, len);
    }
    static int lookup(const std::map<std::string, uint32_t> &table, const char *ref, uint32_t *out)
    {
        std::string id(ref ? ref : "");
        if (id.rfind("game/", 0) == 0)
            id.erase(0, 5);
        auto it = table.find(id);
        if (it == table.end())
            return SWOFTY_ASSET_INVALID;
        *out = it->second;
        return SWOFTY_ASSET_OK;
    }
    void notify(const std::string &id)
    {
        const Option &o = options[id];
        SwoftySettingValue v{o.type, o.toggle, o.slider, o.choice.c_str()};
        for (size_t i = 0; i < watches.size(); ++i)
            if (watches[i].id == id)
                watches[i].fn(id.c_str(), &v, watches[i].user);
    }

    // The core's set_seat on the fake rows ({"driver":stat,"team":stat,"seat":n}): the driver must hold exactly one
    // row and be a race driver or a free agent (a reserve, a team but no race seat, is refused). He and the seat's
    // driver exchange rows: a race driver swaps seats with him, a free agent leaves him without a team.
    static int o_set_seat(SwoftyText request, char *report, size_t cap, size_t *len)
    {
        json::Error err;
        json::Value req = json::parse(std::string_view(request.data, request.size), &err);
        uint32_t driver = static_cast<uint32_t>(req["driver"].as_int(-1)), team = static_cast<uint32_t>(req["team"].as_int(-1));
        int seat = static_cast<int>(req["seat"].as_int(-1));
        auto refuse_it = [&](const char *why, int code) {
            ++game->seats_refused;
            game->seat_log.push_back("refused (" + std::string(why) + "): driver " + std::to_string(driver) + " -> team " +
                                     std::to_string(team) + " seat " + std::to_string(seat));
            return code;
        };
        if (!game->seats_writable)
            return refuse_it("not a Driver Career", SWOFTY_ASSET_UNAVAILABLE);
        size_t mine = SIZE_MAX, target = SIZE_MAX, count = 0;
        for (size_t off = 0; off + 0x88 <= rows.size(); off += 0x88) {
            uint32_t d = 0, t = 0;
            uint16_t pos = 0;
            std::memcpy(&d, rows.data() + off + 0x50, 4);
            std::memcpy(&t, rows.data() + off + 0x58, 4);
            std::memcpy(&pos, rows.data() + off + 0x80, 2);
            int s = (pos >> 8) ? (pos & 0xff) : -1;
            if ((d & 0xff) == 0 && (d >> 16) == driver) {
                mine = off;
                ++count;
            }
            if ((t & 0xffff0000u) != 0x3fff0000u && (t >> 16) == team && s == seat)
                target = off;
        }
        if (count != 1)
            return refuse_it("no single career row", SWOFTY_ASSET_INVALID);
        if (target == SIZE_MAX)
            return refuse_it("no such seat", SWOFTY_ASSET_INVALID);
        if (game->refuse_all || game->refuse.count(driver))
            return refuse_it("the game says no", SWOFTY_REFUSED);
        uint32_t my_team = 0, ordinal = 0, my_ordinal = 0;
        uint16_t my_pos = 0;
        std::memcpy(&my_team, rows.data() + mine + 0x58, 4);
        std::memcpy(&my_pos, rows.data() + mine + 0x80, 2);
        std::memcpy(&my_ordinal, rows.data() + mine + 0x48, 4);
        std::memcpy(&ordinal, rows.data() + target + 0x48, 4);
        bool teamless = (my_team & 0xffff0000u) == 0x3fff0000u, seated = (my_pos >> 8) != 0;
        if (my_ordinal & 0x10000000u)
            return refuse_it("the player is never moved", SWOFTY_REFUSED);
        if (!teamless && !seated)
            return refuse_it("a reserve", SWOFTY_REFUSED);
        if (mine != target && (ordinal & 0x10000000u))
            return refuse_it("the human's seat", SWOFTY_REFUSED);
        if (game->busy_after_writes > 0 && --game->busy_after_writes == 0)
            entity[0x2f8] = 1; // the game is updating its rows: a read now fails
        if (game->seats_ignored) {
            ++game->seats_ok;
            game->seat_log.push_back("ok, not applied: driver " + std::to_string(driver));
            return put("{}", report, cap, len);
        }
        if (mine != target) {
            uint32_t a = 0, b = 0;
            std::memcpy(&a, rows.data() + mine + 0x50, 4);
            std::memcpy(&b, rows.data() + target + 0x50, 4);
            std::memcpy(rows.data() + mine + 0x50, &b, 4);
            std::memcpy(rows.data() + target + 0x50, &a, 4);
        }
        ++game->seats_ok;
        game->seat_log.push_back("ok: driver " + std::to_string(driver) + " -> team " + std::to_string(team) + " seat " +
                                 std::to_string(seat));
        return put("{}", report, cap, len);
    }
};

} // namespace fake
