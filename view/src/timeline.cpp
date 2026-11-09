#include "histsim/view/timeline.hpp"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "histsim/systems/polity.hpp"
#include "histsim/systems/religion.hpp"

namespace histsim::view {

namespace {

std::string commas(std::int64_t n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

std::string polity_name(const SimState& s, std::int64_t id) {
    return id > 0 && static_cast<std::size_t>(id) < s.polities.size() ? s.polities[static_cast<std::size_t>(id)].name : "?";
}

// "at Kish" for a city, else "in the lands of X" for a ruled cell, else
// coordinates.
std::string place(const Simulation& sim, CellId c) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    if (c >= w.cell_count()) return "";
    if (s.city_of.size() == w.cell_count() && s.city_of[c] != 0) return " at " + s.cities[s.city_of[c]].name;
    const double lat = w.grid.latitude_deg_display(c), lon = w.grid.longitude_deg_display(c);
    char buf[48];
    std::snprintf(buf, sizeof buf, " (%.0f°%s %.0f°%s)", std::fabs(lat), lat >= 0 ? "N" : "S", std::fabs(lon),
                  lon >= 0 ? "E" : "W");
    return buf;
}

const char* cause_text(MigrationCause c) {
    switch (c) {
        case MigrationCause::Famine: return "fleeing famine";
        case MigrationCause::War: return "fleeing war";
        case MigrationCause::Raid: return "fleeing raids";
        case MigrationCause::Displaced: return "pushed out by newcomers";
        case MigrationCause::Wandering: return "a whole people on the move";
        case MigrationCause::Conquest: return "following their horde into conquered land";
    }
    return "";
}

}  // namespace

const char* event_kind(EventType type) {
    switch (type) {
        case EventType::PolityFounded:
        case EventType::PolityEnded:
        case EventType::Subjugated:
        case EventType::Submitted:
        case EventType::TributeWithheld:
        case EventType::Abandoned:
        case EventType::Betrayal:
        case EventType::LawChanged:
        case EventType::GovernorsRise:
        case EventType::RealmDivided:
        case EventType::Absorbed:
        case EventType::ConquestDynasty:
        case EventType::CityFounded:
        case EventType::PolityWandered:
        case EventType::Succession:
        case EventType::Usurpation:
        case EventType::SuccessionDisputed:
        case EventType::ProvinceRevolt:
        case EventType::PeasantUprising:
        case EventType::InstitutionFounded:
        case EventType::InstitutionLost: return "realms";
        case EventType::WarBegan:
        case EventType::WarEnded:
        case EventType::AllianceFormed:
        case EventType::BattleFought:
        case EventType::Raid: return "war";
        case EventType::LanguageSplit:
        case EventType::CultureSplit:
        case EventType::LanguageExtinct:
        case EventType::CultureExtinct:
        case EventType::FolkMigration:
        case EventType::CellSettled:
        case EventType::WorldPopulation: return "peoples";
        case EventType::ReligionFounded:
        case EventType::Schism:
        case EventType::StateConversion: return "faith";
        case EventType::Eruption:
        case EventType::BondEvent:
        case EventType::DiseaseEmerged:
        case EventType::Intervention: return "nature";
        case EventType::Domestication:
        case EventType::Invention: return "discovery";
    }
    return "";
}

std::string describe_event(const Simulation& sim, const Event& e) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    const Content& content = *w.content;
    const auto v = e.value;
    const auto index = static_cast<std::size_t>(v < 0 ? 0 : v);
    switch (e.type) {
        case EventType::CellSettled: return "People first settle land" + place(sim, e.cell);
        case EventType::WorldPopulation: return "The world's people pass " + commas(v);
        case EventType::Eruption: {
            char buf[96];
            std::snprintf(buf, sizeof buf, "A great eruption darkens the sky (%.1f W/m²)", static_cast<double>(v) / 1000.0);
            return buf;
        }
        case EventType::Intervention: return "God mode: an intervention" + place(sim, e.cell);
        case EventType::LanguageSplit:
            if (index < s.languages.size()) {
                const Language& l = s.languages[index];
                return l.name + " becomes a language of its own" +
                       (l.parent != 0 && l.parent < s.languages.size() ? ", apart from " + s.languages[l.parent].name : "");
            }
            return "";
        case EventType::CultureSplit:
            return index < s.cultures.size() ? "The " + s.cultures[index].name + " become a people of their own" + place(sim, e.cell)
                                             : "";
        case EventType::LanguageExtinct:
            return index < s.languages.size() ? "The last speakers of " + s.languages[index].name + " are gone" : "";
        case EventType::CultureExtinct:
            return index < s.cultures.size() ? "The " + s.cultures[index].name + " are no more" : "";
        case EventType::Domestication: {
            if (index >= content.species.size()) return "";
            std::string who;
            for (const Domestication& d : s.domestications) {
                if (d.tick == e.tick && d.cell == e.cell && d.species == index && d.culture < s.cultures.size()) {
                    who = " by the " + s.cultures[d.culture].name;
                }
            }
            return content.species[index].domesticated + " first domesticated" + who + place(sim, e.cell);
        }
        case EventType::BondEvent: {
            char buf[96];
            std::snprintf(buf, sizeof buf, "A cold, dry age begins in the north (up to %.1f °C colder)",
                          std::fabs(static_cast<double>(v)) / 1000.0);
            return buf;
        }
        case EventType::DiseaseEmerged:
            return index < content.diseases.size() ? content.diseases[index].name + " emerges" + place(sim, e.cell) : "";
        case EventType::Invention: {
            if (index >= content.techniques.size()) return "";
            std::string who;
            for (const Invention& inv : s.inventions) {
                if (inv.tick == e.tick && inv.cell == e.cell && inv.technique == index && inv.culture < s.cultures.size()) {
                    who = " by " + (inv.figure != 0 && inv.figure < s.figures.size() ? s.figures[inv.figure].name + " of " : "") +
                          "the " + s.cultures[inv.culture].name;
                }
            }
            return content.techniques[index].name + " invented" + who + place(sim, e.cell);
        }
        case EventType::CityFounded:
            return index < s.cities.size() ? s.cities[index].name + " grows into a city" : "";
        case EventType::PolityFounded:
            if (index < s.polities.size()) {
                const Polity& p = s.polities[index];
                std::string from = p.parent != 0 ? ", breaking away from " + polity_name(s, p.parent) : "";
                return p.name + (p.nomadic ? ", a nomad confederation," : "") + " is founded" + place(sim, p.capital) + from;
            }
            return "";
        case EventType::PolityEnded:
            return index < s.polities.size() ? s.polities[index].name + " ends: " + polity_fate_text(s.polities[index]) : "";
        case EventType::WarBegan:
            return index < s.wars.size() ? "The " + s.wars[index].name + " begins" : "";
        case EventType::WarEnded:
            if (index < s.wars.size()) {
                const War& war = s.wars[index];
                const char* how = war.outcome == WarOutcome::AttackersWon   ? ": the attackers win"
                                  : war.outcome == WarOutcome::DefendersWon ? ": the defenders hold"
                                                                            : " in a stalemate";
                const char* terms = "";
                switch (war.terms) {
                    case PeaceTerms::WhitePeace: terms = "; a white peace, all taken land given back"; break;
                    case PeaceTerms::LandKept: terms = war.cells_taken > 0 ? "; the victors keep what they took" : ""; break;
                    case PeaceTerms::LandReturned: terms = "; the lost land is taken back"; break;
                    case PeaceTerms::Tribute: terms = "; the vanquished pay tribute"; break;
                    case PeaceTerms::Vassalage: terms = "; the vanquished become vassals"; break;
                    case PeaceTerms::Annexed: terms = "; the vanquished are annexed"; break;
                    case PeaceTerms::Independence: terms = "; independence won"; break;
                    case PeaceTerms::Dynasty: terms = "; the conquerors found a dynasty"; break;
                    case PeaceTerms::Throne: terms = "; the victor rules the whole realm"; break;
                    case PeaceTerms::Divided: terms = "; the realm stays divided between the claimants"; break;
                    case PeaceTerms::None: break;
                }
                return "The " + war.name + " ends" + how + terms + " (" + commas(war.deaths) + " dead)";
            }
            return "";
        case EventType::AllianceFormed:
            if (index < s.alliances.size()) {
                return polity_name(s, s.alliances[index].a) + " and " + polity_name(s, s.alliances[index].b) + " become allies";
            }
            return "";
        case EventType::Subjugated:
            if (index < s.polities.size()) {
                const Polity& p = s.polities[index];
                return p.name + (p.bond == Bond::Vassal ? " becomes a vassal of " : " pays tribute to ") + polity_name(s, p.overlord);
            }
            return "";
        case EventType::Absorbed:
            return index < s.polities.size() ? s.polities[index].name + " is absorbed by its overlord" : "";
        case EventType::Submitted:
            if (index < s.polities.size()) {
                return s.polities[index].name + " offers tribute to " + polity_name(s, s.polities[index].overlord);
            }
            return "";
        case EventType::TributeWithheld:
            return polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) >> 32)) + " withholds its tribute from " +
                   polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) & 0xffffffffu));
        case EventType::LawChanged: {
            const auto law = static_cast<Laws::Law>((static_cast<std::uint64_t>(v) >> 8) & 0xffu);
            const auto level = static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) & 0xffu);
            return polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) >> 16)) + ": " + law_name(law) +
                   " now " + (law == Laws::Succession ? "by " : "") + law_level_name(law, level);
        }
        case EventType::GovernorsRise:
            return index < s.polities.size() ? "Governors of " + polity_name(s, s.polities[index].parent) +
                                                   " rise together as " + s.polities[index].name
                                             : "";
        case EventType::RealmDivided:
            return index < s.polities.size() ? polity_name(s, s.polities[index].parent) + " is shared out among the sons: " +
                                                   s.polities[index].name + " goes to a younger son"
                                             : "";
        case EventType::Betrayal:
            return polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) >> 32)) +
                   " makes a separate peace, abandoning " +
                   polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) & 0xffffffffu));
        case EventType::Abandoned:
            return polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) & 0xffffffffu)) +
                   ", at war elsewhere, leaves " + polity_name(s, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) >> 32)) +
                   " to fend for itself";
        case EventType::Raid: return polity_name(s, v) + " nomads raid" + place(sim, e.cell);
        case EventType::ConquestDynasty:
            return polity_name(s, v) + " nomads take" + place(sim, e.cell) + " and found a conquest dynasty";
        case EventType::FolkMigration:
            if (index < s.migrations.size()) {
                const FolkMovement& m = s.migrations[index];
                if (m.cause == MigrationCause::Wandering) {
                    return "The " + polity_name(s, m.polity) + " set out" + place(sim, m.from) + " with " + commas(m.people) +
                           " people";
                }
                return commas(m.people) + " " + s.languages[m.language].name + " speakers leave" + place(sim, m.from) + ", " +
                       cause_text(m.cause);
            }
            return "";
        case EventType::ReligionFounded:
            if (index < s.religions.size()) {
                const Religion& r = s.religions[index];
                const std::string by =
                    r.founder != 0 && r.founder < s.figures.size() ? ", taught by " + s.figures[r.founder].name : "";
                return "The " + r.name + " arises" + place(sim, e.cell) + by;
            }
            return "";
        case EventType::Schism:
            if (index < s.religions.size() && s.religions[index].parent < s.religions.size()) {
                return "The " + s.religions[index].name + " splits from the " + s.religions[s.religions[index].parent].name;
            }
            return "";
        case EventType::StateConversion:
            if (index < s.polities.size()) {
                const std::uint32_t f = s.polities[index].religion;
                return s.polities[index].name + "'s rulers take up the " + (f < s.religions.size() ? s.religions[f].name : "?");
            }
            return "";
        case EventType::PolityWandered: return "The " + polity_name(s, v) + " settle in new land" + place(sim, e.cell);
        case EventType::InstitutionFounded:
        case EventType::InstitutionLost: {
            const auto k = static_cast<std::size_t>(v % 16);
            const std::string realm = polity_name(s, v / 16);
            if (k >= kInstitutionCount) return "";
            const char* what = institution_name(static_cast<Institution>(k));
            return e.type == EventType::InstitutionFounded ? realm + " establishes a " + what
                                                           : realm + "'s " + what + " falls apart";
        }
        case EventType::Succession:
        case EventType::Usurpation:
            if (index < s.rulers.size()) {
                const Ruler& r = s.rulers[index];
                const int age = static_cast<int>((r.reign_start - r.born) / kTicksPerYear);
                const std::string realm = polity_name(s, r.polity);
                if (e.type == EventType::Usurpation) {
                    const std::uint32_t fig = r.figure < s.figures.size() ? r.figure : 0;
                    const std::uint32_t roles = fig != 0 ? s.figures[fig].roles : 0;
                    const std::string who = (roles & kRoleGeneral)  ? "General "
                                            : (roles & kRoleRebel) ? "The rebel leader "
                                                                   : "";
                    return who + ruler_name(r) + " seizes the throne of " + realm + ", founding the " +
                           (r.dynasty < s.dynasties.size() ? s.dynasties[r.dynasty].name : std::string("new house"));
                }
                return ruler_name(r) + " succeeds to the throne of " + realm + " at " + std::to_string(age) +
                       (age < RulerSystem::kAdultAge ? ", ruled for by a regent" : "");
            }
            return "";
        case EventType::BattleFought:
            if (index < s.battles.size()) {
                const Battle& b = s.battles[index];
                auto who = [&](std::uint32_t id) {
                    if (id == 0 || id >= s.armies.size()) return std::string("an army");
                    const Army& a = s.armies[id];
                    std::string led;
                    if (a.general != 0 && a.general < s.figures.size()) led = " under " + s.figures[a.general].name;
                    return "the army of " + polity_name(s, a.polity) + led;
                };
                std::string text = who(b.winner);
                text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
                return text + " defeats " + who(b.loser) + place(sim, b.cell) + " (" + commas(b.dead) + " dead)";
            }
            return "";
        case EventType::PeasantUprising:
            if (index < s.uprisings.size()) {
                const Uprising& u = s.uprisings[index];
                const std::string where = u.province != 0 && u.province < s.provinces.size()
                                              ? " in " + s.provinces[u.province].name
                                              : " in the heart of the realm";
                const std::string leader = u.leader != 0 && u.leader < s.figures.size() ? " under " + s.figures[u.leader].name : "";
                std::string how;
                switch (u.outcome) {
                    case UprisingOutcome::Crushed: how = ", and are crushed (" + commas(u.dead) + " dead)"; break;
                    case UprisingOutcome::BrokeAway:
                        how = " and break away" + (u.became != 0 ? " as " + polity_name(s, u.became) : std::string());
                        break;
                    case UprisingOutcome::Overthrew: how = " and overthrow the dynasty: their leader takes the throne"; break;
                }
                return commas(u.rebels) + " peasants rise against " + polity_name(s, u.polity) + where + leader + how;
            }
            return "";
        case EventType::ProvinceRevolt:
            if (index < s.provinces.size()) {
                const Province& pv = s.provinces[index];
                const std::string who = pv.governor != 0 && pv.governor < s.figures.size() ? s.figures[pv.governor].name : "Its governor";
                return who + ", governor of " + pv.name + ", breaks away from " + polity_name(s, pv.polity) +
                       (pv.became != 0 ? " as " + polity_name(s, pv.became) : std::string());
            }
            return "";
        case EventType::SuccessionDisputed:
            if (index < s.figures.size()) {
                const Figure& f = s.figures[index];
                // The heir crowned that year.
                std::string heir;
                for (std::size_t r = s.rulers.size(); r-- > 1;) {
                    if (s.rulers[r].polity == f.polity && s.rulers[r].reign_start == e.tick) {
                        heir = ruler_name(s.rulers[r]);
                        break;
                    }
                }
                std::string kin = "a kinsman";
                if (!heir.empty()) {
                    for (std::size_t r = s.rulers.size(); r-- > 1;) {
                        const Ruler& ru = s.rulers[r];
                        if (ru.polity != f.polity || ru.reign_start != e.tick || ru.figure >= s.figures.size()) continue;
                        const Figure& h = s.figures[ru.figure];
                        if (f.father != 0 && (f.father == h.father || f.mother == h.mother)) {
                            kin = f.born < h.born ? (f.female ? "elder sister" : "elder brother")
                                                  : (f.female ? "younger sister" : "younger brother");
                            kin = "the heir's " + kin;
                        }
                        break;
                    }
                }
                return f.name + ", " + kin + ", claims the throne of " + polity_name(s, f.polity) +
                       (heir.empty() ? "" : " against " + heir);
            }
            return "";
    }
    return "";
}

