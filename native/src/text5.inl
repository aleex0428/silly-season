// Silly Season 1.1.0 phase 5 texts. Included by text.cpp inside namespace ss::text, so it shares its helpers
// (fmt, choose, num, who, where).

// --- the press: new topics -------------------------------------------------------------------------------

std::string q_deadline(Lang l, const std::string &team, int left)
{
    return fmt(left <= 1 ? (l.es ? "\"Última carrera antes de cerrar la parrilla. ¿Necesita %1 un fichaje más?\""
                                 : "\"One race before the grid is set. Do %1 need one more signing?\"")
                         : (l.es ? "\"Empieza el deadline day. ¿Necesita %1 moverse en el mercado?\""
                                 : "\"Deadline day is here. Do %1 need to make a move?\""),
               team);
}
std::string a_deadline(Lang l, int i)
{
    static const char *const es[] = {"\"Estoy contento con lo que tenemos.\"", "\"Necesitamos refuerzos, y ya.\"",
                                     "\"Eso es cosa del equipo, no mía.\""};
    static const char *const en[] = {"\"I'm happy with what we have.\"", "\"We need reinforcements, and now.\"",
                                     "\"That's for the team to decide, not me.\""};
    return l.es ? es[i % 3] : en[i % 3];
}
std::string r_deadline(Lang l, int i, const std::string &team)
{
    static const char *const es[] = {"Confianza en el proyecto: el piloto de %1 no pide nada. En la fábrica se ha agradecido.",
                                     "Recado a los jefes: el piloto de %1 pide refuerzos en pleno deadline day. El equipo busca un nombre fuerte.",
                                     "El piloto de %1 deja el mercado en manos del equipo."};
    static const char *const en[] = {"Trust in the project: %1's driver asks for nothing, and the factory noticed.",
                                     "A message to the bosses: %1's driver asks for reinforcements on deadline day. The team is hunting a big name.",
                                     "%1's driver leaves the market to the team."};
    return fmt(l.es ? es[i % 3] : en[i % 3], team);
}

std::string q_rival(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "\"%1 se va a %2. ¿Te preocupa tener un rival así el año que viene?\""
                    : "\"%1 is off to %2. Does a rival like that worry you for next year?\"",
               driver, team);
}
std::string a_rival(Lang l, int i)
{
    static const char *const es[] = {"\"Que venga. Me gusta ganar a los mejores.\"", "\"Mi equipo tiene que responder.\"",
                                     "\"No es asunto mío.\""};
    static const char *const en[] = {"\"Bring it on. I like beating the best.\"", "\"My team has to respond.\"",
                                     "\"It's not my business.\""};
    return l.es ? es[i % 3] : en[i % 3];
}
std::string r_rival(Lang l, int i, const std::string &driver)
{
    static const char *const es[] = {"Sin miedo: \"me gusta ganar a los mejores\", responde sobre %1. Su equipo, encantado.",
                                     "Presión a su equipo tras el fichaje de %1: \"tenemos que responder\". Se buscan refuerzos.",
                                     "Ni una palabra de más sobre el fichaje de %1."};
    static const char *const en[] = {"No fear: \"I like beating the best\", on %1's move. His team loved it.",
                                     "Pressure on his own team after %1's move: \"we have to respond\". Reinforcements are wanted.",
                                     "Not a word more than needed about %1's move."};
    return fmt(l.es ? es[i % 3] : en[i % 3], driver);
}

