#include "names.hpp"

#include <cctype>
#include <string_view>

namespace ss::names {

namespace {

enum Group { es, it, fr, de, br, en, nl, nordic, jp, kGroups };

Group group_of(std::string_view nat)
{
    for (auto n : {"spain", "mexico", "argentina", "columbia", "colombia", "peru", "uruguay", "chile", "venezuela", "paraguay"})
        if (nat == n)
            return es;
    if (nat == "italy" || nat == "san_marino")
        return it;
    for (auto n : {"france", "monaco", "belgium", "switzerland_fr"})
        if (nat == n)
            return fr;
    for (auto n : {"germany", "austria", "switzerland", "poland", "czech_republic", "liechtenstein"})
        if (nat == n)
            return de;
    if (nat == "brazil" || nat == "portugal")
        return br;
    if (nat == "netherlands")
        return nl;
    for (auto n : {"finland", "denmark", "sweden", "norway", "estonia", "iceland"})
        if (nat == n)
            return nordic;
    if (nat == "japan")
        return jp;
    return en;
}

struct Pool {
    const char *male[8];
    const char *female[6];
    const char *vowel_endings[6];     // after a prefix that ends in a consonant
    const char *consonant_endings[6]; // after a prefix that ends in a vowel
};

const Pool kPools[kGroups] = {
    // es
    {{"Mateo", "Hugo", "Álvaro", "Iker", "Bruno", "Nicolás", "Tomás", "Gael"},
     {"Lucía", "Martina", "Carla", "Elena", "Paula", "Irene"},
     {"arez", "ero", "ido", "olas", "eda", "illa"},
     {"ntos", "rrano", "lgado", "stillo", "bral", "rcía"}},
    // it
    {{"Lorenzo", "Matteo", "Tommaso", "Riccardo", "Edoardo", "Filippo", "Pietro", "Leonardo"},
     {"Giulia", "Chiara", "Aurora", "Sofia", "Beatrice", "Alice"},
     {"etti", "ardi", "oni", "elli", "ucci", "ini"},
     {"ssini", "rtelli", "nzani", "ldi", "ccardi", "valli"}},
    // fr
    {{"Théo", "Louis", "Raphaël", "Arthur", "Jules", "Nathan", "Maxence", "Hugo"},
     {"Léa", "Chloé", "Manon", "Camille", "Inès", "Jade"},
     {"ard", "ier", "eau", "ault", "ette", "and"},
     {"rnier", "lleau", "nard", "vel", "cquet", "rrand"}},
    // de
    {{"Felix", "Lukas", "Jonas", "Maximilian", "Elias", "Paul", "Niklas", "Moritz"},
     {"Lena", "Mia", "Hannah", "Lea", "Johanna", "Clara"},
     {"ermann", "ert", "inger", "ach", "oldt", "erer"},
     {"rner", "ttner", "lzer", "ckert", "nder", "hring"}},
    // br
    {{"Enzo", "Gabriel", "Rafael", "Davi", "Arthur", "Heitor", "Caio", "Thiago"},
     {"Ana", "Beatriz", "Larissa", "Júlia", "Marina", "Laura"},
     {"eira", "ado", "inho", "eiro", "ares", "ondes"},
     {"rtins", "ntoro", "lveira", "rreira", "stro", "ldanha"}},
    // en
    {{"Oliver", "Harry", "Jack", "Ethan", "Lewis", "Callum", "Ryan", "Noah"},
     {"Olivia", "Amelia", "Grace", "Isla", "Emily", "Ella"},
     {"ington", "ell", "ard", "ett", "ison", "ows"},
     {"rton", "wley", "ckson", "nley", "rrow", "lton"}},
    // nl
    {{"Daan", "Sem", "Levi", "Finn", "Milan", "Thijs", "Jesse", "Bram"},
     {"Emma", "Tess", "Julia", "Fenna", "Sara", "Lotte"},
     {"ens", "ema", "ink", "erts", "ers", "elaar"},
     {"rkens", "nstra", "lder", "ssink", "ckx", "ndel"}},
    // nordic
    {{"Elias", "Oskar", "Emil", "Aksel", "Leo", "Viktor", "Nils", "Eetu"},
     {"Saga", "Ella", "Freja", "Aino", "Ida", "Maja"},
     {"ström", "ander", "inen", "sen", "ola", "berg"},
     {"rinen", "nssen", "lund", "kola", "rsson", "dahl"}},
    // jp
    {{"Haruto", "Ren", "Sota", "Yuto", "Kaito", "Riku", "Hinata", "Daiki"},
     {"Yui", "Hina", "Aoi", "Mio", "Sakura", "Rin"},
     {"ura", "ama", "ita", "oto", "ashi", "ano"},
     {"mura", "kawa", "moto", "yama", "shita", "zaki"}},
};

bool vowel(char c)
{
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

} // namespace

std::string first(std::string_view nationality, bool female, uint64_t pick)
{
    const Pool &p = kPools[group_of(nationality)];
    return female ? p.female[pick % 6] : p.male[pick % 8];
}

std::string surname(std::string_view nationality, std::string_view initials, uint64_t pick)
{
    const Pool &p = kPools[group_of(nationality)];
    // the prefix: "Gil" from GIL, "Sa" from SAA, "M" from MNO
    std::string prefix;
    if (initials.size() >= 2 && std::isalpha(static_cast<unsigned char>(initials[0])) && vowel(initials[1])) {
        prefix = std::string(1, initials[0]) + initials[1];
        if (initials.size() >= 3 && !vowel(initials[2]) && std::isalpha(static_cast<unsigned char>(initials[2])))
            prefix += initials[2];
    } else if (!initials.empty() && std::isalpha(static_cast<unsigned char>(initials[0]))) {
        prefix = std::string(1, initials[0]);
    }
    for (size_t i = 0; i < prefix.size(); ++i)
        prefix[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(prefix[i]))
                                             : std::tolower(static_cast<unsigned char>(prefix[i])));
    if (prefix.empty())
        return std::string("N") + p.vowel_endings[pick % 6];
    bool ends_vowel = vowel(prefix.back());
    return prefix + (ends_vowel ? p.consonant_endings[pick % 6] : p.vowel_endings[pick % 6]);
}

std::string nationality(uint64_t pick)
{
    static const char *const nats[] = {"britain", "italy", "germany", "france", "spain", "netherlands", "japan", "finland",
                                       "brazil", "austria"};
    return nats[pick % 10];
}

} // namespace ss::names
