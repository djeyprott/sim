#include "histsim/view/figures.hpp"

#include <algorithm>

#include "histsim/figures.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/view/timeline.hpp"

namespace histsim::view {

namespace {

std::string year_text(Tick t) { return "year " + std::to_string(t / kTicksPerYear); }

std::string polity_name(const SimState& s, std::uint32_t id) {
    return id != 0 && id < s.polities.size() ? s.polities[id].name : "";
}

// A few words for the traits that stand out.
std::string traits_text(const Figure& f) {
    std::vector<std::string> words;
    auto add = [&](double v, const char* high, const char* low) {
        if (v >= 0.75) words.emplace_back(high);
        if (v <= 0.25 && low != nullptr) words.emplace_back(low);
    };
    if (f.roles & kRoleRuler) add(f.ability, "able", "inept");
    add(f.martial, "warlike", "peaceable");
    add(f.piety, "pious", "worldly");
    add(f.ambition, "ambitious", "content");
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) out += (i == 0 ? "" : ", ") + words[i];
    return out;
}

const char* reign_end_text(ReignEnd e) {
    switch (e) {
        case ReignEnd::Reigning: return "reigning";
        case ReignEnd::Died: return "died on the throne";
        case ReignEnd::Usurped: return "overthrown";
        case ReignEnd::RealmFell: return "the realm fell";
    }
    return "";
}

bool deed_worth_telling(EventType t) {
    switch (t) {
        case EventType::WarBegan:
        case EventType::WarEnded:
        case EventType::AllianceFormed:
        case EventType::Subjugated:
        case EventType::Absorbed:
        case EventType::ConquestDynasty:
        case EventType::StateConversion:
        case EventType::PolityWandered:
        case EventType::InstitutionFounded:
        case EventType::InstitutionLost:
        case EventType::ReligionFounded:
        case EventType::SuccessionDisputed:
        case EventType::PolityEnded: return true;
        default: return false;
    }
}

}  // namespace

std::string figure_title(const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.figures.size()) return "";
    const Figure& f = s.figures[id];
    if (f.ruler != 0 && f.ruler < s.rulers.size()) return ruler_name(s.rulers[f.ruler]);
    return f.name;
}

std::uint32_t polity_ruler_figure(const SimState& s, std::uint32_t polity) {
    if (polity == 0 || polity >= s.polities.size()) return 0;
    const Polity& p = s.polities[polity];
    if (p.ended != kNever || p.ruler == 0 || p.ruler >= s.rulers.size()) return 0;
    const std::uint32_t f = s.rulers[p.ruler].figure;
    return f < s.figures.size() ? f : 0;
}