std::uint32_t event_figure(const SimState& s, const Event& e) {
    const auto index = static_cast<std::size_t>(e.value < 0 ? 0 : e.value);
    std::uint32_t f = 0;
    switch (e.type) {
        case EventType::Succession:
        case EventType::Usurpation: f = index < s.rulers.size() ? s.rulers[index].figure : 0; break;
        case EventType::SuccessionDisputed: f = static_cast<std::uint32_t>(index); break;
        case EventType::ProvinceRevolt: f = index < s.provinces.size() ? s.provinces[index].governor : 0; break;
        case EventType::PeasantUprising: f = index < s.uprisings.size() ? s.uprisings[index].leader : 0; break;
        case EventType::BattleFought:
            f = index < s.battles.size() && s.battles[index].winner < s.armies.size() ? s.armies[s.battles[index].winner].general : 0;
            break;
        case EventType::ReligionFounded: f = index < s.religions.size() ? s.religions[index].founder : 0; break;
        case EventType::Invention:
            for (const Invention& inv : s.inventions) {
                if (inv.tick == e.tick && inv.cell == e.cell && inv.technique == index) f = inv.figure;
            }
            break;
        default: break;
    }
    return f < s.figures.size() ? f : 0;
}

bool event_involves(const SimState& s, const Event& e, std::uint32_t p) {
    if (p == 0) return true;
    const auto v = e.value;
    const auto index = static_cast<std::size_t>(v < 0 ? 0 : v);
    switch (e.type) {
        case EventType::PolityFounded:
        case EventType::PolityEnded:
        case EventType::Absorbed:
        case EventType::StateConversion:
        case EventType::PolityWandered:
        case EventType::Raid:
        case EventType::ConquestDynasty: return v == p;
        case EventType::Subjugated:
        case EventType::Submitted: return v == p || (index < s.polities.size() && s.polities[index].overlord == p);
        case EventType::TributeWithheld:
        case EventType::Abandoned:
        case EventType::Betrayal:
            return static_cast<std::uint64_t>(v) >> 32 == p || (static_cast<std::uint64_t>(v) & 0xffffffffu) == p;
        case EventType::LawChanged: return static_cast<std::uint64_t>(v) >> 16 == p;
        case EventType::GovernorsRise:
        case EventType::RealmDivided: return v == p || (index < s.polities.size() && s.polities[index].parent == p);
        case EventType::WarBegan:
        case EventType::WarEnded:
            if (index < s.wars.size()) {
                const War& war = s.wars[index];
                return std::find(war.attackers.begin(), war.attackers.end(), p) != war.attackers.end() ||
                       std::find(war.defenders.begin(), war.defenders.end(), p) != war.defenders.end();
            }
            return false;
        case EventType::AllianceFormed:
            return index < s.alliances.size() && (s.alliances[index].a == p || s.alliances[index].b == p);
        case EventType::FolkMigration: return index < s.migrations.size() && s.migrations[index].polity == p;
        case EventType::ReligionFounded: return index < s.religions.size() && s.religions[index].polity == p;
        case EventType::Succession:
        case EventType::Usurpation: return index < s.rulers.size() && s.rulers[index].polity == p;
        case EventType::SuccessionDisputed: return index < s.figures.size() && s.figures[index].polity == p;
        case EventType::ProvinceRevolt:
            return index < s.provinces.size() && (s.provinces[index].polity == p || s.provinces[index].became == p);
        case EventType::PeasantUprising:
            return index < s.uprisings.size() && (s.uprisings[index].polity == p || s.uprisings[index].became == p);
        case EventType::BattleFought:
            if (index < s.battles.size()) {
                const Battle& b = s.battles[index];
                for (std::uint32_t a : {b.winner, b.loser}) {
                    if (a < s.armies.size() && s.armies[a].polity == p) return true;
                }
            }
            return false;
        case EventType::InstitutionFounded:
        case EventType::InstitutionLost: return v / 16 == p;
        default: return false;
    }
}

