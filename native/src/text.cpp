#include "text.hpp"

#include <cstdio>

namespace ss::text {

namespace {

std::string fmt(const char *f, const std::string &a = {}, const std::string &b = {}, const std::string &c = {},
                const std::string &d = {})
{
    // %1 %2 %3 %4 placeholders
    std::string out;
    for (const char *p = f; *p; ++p) {
        if (*p == '%' && p[1] >= '1' && p[1] <= '4') {
            const std::string *arg = p[1] == '1' ? &a : p[1] == '2' ? &b : p[1] == '3' ? &c : &d;
            out += *arg;
            ++p;
        } else {
            out += *p;
        }
    }
    return out;
}

std::string num(int n) { return std::to_string(n); }

template <size_t N> const char *choose(const char *const (&list)[N], int pick)
{
    return list[static_cast<size_t>(pick < 0 ? -pick : pick) % N];
}

// capitals, also for the accented Latin letters in UTF-8 (é -> É)
std::string upper(std::string_view s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'a' && c <= 'z') {
            out[i] = static_cast<char>(c - 32);
        } else if (c == 0xC3 && i + 1 < out.size()) {
            unsigned char n = static_cast<unsigned char>(out[i + 1]);
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7)
                out[i + 1] = static_cast<char>(n - 0x20);
            ++i;
        }
    }
    return out;
}

// a driver in a headline: his surname, in capitals
std::string who(const std::string &name)
{
    size_t sp = name.find_last_of(' ');
    return upper(sp == std::string::npos ? name : name.substr(sp + 1));
}

std::string where(const std::string &team) { return upper(team); }

} // namespace

std::string h_rumour(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {"¿%1 A %2?", "%2 SIGUE A %1", "%1, EN LA LISTA DE %2", "¿%2 VA A POR %1?"};
    static const char *const en[] = {"%1 TO %2?", "%2 EYE %1", "%1 LINKED WITH %2", "%2 MOVE FOR %1?"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver), where(team));
}

std::string h_talks(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {"%1 NEGOCIA CON %2", "%2 Y %1 YA HABLAN", "AVANZAN LAS CHARLAS %1-%2"};
    static const char *const en[] = {"%1 IN TALKS WITH %2", "%2 AND %1 TALKING", "%1-%2 TALKS MOVE ON"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver), where(team));
}

std::string h_signing(Lang l, int pick, const std::string &driver, const std::string &team, bool bombshell)
{
    if (bombshell)
        return fmt(l.es ? "BOMBAZO: %1 FICHA POR %2" : "BOMBSHELL: %1 TO %2", who(driver), where(team));
    static const char *const es[] = {"OFICIAL: %1 FICHA POR %2", "%2 FICHA A %1", "HECHO: %1 A %2"};
    static const char *const en[] = {"OFFICIAL: %1 JOINS %2", "%2 SIGN %1", "IT'S DONE: %1 TO %2"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver), where(team));
}

std::string h_renewal(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {"%1 SE QUEDA EN %2", "%2 RENUEVA A %1", "%1 SIGUE CON %2"};
    static const char *const en[] = {"%1 STAYS AT %2", "%2 RENEW %1", "%1 COMMITS TO %2"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver), where(team));
}

std::string h_renewals(Lang l, int count)
{
    if (count == 1)
        return l.es ? "UNA RENOVACIÓN DE ÚLTIMA HORA" : "A LATE RENEWAL";
    return fmt(l.es ? "%1 RENOVACIONES DE ÚLTIMA HORA" : "%1 LATE RENEWALS", num(count));
}

std::string h_collapse(Lang l, int pick, const std::string &driver, const std::string &team, bool fake)
{
    static const char *const es_fake[] = {"¿%1 A %2? NADA DE NADA", "%2 DESMIENTE LO DE %1"};
    static const char *const en_fake[] = {"%1 TO %2? NOT SO FAST", "%2 DENY %1 STORY"};
    static const char *const es[] = {"SE ENFRÍA EL %1-%2", "%1 Y %2 ROMPEN LAS CHARLAS"};
    static const char *const en[] = {"%1-%2 TALKS COOL OFF", "%1 DEAL WITH %2 IS OFF"};
    const char *f = fake ? (l.es ? choose(es_fake, pick) : choose(en_fake, pick)) : (l.es ? choose(es, pick) : choose(en, pick));
    return fmt(f, who(driver), where(team));
}

std::string h_left(Lang l, const std::string &driver)
{
    return fmt(l.es ? "%1 SE QUEDA SIN ASIENTO" : "%1 OFF THE GRID", who(driver));
}

std::string h_retirement(Lang l, int pick, const std::string &driver)
{
    static const char *const es[] = {"%1 SE RETIRA", "%1 DICE ADIÓS", "%1 ANUNCIA SU RETIRADA"};
    static const char *const en[] = {"%1 TO RETIRE", "%1 CALLS IT A DAY", "%1 ANNOUNCES RETIREMENT"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver));
}