FigurePage figure_page(const Simulation& sim, std::uint32_t id) {
    FigurePage page;
    const SimState& s = sim.state();
    if (id == 0 || id >= s.figures.size()) return page;
    const Figure& f = s.figures[id];
    const Tick now = s.tick;
    page.id = id;
    page.title = figure_title(s, id);
    if (f.dynasty != 0 && f.dynasty < s.dynasties.size()) page.title += " of the " + s.dynasties[f.dynasty].name;

    // ---- Who: the roles, in one line.
    const Ruler* reign = f.ruler != 0 && f.ruler < s.rulers.size() ? &s.rulers[f.ruler] : nullptr;
    std::string sub;
    if (reign != nullptr) {
        sub = "Ruler of " + polity_name(s, reign->polity) + ", " +
              (reign->reign_end == kNever ? "since " + year_text(reign->reign_start)
                                          : year_text(reign->reign_start) + "–" + std::to_string(reign->reign_end / kTicksPerYear));
    } else if (f.roles & kRoleProphet) {
        sub = "Teacher of a faith";
    } else if (f.roles & kRoleInventor) {
        sub = "Inventor";
    } else if ((f.roles & kRoleRebel) && !(f.roles & kRoleRuler)) {
        sub = "Leader of a peasant uprising";
    } else if (f.roles & kRoleGovernor) {
        sub = "Governor";
        for (const Province& pv : s.provinces) {
            if (pv.governor == id) sub = "Governor of " + pv.name + (pv.polity != 0 ? " in " + polity_name(s, pv.polity) : std::string());
        }
    } else if (f.roles & kRoleClaimant) {
        sub = "Claimant to the throne of " + polity_name(s, f.polity);
    } else if (f.roles & kRoleConsort) {
        sub = "Consort";
    } else if (f.roles & kRoleRoyalKin) {
        sub = std::string(f.female ? "Princess" : "Prince") +
              (f.polity != 0 ? " of " + polity_name(s, f.polity) : std::string());
    }
    const std::string traits = traits_text(f);
    if (!traits.empty()) sub += (sub.empty() ? "" : "; ") + traits;
    page.subtitle = sub;

    // ---- Life.
    const std::string people = f.culture != 0 && f.culture < s.cultures.size() ? "of the " + s.cultures[f.culture].name : "";
    const std::string tongue =
        f.language != 0 && f.language < s.languages.size() ? ", speaking " + s.languages[f.language].name : "";
    page.life.push_back({"Born", year_text(f.born) + (people.empty() ? "" : ", " + people) + tongue, 0, f.home});
    if (f.died != kNever && f.died <= now) {
        std::string how;
        if (reign != nullptr && reign->reign_end == f.died && reign->end == ReignEnd::Died) how = ", on the throne";
        page.life.push_back({"Died", year_text(f.died) + " at " + std::to_string(figure_age(f, f.died)) + how, 0, kInvalidCell});
    } else {
        page.life.push_back({"Alive", "aged " + std::to_string(figure_age(f, now)), 0, kInvalidCell});
    }
    if (f.goal != FigureGoal::None) page.life.push_back({"Wanted", goal_name(f.goal), 0, kInvalidCell});
    if (reign != nullptr) {
        const int age = static_cast<int>((reign->reign_start - f.born) / kTicksPerYear);
        std::string how = (f.roles & kRoleUsurper)   ? "seized the throne"
                          : (f.roles & kRoleFounder) ? "founded the realm and the " +
                                                           (f.dynasty < s.dynasties.size() ? s.dynasties[f.dynasty].name
                                                                                           : std::string("house"))
                                                     : "inherited the throne";
        how += " at " + std::to_string(age);
        if (age < RulerSystem::kAdultAge && !(f.roles & kRoleFounder)) how += ", a regent ruling for the child";
        page.life.push_back({"Reign", how + "; " + reign_end_text(reign->end), 0, s.polities[reign->polity].capital});
        // Who came before and after on that throne.
        for (std::size_t r = 1; r < s.rulers.size(); ++r) {
            const Ruler& o = s.rulers[r];
            if (o.polity != reign->polity || o.id == reign->id) continue;
            if (o.reign_end == reign->reign_start && o.figure != 0) {
                page.life.push_back({"Before", figure_title(s, o.figure), o.figure, kInvalidCell});
            }
            if (reign->reign_end != kNever && o.reign_start == reign->reign_end && o.figure != 0) {
                page.life.push_back({"After", figure_title(s, o.figure), o.figure, kInvalidCell});
            }
        }
    }
    for (const Invention& inv : s.inventions) {
        if (inv.figure == id && inv.technique < sim.world().content->techniques.size()) {
            page.life.push_back({"Invented", sim.world().content->techniques[inv.technique].name + ", " + year_text(inv.tick), 0,
                                 inv.cell});
        }
    }
    for (const Uprising& u : s.uprisings) {
        if (u.leader != id) continue;
        const std::string where = u.province != 0 && u.province < s.provinces.size() ? s.provinces[u.province].name
                                                                                     : "the crown's land";
        const char* how = u.outcome == UprisingOutcome::Crushed     ? "crushed"
                          : u.outcome == UprisingOutcome::BrokeAway ? "the province broke away"
                                                                    : "overthrew the dynasty";
        page.life.push_back({"Rose", std::to_string(u.rebels) + " peasants against " + polity_name(s, u.polity) + " in " + where +
                                         ", " + year_text(u.tick) + "; " + how,
                             0, u.cell});
    }
    for (const Province& pv : s.provinces) {
        if (pv.governor != id) continue;
        std::string how = pv.became != 0 ? "broke away, founding " + polity_name(s, pv.became)
                          : pv.ended == kNever ? "governing" : "the province ended";
        page.life.push_back({"Governed", pv.name + " for " + polity_name(s, pv.polity) + "; " + how, 0, pv.seat});
    }
    for (const Religion& r : s.religions) {
        if (r.id != 0 && r.founder == id) {
            page.life.push_back({"Taught", "the " + r.name + ", " + year_text(r.founded), 0, r.origin});
        }
    }

    // ---- Family.
    auto relative = [&](const char* label, std::uint32_t who) {
        if (who != 0 && who < s.figures.size()) page.family.push_back({label, figure_title(s, who), who, kInvalidCell});
    };
    relative("Father", f.father);
    relative("Mother", f.mother);
    relative(f.female ? "Husband" : "Wife", f.spouse);
    for (const Figure& o : s.figures) {
        if (o.id == 0 || o.id == id) continue;
        if (f.father != 0 && (o.father == f.father || (f.mother != 0 && o.mother == f.mother))) {
            relative(o.female ? "Sister" : "Brother", o.id);
        }
    }
    for (const Figure& o : s.figures) {
        if (o.id != 0 && (o.father == id || o.mother == id)) relative(o.female ? "Daughter" : "Son", o.id);
    }

    // ---- Deeds: what befell the realm in the reign (or the claim).
    Tick from = kNever, to = kNever;
    std::uint32_t realm = 0;
    if (reign != nullptr) {
        from = reign->reign_start;
        to = reign->reign_end == kNever ? now + 1 : reign->reign_end + 1;
        realm = reign->polity;
    }
    std::size_t told = 0, more = 0;
    for (const Event& e : s.events) {
        const bool claim = e.type == EventType::SuccessionDisputed && e.value == static_cast<std::int64_t>(id);
        const bool in_reign = realm != 0 && e.tick >= from && e.tick < to && deed_worth_telling(e.type) &&
                              event_involves(s, e, realm);
        if (!claim && !in_reign) continue;
        if (told >= 20) {
            ++more;
            continue;
        }
        std::string text = describe_event(sim, e);
        if (text.empty()) continue;
        page.deeds.push_back({year_text(e.tick), std::move(text), event_figure(s, e) != id ? event_figure(s, e) : 0, e.cell});
        ++told;
    }
    if (more > 0) page.deeds.push_back({"", "and " + std::to_string(more) + " more", 0, kInvalidCell});
    return page;
}