std::string q_renewal(Lang l, const std::string &team)
{
    // the game doesn't tell the player's own contract: the question asks, it doesn't claim
    return fmt(l.es ? "\"Se habla de tu próximo contrato con %1. ¿Estáis cerca de un acuerdo?\""
                    : "\"There's talk about your next deal with %1. Are you close to an agreement?\"",
               team);
}
std::string a_renewal(Lang l, int i, const std::string &team)
{
    static const char *const es[] = {"\"Sí, estamos muy cerca. Quiero seguir en %1.\"", "\"Estoy valorando todas las opciones.\"",
                                     "\"Hablamos con el equipo; no hay prisa.\""};
    static const char *const en[] = {"\"Yes, we're very close. I want to stay at %1.\"", "\"I'm weighing every option.\"",
                                     "\"We're talking to the team; there's no rush.\""};
    return fmt(l.es ? es[i % 3] : en[i % 3], team);
}
std::string r_renewal(Lang l, int i, const std::string &team)
{
    static const char *const es[] = {"Renovación encarrilada: el piloto de %1 quiere seguir y el equipo lo celebra.",
                                     "El piloto de %1 se hace valer: \"valoro todas las opciones\". En el equipo no ha gustado.",
                                     "Sin prisa: el piloto de %1 sigue hablando con el equipo."};
    static const char *const en[] = {"A new deal on track: %1's driver wants to stay, and the team is happy.",
                                     "%1's driver plays hard to get: \"I'm weighing every option\". The team didn't like it.",
                                     "No rush: %1's driver is still talking to the team."};
    return fmt(l.es ? es[i % 3] : en[i % 3], team);
}

std::string q_award(Lang l, const std::string &driver, const std::string &award, int year)
{
    return fmt(l.es ? "\"%1 fue elegido '%2' en %3. ¿Lo merecía?\"" : "\"%1 was named '%2' for %3. Did he deserve it?\"", driver,
               award, num(year));
}
std::string a_award(Lang l, int i)
{
    static const char *const es[] = {"\"Sin duda. Se lo ganó.\"", "\"Está sobrevalorado.\"", "\"Este año el premio es mío.\""};
    static const char *const en[] = {"\"No doubt. He earned it.\"", "\"He's overrated.\"", "\"This year it's mine.\""};
    return l.es ? es[i % 3] : en[i % 3];
}
std::string r_award(Lang l, int i, const std::string &driver)
{
    static const char *const es[] = {"Reconocimiento para %1: \"se lo ganó\".", "Polémica: llama \"sobrevalorado\" a %1, que ya ha contestado.",
                                     "Ambición: \"este año es mío\", avisa a %1."};
    static const char *const en[] = {"Respect for %1: \"he earned it\".", "Controversy: he calls %1 \"overrated\", and %1 hit back.",
                                     "Ambition: \"this year it's mine\", he warns %1."};
    return fmt(l.es ? es[i % 3] : en[i % 3], driver);
}

// --- sister-team swaps ----------------------------------------------------------------------------------------

std::string h_swap(Lang l, const std::string &up, const std::string &senior)
{
    return fmt(l.es ? "%1 SUBE A %2" : "%1 PROMOTED TO %2", who(up), where(senior));
}
std::string swap(Lang l, int pick, const std::string &up, const std::string &down, const std::string &senior,
                 const std::string &junior)
{
    static const char *const es[] = {
        "Cambio de pilotos entre %3 y %4 con efecto inmediato: %1 sube al equipo grande y %2 vuelve a %4.",
        "%3 no espera más: %2 baja a %4 y %1 ocupa su asiento desde la próxima carrera.",
    };
    static const char *const en[] = {
        "A driver swap between %3 and %4 with immediate effect: %1 steps up to the senior team and %2 goes back to %4.",
        "%3 won't wait any longer: %2 drops to %4 and %1 takes his seat from the next race.",
    };
    return fmt(l.es ? choose(es, pick) : choose(en, pick), up, down, senior, junior);
}

// --- money ----------------------------------------------------------------------------------------------------