std::string h_sacking(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {"%2 DESPIDE A %1", "%1, FUERA DE %2"};
    static const char *const en[] = {"%2 SACK %1", "%1 OUT AT %2"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver), where(team));
}

std::string h_pressure(Lang l, int pick, const std::string &driver)
{
    static const char *const es[] = {"%1, BAJO PRESIÓN", "SE CALIENTA EL ASIENTO DE %1"};
    static const char *const en[] = {"%1 UNDER PRESSURE", "HOT SEAT FOR %1"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), who(driver));
}

std::string h_player_interest(Lang l, int pick, const std::string &team)
{
    static const char *const es[] = {"%1 TE SIGUE LA PISTA", "%1 PREGUNTA POR TI"};
    static const char *const en[] = {"%1 WATCHING YOU", "%1 ASK ABOUT YOU"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), where(team));
}

std::string h_player_renewal(Lang l, int pick, const std::string &team)
{
    static const char *const es[] = {"%1 QUIERE RENOVARTE", "SE HABLA DE TU RENOVACIÓN EN %1"};
    static const char *const en[] = {"%1 WANT TO KEEP YOU", "NEW DEAL TALK AT %1"};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), where(team));
}

std::string h_standin(Lang l, const std::string &in, const std::string &out)
{
    return fmt(l.es ? "%1 SUSTITUYE A %2" : "%1 STANDS IN FOR %2", who(in), who(out));
}

std::string h_standin_return(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "%1 VUELVE AL %2" : "%1 BACK AT %2", who(driver), where(team));
}

std::string h_staff(Lang l, const std::string &director, const std::string &team)
{
    return fmt(l.es ? "%1 SE VA A %2" : "%1 MOVES TO %2", who(director), where(team));
}

std::string h_feud(Lang l, const std::string &a, const std::string &b)
{
    return fmt(l.es ? "%1 CONTRA %2" : "%1 VS %2", who(a), who(b));
}

std::string h_feud_exit(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "%1 QUIERE SALIR DE %2" : "%1 WANTS OUT OF %2", who(driver), where(team));
}

std::string h_clause(Lang l, const std::string &driver)
{
    return fmt(l.es ? "SALTA LA CLÁUSULA DE %1" : "%1'S EXIT CLAUSE KICKS IN", who(driver));
}

std::string h_press(Lang l, const std::string &topic, const std::string &about, bool reply)
{
    if (topic == "future")
        return reply ? (l.es ? "HABLAS DE TU FUTURO" : "YOU ON YOUR FUTURE")
                     : (l.es ? "LA PRENSA PREGUNTA POR TU FUTURO" : "THE PRESS ASK ABOUT YOUR FUTURE");
    if (topic == "wish")
        return reply ? (l.es ? "ELIGES COMPAÑERO" : "YOU NAME YOUR TEAM-MATE")
                     : (l.es ? "¿QUIÉN A TU LADO? PREGUNTA LA PRENSA" : "WHO NEXT TO YOU? THE PRESS ASK");
    return fmt(reply ? (l.es ? "HABLAS DE %1" : "YOU ON %1") : (l.es ? "LA PRENSA PREGUNTA POR %1" : "THE PRESS ASK ABOUT %1"),
               who(about));
}

std::string h_press_silence(Lang l) { return l.es ? "SIN COMENTARIOS" : "NO COMMENT"; }

std::string t_rumour(Lang l) { return l.es ? "RUMOR DE MERCADO" : "MARKET RUMOUR"; }
std::string t_talks(Lang l) { return l.es ? "NEGOCIACIONES EN MARCHA" : "TALKS UNDER WAY"; }
std::string t_official(Lang l) { return l.es ? "OFICIAL" : "OFFICIAL"; }
std::string t_retirement(Lang l) { return l.es ? "ANUNCIO DE RETIRADA" : "RETIREMENT ANNOUNCED"; }
std::string t_sacking(Lang l) { return l.es ? "CAMBIO A MITAD DE TEMPORADA" : "MID-SEASON CHANGE"; }
std::string t_collapse(Lang l) { return l.es ? "MERCADO" : "MARKET"; }
std::string t_bombshell(Lang l) { return l.es ? "BOMBAZO EN EL PADDOCK" : "PADDOCK BOMBSHELL"; }
std::string t_grid(Lang l, int year) { return (l.es ? "PARRILLA " : "GRID ") + num(year); }
std::string t_season(Lang l, int year) { return (l.es ? "TEMPORADA " : "SEASON ") + num(year); }
std::string t_paddock(Lang l, size_t count)
{
    return (l.es ? "SILLY SEASON - NOTICIAS (" : "SILLY SEASON - NEWS (") + std::to_string(count) + ")";
}

