#include "histsim/view/timeline.hpp"

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
    }
    return "";
}

}  // namespace

const char* event_kind(EventType type) {
    switch (type) {
        case EventType::PolityFounded:
        case EventType::PolityEnded:
        case EventType::Subjugated:
        case EventType::Absorbed:
        case EventType::ConquestDynasty:
        case EventType::CityFounded:
        case EventType::PolityWandered:
        case EventType::Succession:
        case EventType::Usurpation:
        case EventType::InstitutionFounded:
        case EventType::InstitutionLost: return "realms";
        case EventType::WarBegan:
        case EventType::WarEnded:
        case EventType::AllianceFormed:
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
                    who = " by the " + s.cultures[inv.culture].name;
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
                return "The " + war.name + " ends" + how + " (" + commas(war.deaths) + " dead)";
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
            return index < s.religions.size() ? "The " + s.religions[index].name + " arises" + place(sim, e.cell) : "";
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
                    return ruler_name(r) + " seizes the throne of " + realm + ", founding the " +
                           (r.dynasty < s.dynasties.size() ? s.dynasties[r.dynasty].name : std::string("new house"));
                }
                return ruler_name(r) + " succeeds to the throne of " + realm + " at " + std::to_string(age) +
                       (age < RulerSystem::kAdultAge ? ", ruled for by a regent" : "");
            }
            return "";
    }
    return "";
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
        case EventType::Subjugated: return v == p || (index < s.polities.size() && s.polities[index].overlord == p);
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
        const bool frequent = e.type == EventType::Raid || e.type == EventType::CellSettled || e.type == EventType::Succession;
        if (kind.empty() && cell == kInvalidCell && polity == 0 && frequent) continue;
        if (cell != kInvalidCell && e.cell != cell) continue;
        if (polity != 0 && !event_involves(s, e, polity)) continue;
        std::string text = describe_event(sim, e);
        if (text.empty()) continue;
        out.push_back({e.tick / kTicksPerYear, k, std::move(text), e.cell});
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