const char *sponsor_brand(int pick)
{
    static const char *const brands[] = {"Valtora Energy", "Nordvik Bank",   "Helix Telecom",  "Corvan Watches", "Aurea Air",
                                         "Brightwave",     "Kestrel Cloud",  "Monza Coffee",   "Pellion Tyres",  "Sora Mobility",
                                         "Tidewater Tech", "Quanta Freight", "Lumen Drinks",   "Rakuda Games",   "Orvel Insurance"};
    return choose(brands, pick);
}
std::string h_sponsor(Lang l, const std::string &team, bool in)
{
    return fmt(in ? (l.es ? "DINERO NUEVO PARA %1" : "NEW MONEY FOR %1") : (l.es ? "UN PATROCINADOR DEJA %1" : "A SPONSOR LEAVES %1"),
               where(team));
}
std::string sponsor(Lang l, int pick, const std::string &team, const std::string &brand, int millions, bool in)
{
    static const char *const es_in[] = {"%2 llega a %1 con unos %3 millones por temporada. Los resultados han convencido a la marca.",
                                        "%1 cierra un acuerdo con %2: %3 millones más para el presupuesto, y para los sueldos."};
    static const char *const en_in[] = {"%2 joins %1, worth about %3 million a season. The results convinced the brand.",
                                        "%1 strike a deal with %2: %3 million more for the budget, and for the salaries."};
    static const char *const es_out[] = {"%2 no renovará con %1: se pierden unos %3 millones. La temporada está pasando factura.",
                                         "Golpe para %1: %2 se marcha y con él %3 millones del presupuesto."};
    static const char *const en_out[] = {"%2 won't renew with %1: about %3 million is gone. The season is taking its toll.",
                                         "A blow for %1: %2 walks away, and %3 million of the budget with it."};
    const char *f = in ? (l.es ? choose(es_in, pick) : choose(en_in, pick)) : (l.es ? choose(es_out, pick) : choose(en_out, pick));
    return fmt(f, team, brand, num(millions));
}

// --- team principals ------------------------------------------------------------------------------------------

std::string h_principal(Lang l, const std::string &team)
{
    return fmt(l.es ? "%1 DESPIDE A SU JEFE DE EQUIPO" : "%1 SACK THEIR TEAM PRINCIPAL", where(team));
}
std::string principal_out(Lang l, int pick, const std::string &old, const std::string &team, const std::string &fresh,
                          const std::string &plan)
{
    static const char *const es[] = {"%2 prescinde de %1: el equipo está muy por debajo de lo que promete el coche. %3 toma el mando.",
                                     "Se acabó la paciencia en %2: %1 deja el cargo y %3 llega como nuevo jefe de equipo."};
    static const char *const en[] = {"%2 part ways with %1: the team is well below what the car promises. %3 takes charge.",
                                     "Patience has run out at %2: %1 steps down and %3 arrives as the new team principal."};
    std::string out = fmt(l.es ? choose(es, pick) : choose(en, pick), old, team, fresh);
    if (!plan.empty())
        out += fmt(l.es ? " Su plan: %1." : " His plan: %1.", plan);
    return out;
}

// --- farewells --------------------------------------------------------------------------------------------------

std::string h_farewell(Lang l, const std::string &driver)
{
    return fmt(l.es ? "ÚLTIMA CARRERA DE %1" : "%1'S LAST RACE", who(driver));
}
std::string farewell(Lang l, const std::string &driver, int seasons, int starts, int wins, int podiums, int titles)
{
    std::string out = fmt(l.es ? "%1 se despide de la F1 tras %2 temporadas." : "%1 says goodbye to F1 after %2 seasons.", driver,
                          num(seasons < 1 ? 1 : seasons));
    if (starts > 0)
        out += fmt(l.es ? " En esta carrera deportiva: %1 salidas, %2 victorias y %3 podios." : " In this career: %1 starts, %2 wins and %3 podiums.",
                   num(starts), num(wins), num(podiums));
    if (titles > 0)
        out += fmt(l.es ? " Campeón del mundo %1 %2." : " World champion %1 %2.", num(titles), titles == 1 ? (l.es ? "vez" : "time") : (l.es ? "veces" : "times"));
    out += l.es ? " El paddock le despide en pie." : " The paddock gives him a standing ovation.";
    return out;
}
std::string tribute(Lang l, const std::string &driver, int seasons, int wins)
{
    std::string out = driver + " (" + num(seasons < 1 ? 1 : seasons) + (l.es ? " temporadas" : " seasons");
    if (wins > 0)
        out += ", " + num(wins) + (wins == 1 ? (l.es ? " victoria" : " win") : (l.es ? " victorias" : " wins"));
    return out + ")";
}

// --- the inbox mails --------------------------------------------------------------------------------------------