std::string rumour(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &from)
{
    static const char *const es[] = {
        "Fuentes del paddock aseguran que %1 ha mantenido contactos con %2 de cara a la próxima temporada.",
        "%2 tiene a %1 en lo más alto de su lista para el año que viene, según varios medios.",
        "El entorno de %1 no descarta nada: %2 aparece como destino para la próxima temporada.",
        "Se vio al mánager de %1 en el motorhome de %2. El paddock ya habla de un posible fichaje.",
    };
    static const char *const en[] = {
        "Paddock sources say %1 has held talks with %2 about next season.",
        "%2 have %1 at the top of their list for next year, according to several reports.",
        "%1's camp rules nothing out: %2 is mentioned as a destination for next season.",
        "%1's manager was seen in the %2 motorhome. The paddock is already talking about a move.",
    };
    std::string s = fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
    if (!from.empty())
        s += fmt(l.es ? " Su futuro en %1 queda en el aire." : " His future at %1 is now in doubt.", from);
    return s;
}

std::string fake_rumour(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {
        "Un medio sensacionalista sitúa a %1 en %2 la próxima temporada. Nadie lo ha confirmado.",
        "Rumor sin confirmar: %2 estaría tanteando a %1.",
        "Las redes arden con una supuesta reunión entre %1 y %2. Ninguna de las partes comenta.",
    };
    static const char *const en[] = {
        "A tabloid places %1 at %2 next season. Nobody has confirmed it.",
        "Unconfirmed rumour: %2 are said to be sounding out %1.",
        "Social media is buzzing about an alleged meeting between %1 and %2. Neither side comments.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
}

std::string talks(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {
        "Las conversaciones entre %1 y %2 avanzan: ya se discuten la duración y las cláusulas del contrato.",
        "%2 y %1 estarían cerca de un acuerdo. Falta la firma.",
        "Según el paddock, %1 ya ha visitado la fábrica de %2. El acuerdo parece encaminado.",
    };
    static const char *const en[] = {
        "Talks between %1 and %2 are moving forward: length and clauses are on the table.",
        "%2 and %1 are said to be close to an agreement. Only the signature is missing.",
        "According to the paddock, %1 has already visited the %2 factory. A deal looks on track.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
}

std::string collapse(Lang l, int pick, const std::string &driver, const std::string &team, bool fake)
{
    if (fake) {
        static const char *const es[] = {"%2 desmiente cualquier contacto con %1.",
                                         "%1 se ríe del rumor: \"No hay nada con %2\"."};
        static const char *const en[] = {"%2 deny any contact with %1.",
                                         "%1 laughs off the rumour: \"There is nothing with %2\"."};
        return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
    }
    static const char *const es[] = {"Se enfrían las negociaciones entre %1 y %2.",
                                     "%2 cambia de planes y se aleja de %1.",
                                     "Las cifras no cuadran: %1 y %2 rompen las conversaciones."};
    static const char *const en[] = {"Talks between %1 and %2 have cooled off.",
                                     "%2 change their plans and move away from %1.",
                                     "The numbers don't add up: %1 and %2 break off talks."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
}

std::string signing(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &from,
                    int until, const std::string &kind)
{
    std::string years = num(until);
    if (kind == "promotion") {
        static const char *const es[] = {"%2 confirma el ascenso de %1 a la Fórmula 1. Contrato hasta %3.",
                                         "¡Debutante! %1 pilotará para %2 con un contrato hasta %3."};
        static const char *const en[] = {"%2 confirm %1's promotion to Formula 1. Contract until %3.",
                                         "Rookie alert! %1 will race for %2 on a deal until %3."};
        return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team, years);
    }
    if (kind == "comeback") {
        return fmt(l.es ? "¡Vuelve! %1 regresa a la parrilla con %2 hasta %3."
                        : "He's back! %1 returns to the grid with %2 until %3.",
                   driver, team, years);
    }
    static const char *const es[] = {"%1 firma con %2 hasta %3. Deja %4 al final de temporada.",
                                     "Es oficial: %1 será piloto de %2 hasta %3, tras su etapa en %4.",
                                     "%2 anuncia el fichaje de %1 (procedente de %4) hasta %3."};
    static const char *const en[] = {"%1 signs for %2 until %3. He leaves %4 at the end of the season.",
                                     "It's official: %1 will drive for %2 until %3, after his time at %4.",
                                     "%2 announce the signing of %1 (from %4) until %3."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team, years, from.empty() ? "-" : from);
}

std::string renewal(Lang l, int pick, const std::string &driver, const std::string &team, int until)
{
    static const char *const es[] = {"%1 renueva con %2 hasta %3.", "%2 blinda a %1: contrato ampliado hasta %3."};
    static const char *const en[] = {"%1 extends with %2 until %3.", "%2 lock in %1: contract extended until %3."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team, num(until));
}

std::string renewals_summary(Lang l, const std::string &list)
{
    return (l.es ? "También siguen en sus equipos: " : "Also staying put: ") + list + ".";
}

std::string retirement(Lang l, int pick, const std::string &driver, int age, int seasons)
{
    static const char *const es[] = {
        "%1 (%2 años) anuncia que se retirará de la Fórmula 1 al final de la temporada.",
        "Fin de una era: %1 colgará el casco tras esta temporada, con %3 temporadas en la categoría.",
    };
    static const char *const en[] = {
        "%1 (%2) announces he will retire from Formula 1 at the end of the season.",
        "End of an era: %1 will hang up his helmet after this season, after %3 seasons in F1.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, num(age), num(seasons));
}

std::string sacking(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &in)
{
    static const char *const es[] = {
        "%2 prescinde de %1 con efecto inmediato. %3 ocupará el asiento desde la próxima carrera.",
        "Sin paciencia en %2: %1 pierde su asiento y %3 toma el relevo ya.",
    };
    static const char *const en[] = {
        "%2 part ways with %1 with immediate effect. %3 takes the seat from the next race.",
        "No patience at %2: %1 loses his seat and %3 steps in straight away.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team, in);
}

std::string season_start(Lang l, int year, int newcomers, const std::string &list)
{
    if (newcomers == 0)
        return fmt(l.es ? "Arranca la temporada %1 sin cambios en la parrilla." : "Season %1 starts with an unchanged grid.",
                   num(year));
    return fmt(l.es ? "Arranca la temporada %1 con %2 cambios en la parrilla: %3."
                    : "Season %1 starts with %2 changes on the grid: %3.",
               num(year), num(newcomers), list);
}

std::string left_grid(Lang l, const std::string &driver)
{
    return fmt(l.es ? "%1 se queda sin asiento para la próxima temporada." : "%1 is left without a seat for next season.",
               driver);
}

const char *outlet_name(int i)
{
    static const char *const names[kOutlets] = {"Grid Gossip", "Box Box Daily", "Full Throttle", "Paddock Confidential",
                                                "The Pit Wall"};
    return names[(i % kOutlets + kOutlets) % kOutlets];
}

int outlet_reliability(int i) { return (i % kOutlets + kOutlets) % kOutlets + 1; }

std::string outlet_label(Lang l, int i)
{
    int r = outlet_reliability(i);
    const char *level = l.es ? (r >= 4 ? "fiabilidad alta" : r == 3 ? "fiabilidad media" : "fiabilidad baja")
                             : (r >= 4 ? "high reliability" : r == 3 ? "medium reliability" : "low reliability");
    return std::string(outlet_name(i)) + " (" + level + ")";
}

std::string boss_quote(Lang l, int pick, const std::string &driver, const std::string &team, const std::string &kind)
{
    if (kind == "sacking") {
        static const char *const es[] = {"\"No ha sido una decisión fácil, pero necesitamos resultados ya\", explican desde la dirección de %2.",
                                         "\"Agradecemos a %1 su trabajo y le deseamos lo mejor\", señala el comunicado de %2."};
        static const char *const en[] = {"\"It wasn't an easy decision, but we need results now\", say %2's management.",
                                         "\"We thank %1 for his work and wish him the best\", reads %2's statement."};
        return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
    }
    if (kind == "promotion") {
        static const char *const es[] = {"\"%1 se ha ganado esta oportunidad. Es el momento de dar el salto\", afirman en %2.",
                                         "\"Llevamos tiempo siguiendo a %1 y estamos convencidos de su talento\", explica el director de %2."};
        static const char *const en[] = {"\"%1 has earned this chance. It's time for the step up\", say %2.",
                                         "\"We've followed %1 for a long time and we believe in his talent\", says %2's team principal."};
        return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
    }
    static const char *const es[] = {"\"Estamos encantados de dar la bienvenida a %1. Encaja con nuestro proyecto\", declara el director de %2.",
                                     "\"Su experiencia nos hará crecer\", aseguran desde %2.",
                                     "\"Es un fichaje que dice mucho de hacia dónde va %2\", celebra la dirección del equipo."};
    static const char *const en[] = {"\"We're delighted to welcome %1. He fits our project\", says %2's team principal.",
                                     "\"His experience will help us grow\", say %2.",
                                     "\"This signing says a lot about where %2 is heading\", says the team's management."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
}

std::string salary_line(Lang l, float millions)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.0f", millions);
    return fmt(l.es ? "Se habla de un salario de unos %1 millones por temporada." : "The salary is said to be around %1 million a season.",
               buf);
}

std::string salary_short(Lang l, float millions)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, millions < 1.5f ? "%.1f M" : "%.0f M", millions);
    return std::string(buf) + (l.es ? "€" : "€");
}

std::string t_standin(Lang l) { return l.es ? "BAJA DE ÚLTIMA HORA" : "LATE WITHDRAWAL"; }

std::string standin(Lang l, int pick, const std::string &out, const std::string &team, const std::string &in, int race)
{
    static const char *const es_reason[] = {"una intoxicación alimentaria", "una apendicitis", "una lesión en la muñeca",
                                            "un fuerte golpe en el simulador", "una gripe"};
    static const char *const en_reason[] = {"food poisoning", "appendicitis", "a wrist injury", "a heavy crash in the simulator",
                                            "flu"};
    std::string reason = l.es ? choose(es_reason, pick) : choose(en_reason, pick);
    return fmt(l.es ? "%1 se perderá la carrera %4 por %3. " : "%1 will miss round %4 with %3. ", out, team, reason,
               num(race)) +
           fmt(l.es ? "%1 confía el coche a %2." : "%1 hand the car to %2.", team, in);
}

std::string standin_return(Lang l, const std::string &out, const std::string &team)
{
    return fmt(l.es ? "%1 está recuperado y vuelve a su %2." : "%1 has recovered and is back in his %2.", out, team);
}

std::string t_review(Lang l, int year) { return (l.es ? "BALANCE DEL MERCADO " : "MARKET REVIEW ") + num(year); }

std::string review(Lang l, const std::string &signing, const std::string &revelation, const std::string &leap,
                   const std::string &retired, int moves, int rookies)
{
    std::string out = l.es ? num(moves) + (moves == 1 ? " cambio de asiento y " : " cambios de asiento y ") + num(rookies) +
                                 (rookies == 1 ? " debutante." : " debutantes.")
                           : num(moves) + (moves == 1 ? " seat change and " : " seat changes and ") + num(rookies) +
                                 (rookies == 1 ? " rookie." : " rookies.");
    if (!signing.empty())
        out += "\n" + std::string(l.es ? "Fichaje del año: " : "Signing of the year: ") + signing;
    if (!leap.empty())
        out += "\n" + std::string(l.es ? "Mayor salto: " : "Biggest step up: ") + leap;
    if (!revelation.empty())
        out += "\n" + std::string(l.es ? "Apuesta de futuro: " : "One for the future: ") + revelation;
    if (!retired.empty())
        out += "\n" + std::string(l.es ? "Se despiden: " : "Farewells: ") + retired;
    return out;
}

std::string t_f2(Lang l) { return l.es ? "FÓRMULA 2" : "FORMULA 2"; }
std::string f2_leader(Lang l, const std::string &driver, int age)
{
    return fmt(l.es ? "%1 (%2 años) lidera la F2 a mitad de temporada. Varios equipos de F1 ya preguntan por él."
                    : "%1 (%2) leads F2 at half distance. Several F1 teams are already asking about him.",
               driver, num(age));
}
std::string f2_champion(Lang l, const std::string &champion, const std::string &second, const std::string &third)
{
    return fmt(l.es ? "%1 se proclama campeón de F2. Completan el podio %2 y %3. La superlicencia ya está en el bolsillo."
                    : "%1 is F2 champion, ahead of %2 and %3. The superlicence is in the bag.",
               champion, second, third);
}
std::string h_f2(Lang l, int year) { return (l.es ? "CAMPEONATO DE F2 " : "F2 CHAMPIONSHIP ") + num(year); }
std::string t_staff(Lang l) { return l.es ? "FICHAJE TÉCNICO" : "TECHNICAL SIGNING"; }
std::string staff_move(Lang l, int pick, const std::string &name, const std::string &from, const std::string &to)
{
    static const char *const es[] = {"%1, director técnico de %2, se marcha a %3. El coche del año que viene ya lleva su firma.",
                                     "Golpe en los despachos: %3 se lleva a %1, el cerebro técnico de %2."};
    static const char *const en[] = {"%1, %2's technical director, is off to %3. Next year's car will carry his signature.",
                                     "Boardroom coup: %3 lure %1, %2's technical mastermind."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), name, from, to);
}
std::string staff_hire(Lang l, const std::string &name, const std::string &team, const std::string &gone)
{
    return fmt(l.es ? "%2 nombra a %1 nuevo director técnico tras la marcha de %3."
                    : "%2 appoint %1 as technical director after %3's departure.",
               name, team, gone);
}
std::string h_staff(Lang l) { return l.es ? "DIRECTORES TÉCNICOS" : "TECHNICAL DIRECTORS"; }
std::string backing_line(Lang l, float millions)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.0f", millions);
    return fmt(l.es ? "Llega con el respaldo de patrocinadores personales por valor de %1 millones."
                    : "He brings personal sponsors worth %1 million.",
               buf);
}
std::string clause_line(Lang l, int rank)
{
    return fmt(l.es ? "El contrato incluye una cláusula de salida si el equipo no acaba entre los %1 primeros."
                    : "The contract has an exit clause if the team finishes outside the top %1.",
               num(rank));
}
std::string t_clause(Lang l) { return l.es ? "CLÁUSULA DE SALIDA" : "EXIT CLAUSE"; }
std::string clause_triggered(Lang l, const std::string &driver, const std::string &team, int rank)
{
    return fmt(l.es ? "%2 no acabará entre los %3 primeros: %1 puede activar su cláusula y quedar libre."
                    : "%2 won't finish in the top %3: %1 can trigger his exit clause and walk away.",
               driver, team, num(rank));
}
std::string t_feud(Lang l) { return l.es ? "TENSIÓN EN EL BOX" : "GARAGE TENSION"; }
std::string feud(Lang l, int pick, const std::string &a, const std::string &b, const std::string &team)
{
    static const char *const es[] = {"Ambiente irrespirable en %3: %1 y %2 ni se miran después del último toque en pista.",
                                     "%1 acusa a %2 de no respetar las órdenes de equipo. %3 intenta calmar las aguas.",
                                     "La relación entre %1 y %2 está rota. En %3 temen que acabe afectando a los resultados."};
    static const char *const en[] = {"The air is thick at %3: %1 and %2 aren't talking after their last on-track clash.",
                                     "%1 accuses %2 of ignoring team orders. %3 try to calm things down.",
                                     "The relationship between %1 and %2 has broken down. %3 fear it will hurt results."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), a, b, team);
}
std::string feud_exit(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "%1 pide salir de %2 a final de temporada: la convivencia ya no es posible."
                    : "%1 asks to leave %2 at the end of the season: the partnership is over.",
               driver, team);
}
std::string t_talents(Lang l, int year) { return (l.es ? "NUEVAS PROMESAS " : "NEW TALENTS ") + num(year); }
std::string talents(Lang l, const std::string &list)
{
    return (l.es ? "Llegan a la F2 los jóvenes que darán que hablar: " : "The youngsters everyone will talk about join F2: ") +
           list + ".";
}