std::vector<std::uint32_t> notable_figures(const Simulation& sim, std::size_t max) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    std::vector<std::uint32_t> out;
    auto add = [&](std::uint32_t f) {
        if (f != 0 && f < s.figures.size() && out.size() < max && std::find(out.begin(), out.end(), f) == out.end()) {
            out.push_back(f);
        }
    };
    // Rulers of the largest realms alive.
    std::vector<double> area(s.polities.size(), 0.0);
    for (CellId c = 0; c < s.polity_of.size(); ++c) area[s.polity_of[c]] += w.grid.area_km2(c);
    std::vector<std::uint32_t> realms;
    for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
        if (s.polities[p].ended == kNever && area[p] > 0.0) realms.push_back(p);
    }
    std::stable_sort(realms.begin(), realms.end(), [&](std::uint32_t a, std::uint32_t b) { return area[a] > area[b]; });
    for (std::size_t i = 0; i < realms.size() && i < std::max<std::size_t>(1, max / 2); ++i) add(polity_ruler_figure(s, realms[i]));
    // Teachers of the faiths, newest first, and the latest inventors.
    for (std::size_t r = s.religions.size(); r-- > 1 && out.size() < max * 3 / 4;) add(s.religions[r].founder);
    for (std::size_t i = s.inventions.size(); i-- > 0 && out.size() < max;) add(s.inventions[i].figure);
    return out;
}

}  // namespace histsim::view