std::string mail_open_seats(Lang l, const std::string &teams)
{
    if (teams.empty())
        return l.es ? "Todos los asientos parecen atados, pero el último día siempre trae sorpresas."
                    : "Every seat looks settled, but the last day always brings a surprise.";
    return fmt(l.es ? "Asientos aún por decidir: %1." : "Seats still to fill: %1.", teams);
}
std::string h_mail_chains(Lang l) { return l.es ? "EFECTO DOMINÓ" : "THE DOMINOES"; }
std::string mail_media(Lang l, const std::string &outlet, int hits, int stories)
{
    return fmt(l.es ? "%1 es el medio más fiable del año: %2 de %3 historias se han cumplido."
                    : "%1 is the year's most reliable outlet: %2 of %3 stories came true.",
               outlet, num(hits), num(stories));
}
std::string h_mail_farewells(Lang l) { return l.es ? "SE DESPIDEN" : "FAREWELLS"; }

// --- academies and reserves (phase 6) ----------------------------------------------------------------------------

std::string h_academy(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "%2 FICHA A %1 PARA SU ACADEMIA" : "%2 SIGN %1 TO THEIR ACADEMY", who(driver), where(team));
}
std::string academy(Lang l, int pick, const std::string &driver, const std::string &team, int age)
{
    static const char *const es[] = {"%2 incorpora a %1 (%3 años) a su programa de jóvenes pilotos. El objetivo: tenerlo listo para la F1.",
                                     "%1, con %3 años, entra en la academia de %2. El equipo le ve futuro en la F1."};
    static const char *const en[] = {"%2 add %1 (%3) to their young driver programme. The aim: have him ready for F1.",
                                     "%1, %3, joins the %2 academy. The team sees an F1 future in him."};
    return fmt(l.es ? choose(es, pick) : choose(en, pick), driver, team, num(age));
}
std::string h_reserves(Lang l, int year)
{
    return fmt(l.es ? "LOS PILOTOS RESERVA DE %1" : "THE %1 RESERVE DRIVERS", num(year));
}
std::string reserve_line(Lang l, const std::string &team, const std::string &driver, bool renewed)
{
    return fmt(renewed ? (l.es ? "%1: %2 (sigue)" : "%1: %2 (stays on)") : (l.es ? "%1: %2 (nuevo)" : "%1: %2 (new)"), team, driver);
}

// --- a promotion from F2 another addon made ------------------------------------------------------------------

std::string h_f2_promotion(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "DE LA F2 A %2: %1" : "FROM F2 TO %2: %1", who(driver), where(team));
}
std::string f2_promotion(Lang l, const std::string &driver, const std::string &team, int until, const std::string &source)
{
    (void)source;
    return fmt(l.es ? "%1 da el salto a la F1 con %2 tras su temporada en F2. Contrato hasta %3."
                    : "%1 steps up to F1 with %2 after his F2 season, on a deal until %3.",
               driver, team, num(until));
}

// --- 1.1.0, from player reports ------------------------------------------------------------------------------

std::string h_deal_off(Lang l, const std::string &driver, const std::string &team)
{
    return fmt(l.es ? "SE CAE: %1 NO IRÁ A %2" : "DEAL OFF: %1 WON'T JOIN %2", who(driver), where(team));
}
std::string deal_off(Lang l, const std::string &driver, const std::string &team, const std::string &now_team)
{
    std::string out = fmt(l.es ? "El fichaje de %1 por %2 se cae en el último momento." : "%1's move to %2 has fallen through at the last moment.",
                          driver, team);
    out += now_team.empty() ? fmt(l.es ? " %1 empieza la temporada sin asiento." : " %1 starts the season without a race seat.", driver)
                            : fmt(l.es ? " %1 sigue en %2." : " %1 stays at %2.", driver, now_team);
    return out;
}
std::string h_observer(Lang l)
{
    return l.es ? "MY TEAM: EL MERCADO LO MUEVEN LOS EQUIPOS" : "MY TEAM: THE TEAMS MAKE THE MOVES";
}
std::string observer(Lang l)
{
    return l.es ? "En My Team los fichajes los cierran los propios equipos. Silly Season sigue los rumores y las "
                  "negociaciones, y da por oficial cada cambio cuando el piloto aparece en su nuevo coche."
                : "In My Team the teams close their own deals. Silly Season follows the rumours and the talks, and "
                  "reports each move as official once the driver turns up in his new car.";
}