std::string t_press(Lang l) { return l.es ? "RUEDA DE PRENSA" : "PRESS CONFERENCE"; }
std::string q_future(Lang l, const std::string &team)
{
    return fmt(l.es ? "\"¿Te ves en %1 la próxima temporada?\"" : "\"Do you see yourself at %1 next season?\"", team);
}
std::string q_mate(Lang l, const std::string &mate, const std::string &team)
{
    return fmt(l.es ? "\"Se dice que tu compañero %1 negocia con %2. ¿Qué te parece?\""
                    : "\"Word is your team-mate %1 is talking to %2. What do you make of it?\"",
               mate, team);
}
std::string q_wish(Lang l, const std::string &team)
{
    return fmt(l.es ? "\"Si hubiera un asiento libre en %1, ¿a quién querrías de compañero?\""
                    : "\"If a seat opened at %1, who would you want as a team-mate?\"",
               team);
}
std::string q_signing(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "\"¿Qué opinas del fichaje de %1 por %2?\"" : "\"What do you think of %1 signing for %2?\"", driver, team);
}
std::string a_future(Lang l, int i, const std::string &team)
{
    static const char *const es[] = {"\"Estoy comprometido con %1 al cien por cien.\"",
                                     "\"Escucharé todas las ofertas. Quiero ganar.\"", "\"Ahora solo pienso en la próxima carrera.\""};
    static const char *const en[] = {"\"I'm one hundred percent committed to %1.\"", "\"I'll listen to every offer. I want to win.\"",
                                     "\"Right now I'm only thinking about the next race.\""};
    return fmt(l.es ? es[i % 3] : en[i % 3], team);
}
std::string a_mate(Lang l, int i)
{
    static const char *const es[] = {"\"Le deseo lo mejor, decida lo que decida.\"", "\"Si quiere irse, que se vaya ya.\"",
                                     "\"Prefiero hablar de la carrera.\""};
    static const char *const en[] = {"\"I wish him the best, whatever he decides.\"", "\"If he wants to go, he should go now.\"",
                                     "\"I'd rather talk about the race.\""};
    return l.es ? es[i % 3] : en[i % 3];
}
std::string a_signing(Lang l, int i)
{
    static const char *const es[] = {"\"Gran fichaje. Le dará mucho al equipo.\"", "\"Me parece un fichaje sobrevalorado.\"",
                                     "\"No me preocupa lo que hagan los demás.\""};
    static const char *const en[] = {"\"A great signing. He'll bring a lot to the team.\"", "\"I think he's overrated.\"",
                                     "\"I don't worry about what others do.\""};
    return l.es ? es[i % 3] : en[i % 3];
}
std::string a_none(Lang l) { return l.es ? "\"No es mi decisión.\"" : "\"It's not my decision.\""; }
std::string r_future(Lang l, int i, const std::string &team)
{
    static const char *const es[] = {"Compromiso total: el piloto de %1 descarta cualquier salida. La dirección del equipo, encantada.",
                                     "El piloto de %1 deja la puerta abierta: \"escucharé ofertas\". El paddock toma nota.",
                                     "El piloto de %1 esquiva las preguntas sobre su futuro."};
    static const char *const en[] = {"Fully committed: %1's driver rules out any move. The team's management is delighted.",
                                     "%1's driver leaves the door open: \"I'll listen to offers\". The paddock takes note.",
                                     "%1's driver sidesteps questions about his future."};
    return fmt(l.es ? es[i % 3] : en[i % 3], team);
}
std::string r_mate(Lang l, int i, const std::string &mate)
{
    static const char *const es[] = {"Deportividad en el box: buenas palabras para %1 pese a los rumores.",
                                     "Guerra abierta en el box: \"si quiere irse, que se vaya ya\". %1 no ha tardado en responder.",
                                     "Silencio sobre el futuro de %1."};
    static const char *const en[] = {"Sportsmanship in the garage: kind words for %1 despite the rumours.",
                                     "Open war in the garage: \"if he wants to go, he should go now\". %1 was quick to reply.",
                                     "Silence on %1's future."};
    return fmt(l.es ? es[i % 3] : en[i % 3], mate);
}
std::string r_wish(Lang l, const std::string &driver)
{
    return fmt(l.es ? "Petición pública: quiere a %1 como compañero. El equipo lo tendrá en cuenta."
                    : "A public request: he wants %1 as his team-mate. The team will bear it in mind.",
               driver);
}
std::string r_signing(Lang l, int i, const std::string &driver)
{
    static const char *const es[] = {"Elogios para %1 en la rueda de prensa.", "Polémica: llama \"sobrevalorado\" a %1, que no se ha callado.",
                                     "Indiferencia ante el fichaje de %1."};
    static const char *const en[] = {"Praise for %1 in the press conference.", "Controversy: he calls %1 \"overrated\", and %1 hit back.",
                                     "Indifference towards %1's move."};
    return fmt(l.es ? es[i % 3] : en[i % 3], driver);
}
std::string r_silence(Lang l)
{
    return l.es ? "Se negó a contestar a la prensa sobre el mercado. Los periodistas no se lo han tomado bien."
                : "He refused to answer the press about the market. The journalists didn't like it.";
}