std::vector<TimelineEntry> timeline(const Simulation& sim, std::string_view kind, std::uint32_t polity, CellId cell,
                                    std::size_t max_entries) {
    std::vector<TimelineEntry> out;
    const SimState& s = sim.state();
    for (auto it = s.events.rbegin(); it != s.events.rend() && out.size() < max_entries; ++it) {
        const Event& e = *it;
        const char* k = event_kind(e.type);
        if (!kind.empty() && kind != k) continue;
        const bool small_battle = e.type == EventType::BattleFought && static_cast<std::size_t>(e.value) < s.battles.size() &&
                                  s.battles[static_cast<std::size_t>(e.value)].dead < 1000;
        const bool frequent =
            e.type == EventType::Raid || e.type == EventType::CellSettled || e.type == EventType::Succession || small_battle;
        if (kind.empty() && cell == kInvalidCell && polity == 0 && frequent) continue;
        if (cell != kInvalidCell && e.cell != cell) continue;
        if (polity != 0 && !event_involves(s, e, polity)) continue;
        std::string text = describe_event(sim, e);
        if (text.empty()) continue;
        out.push_back({e.tick / kTicksPerYear, k, std::move(text), e.cell, event_figure(s, e)});
    }
    return out;
}

void cell_uv(const World& w, CellId c, double& u, double& v) {
    u = v = 0.0;
    if (c >= w.cell_count()) return;
    const double lon = w.grid.longitude_deg_display(c), lat = w.grid.latitude_deg_display(c);
    u = (lon + 180.0) / 360.0;
    v = (90.0 - lat) / 180.0;
}

}  // namespace histsim::view