std::string t_pressure(Lang l) { return l.es ? "BAJO PRESIÓN" : "UNDER PRESSURE"; }
std::string t_player(Lang l) { return l.es ? "TU FUTURO" : "YOUR FUTURE"; }
std::string t_preseason(Lang l, int year) { return (l.es ? "MERCADO " : "MARKET ") + num(year) + (l.es ? ": ASIENTOS EN JUEGO" : ": SEATS IN PLAY"); }

std::string pressure(Lang l, int pick, const std::string &driver, const std::string &team)
{
    static const char *const es[] = {
        "Crecen las dudas en %2 sobre %1: su compañero le está superando con claridad.",
        "%1 necesita resultados ya. En %2 empiezan a mirar alternativas.",
        "Fin de semana complicado para %1. La paciencia en %2 tiene un límite.",
    };
    static const char *const en[] = {
        "Doubts are growing at %2 over %1: his team-mate is clearly ahead.",
        "%1 needs results now. %2 are starting to look at alternatives.",
        "A tough weekend for %1. Patience at %2 has its limits.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team);
}

std::string player_interest(Lang l, int pick, const std::string &team)
{
    static const char *const es[] = {
        "Según el paddock, %1 sigue muy de cerca tu evolución de cara al año que viene.",
        "Tu nombre suena en %1. Tu mánager ha recibido una primera llamada.",
        "%1 te ha incluido en su lista corta para la próxima temporada.",
    };
    static const char *const en[] = {
        "According to the paddock, %1 are following your progress closely for next year.",
        "Your name is doing the rounds at %1. Your manager has had a first call.",
        "%1 have put you on their shortlist for next season.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), team);
}

std::string player_renewal(Lang l, int pick, const std::string &team)
{
    static const char *const es[] = {"%1 quiere ampliar tu contrato. La dirección del equipo está contenta contigo.",
                                     "En %1 hablan de renovarte: estás convenciendo a todo el mundo."};
    static const char *const en[] = {"%1 want to extend your contract. The team's management is pleased with you.",
                                     "Talk at %1 is of a new deal for you: you're winning everyone over."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), team);
}

std::string preseason(Lang l, int year, int count, const std::string &list)
{
    if (count == 0)
        return fmt(l.es ? "Todos los pilotos tienen contrato más allá de %1. Mercado tranquilo... de momento."
                        : "Every driver is signed beyond %1. A quiet market... for now.",
                   num(year));
    return fmt(l.es ? "%2 pilotos acaban contrato en %1: %3. Empieza la silly season."
                    : "%2 drivers are out of contract after %1: %3. The silly season begins.",
               num(year), num(count), list);
}

std::string mail_preview_subject(Lang l, int round)
{
    return round <= 1 ? (l.es ? "Silly Season: arranca el mercado" : "Silly Season: the market opens")
                      : (l.es ? "Silly Season: previa del mercado" : "Silly Season: market preview");
}
std::string mail_radar_subject(Lang l) { return l.es ? "Silly Season: radar del mercado" : "Silly Season: market radar"; }
std::string mail_grid_subject(Lang l, int year)
{
    return (l.es ? "Silly Season: así queda la parrilla " : "Silly Season: the ") + num(year) +
           (l.es ? "" : " grid");
}
std::string mail_intro(Lang l)
{
    return l.es ? "Esto es lo que se mueve en el paddock:" : "Here is what's moving in the paddock:";
}
std::string mail_quiet(Lang l)
{
    return l.es ? "Semana tranquila en el mercado. Nadie mueve ficha... todavía."
                : "A quiet week on the market. Nobody is making a move... yet.";
}
// no personal name: the game shows the mail under its own contact (its name depends on the game's language)
std::string mail_sign(Lang l) { return l.es ? "— Redacción de Silly Season" : "— The Silly Season desk"; }
std::string h_negotiations(Lang l) { return l.es ? "NEGOCIACIONES ABIERTAS" : "OPEN TALKS"; }
std::string h_stock(Lang l) { return l.es ? "BOLSA DE PILOTOS" : "DRIVER STOCK"; }
std::string h_up(Lang l) { return l.es ? "Suben" : "Rising"; }
std::string h_down(Lang l) { return l.es ? "Bajan" : "Falling"; }
std::string h_pressure(Lang l) { return l.es ? "BAJO PRESIÓN" : "UNDER PRESSURE"; }
std::string h_expiring(Lang l, int year) { return (l.es ? "ACABAN CONTRATO EN " : "OUT OF CONTRACT IN ") + num(year); }
std::string h_moves(Lang l, int year) { return (l.es ? "MOVIMIENTOS DE " : "MOVES OF ") + num(year); }
std::string h_news(Lang l) { return l.es ? "ÚLTIMAS NOTICIAS" : "LATEST NEWS"; }

std::string h_rumours(Lang l) { return l.es ? "RUMORES Y NEGOCIACIONES" : "RUMOURS AND TALKS"; }
std::string h_deals(Lang l, int year) { return (l.es ? "FICHAJES CERRADOS PARA " : "DEALS DONE FOR ") + num(year); }
std::string h_contracts(Lang l, int year) { return (l.es ? "CONTRATOS (TEMPORADA " : "CONTRACTS (SEASON ") + num(year) + ")"; }
std::string h_grid(Lang l, int year, bool final)
{
    return (l.es ? (final ? "PARRILLA DEFINITIVA " : "PARRILLA PREVISTA ") : (final ? "FINAL GRID " : "PROJECTED GRID ")) +
           num(year);
}
std::string h_history(Lang l) { return l.es ? "HISTORIAL DE MOVIMIENTOS" : "TRANSFER HISTORY"; }
std::string h_market(Lang l) { return l.es ? "AGENTES LIBRES Y CANTERA" : "FREE AGENTS AND JUNIORS"; }
std::string none(Lang l) { return l.es ? "Nada por ahora." : "Nothing yet."; }

std::string stage(Lang l, const char *s)
{
    std::string k(s);
    if (k == "rumour")
        return l.es ? "rumor" : "rumour";
    if (k == "talks")
        return l.es ? "negociando" : "talks";
    if (k == "agreed")
        return l.es ? "acuerdo cerca" : "deal close";
    if (k == "official")
        return l.es ? "oficial" : "official";
    return l.es ? "descartado" : "off";
}

std::string kind(Lang l, const std::string &k)
{
    if (k == "transfer")
        return l.es ? "fichaje" : "transfer";
    if (k == "renewal")
        return l.es ? "renovación" : "renewal";
    if (k == "promotion")
        return l.es ? "ascenso" : "promotion";
    if (k == "comeback")
        return l.es ? "regreso" : "comeback";
    if (k == "retirement")
        return l.es ? "retirada" : "retirement";
    if (k == "sacked")
        return l.es ? "despedido" : "sacked";
    if (k == "replaced")
        return l.es ? "sustituto" : "stand-in";
    if (k == "left")
        return l.es ? "sin asiento" : "no seat";
    return k;
}

std::string free_seat(Lang l) { return l.es ? "(libre)" : "(open)"; }
std::string unknown_seat(Lang l) { return l.es ? "¿?" : "?"; }

} // namespace ss::text
