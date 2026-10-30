#include "histsim/interventions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "histsim/rng.hpp"
#include "histsim/systems/climate.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/people.hpp"

namespace histsim {

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

std::string fmt(const char* format, double v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

InterventionResult fail(std::string message) {
    InterventionResult r;
    r.ok = false;
    r.message = std::move(message);
    return r;
}

// Kill a share of everyone in the given cells. Returns deaths.
std::int64_t kill_share(SimState& s, const std::vector<WeightedCell>& cells, double share, RngStream& rng) {
    std::int64_t killed = 0;
    for (const WeightedCell& wc : cells) {
        for (auto i = s.cohort_begin[wc.cell]; i < s.cohort_begin[wc.cell + 1]; ++i) {
            Cohort& co = s.cohorts[i];
            for (int b = 0; b < kAgeBins; ++b) {
                const auto df = share >= 1.0 ? co.female[b]
                                             : std::min<std::int64_t>(co.female[b], stochastic_round(share * co.female[b], rng));
                const auto dm = share >= 1.0 ? co.male[b]
                                             : std::min<std::int64_t>(co.male[b], stochastic_round(share * co.male[b], rng));
                co.female[b] -= static_cast<std::uint32_t>(df);
                co.male[b] -= static_cast<std::uint32_t>(dm);
                killed += df + dm;
            }
        }
    }
    return killed;
}

// This year's climate and capacity for the given cells, after a change to
// their climate or game. Mirrors what the climate and environment systems
// would compute, so the map responds at once instead of next year.
void refresh_capacity(const World& w, SimState& s, const std::vector<WeightedCell>& cells) {
    for (const WeightedCell& wc : cells) {
        const CellId c = wc.cell;
        if (!w.is_land[c]) continue;
        s.env.capacity[c] = environment_capacity(w, s, c);
    }
}

}  // namespace

const char* command_name(CommandType type) {
    switch (type) {
        case CommandType::Eruption: return "Volcanic eruption";
        case CommandType::ClimateForcing: return "Climate forcing";
        case CommandType::RegionalClimate: return "Regional climate";
        case CommandType::Plague: return "Plague";
        case CommandType::Settlers: return "Settlers";
        case CommandType::WipeOut: return "Wipe out";
        case CommandType::SetGame: return "Set game";
    }
    return "Unknown";
}

std::vector<WeightedCell> cells_within(const World& w, CellId center, double radius_km, bool taper) {
    std::vector<WeightedCell> out;
    if (center >= w.cell_count()) return out;
    radius_km = std::clamp(radius_km, 0.0, kMaxRadiusKm);
    out.push_back({center, 1.0});
    if (radius_km <= 0.0) return out;
    const double inner = 0.7 * radius_km;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (c == center) continue;
        const double d = w.grid.distance_km(center, c);
        if (d > radius_km) continue;
        const double weight = (!taper || d <= inner) ? 1.0 : (radius_km - d) / (radius_km - inner);
        out.push_back({c, weight});
    }
    return out;
}

InterventionResult apply_command(const World& w, SimState& s, std::uint64_t seed, Command cmd) {
    const std::size_t n = w.cell_count();
    const bool needs_cell = cmd.type != CommandType::Eruption && cmd.type != CommandType::ClimateForcing;
    if (needs_cell && cmd.cell >= n) {
        return fail("Pick a cell on the map first.");
    }
    if (!needs_cell) cmd.cell = kInvalidCell;
    cmd.tick = s.tick;
    cmd.radius_km = std::clamp(cmd.radius_km, 0.0, kMaxRadiusKm);

    // One random stream per command, keyed by its position in the log.
    RngStream rng(seed, s.tick, Stream::Intervention, static_cast<std::uint32_t>(s.commands.size()));
    InterventionResult r;
    r.ok = true;

    switch (cmd.type) {
        case CommandType::Eruption: {
            cmd.amount = -std::clamp(std::fabs(cmd.amount), 0.1, kMaxEruptionWm2);
            s.climate.volcanic_forcing += cmd.amount;
            s.events.push_back({s.tick, EventType::Eruption, kInvalidCell, static_cast<std::int64_t>(cmd.amount * 1000.0)});
            r.message = "Volcanic eruption: " + fmt("%.1f W/m²", cmd.amount) +
                        " of forcing; the planet cools over the next year or two.";
            break;
        }
        case CommandType::ClimateForcing: {
            cmd.amount = std::clamp(cmd.amount, -kMaxImposedForcingWm2, kMaxImposedForcingWm2);
            s.climate.imposed_forcing = cmd.amount;
            r.message = "Climate forcing set to " + fmt("%+.1f W/m²", cmd.amount) + " (about " +
                        fmt("%+.1f °C", cmd.amount / ClimateSystem::kFeedback) + " at equilibrium, reached over decades).";
            break;
        }
        case CommandType::RegionalClimate: {
            cmd.amount = std::clamp(cmd.amount, -kMaxRegionalTempC, kMaxRegionalTempC);
            cmd.amount2 = std::clamp(cmd.amount2, -1.0, 3.0);
            cmd.years = std::clamp<std::int64_t>(cmd.years, 1, kMaxModifierYears);
            const auto cells = cells_within(w, cmd.cell, cmd.radius_km, /*taper=*/true);
            // This year at once, then the climate system applies it for the
            // remaining years.
            for (const WeightedCell& wc : cells) {
                s.env.temperature_c[wc.cell] += wc.weight * cmd.amount;
                s.env.precipitation_mm[wc.cell] *= std::max(0.0, 1.0 + wc.weight * cmd.amount2);
            }
            refresh_capacity(w, s, cells);
            if (cmd.years > 1) {
                s.climate.modifiers.push_back({cmd.cell, cmd.radius_km, cmd.amount, cmd.amount2, cmd.years - 1});
            }
            r.cells = cells.size();
            r.message = "Regional climate: " + fmt("%+.1f °C", cmd.amount) + ", rainfall " +
                        fmt("%+.0f%%", 100.0 * cmd.amount2) + " over " + commas(static_cast<std::int64_t>(r.cells)) +
                        " cells for " + commas(cmd.years) + (cmd.years == 1 ? " year." : " years.");
            break;
        }
        case CommandType::Plague:
        case CommandType::WipeOut: {
            if (cmd.type == CommandType::WipeOut) cmd.amount = 1.0;
            cmd.amount = std::clamp(cmd.amount, 0.0, 1.0);
            const auto cells = cells_within(w, cmd.cell, cmd.radius_km, /*taper=*/false);
            r.people = kill_share(s, cells, cmd.amount, rng);
            r.cells = cells.size();
            s.ledger.deaths += r.people;
            s.normalize_cohorts(n);
            r.message = std::string(cmd.type == CommandType::Plague ? "Plague" : "Wipe out") + ": " +
                        commas(r.people) + " people died in " + commas(static_cast<std::int64_t>(r.cells)) +
                        (r.cells == 1 ? " cell." : " cells.");
            break;
        }
        case CommandType::Settlers: {
            if (!w.is_land[cmd.cell]) {
                return fail("Settlers need land: pick a land cell.");
            }
            cmd.amount = std::clamp(std::floor(cmd.amount), 1.0, static_cast<double>(kMaxSettlers));
            const auto people = static_cast<std::uint32_t>(cmd.amount);
            const CellId home[] = {cmd.cell};
            const FoundedPeople founded = found_people(w, s, seed, 0x20000000u | static_cast<std::uint32_t>(s.commands.size()), home,
                                                       /*history_years=*/0.0);
            const std::uint32_t language = founded.family;
            Cohort settlers = make_stable_cohort(cmd.cell, founded.culture, language, people);
            settlers.traits = founded.traits;
            s.cohorts.push_back(settlers);
            s.ledger.added += people;
            s.normalize_cohorts(n);
            if (!s.ever_settled[cmd.cell]) {
                s.ever_settled[cmd.cell] = 1;
                s.events.push_back({s.tick, EventType::CellSettled, cmd.cell, people});
            }
            r.people = people;
            r.cells = 1;
            r.message = "Settlers: " + commas(people) + " people speaking " + s.languages[language].name +
                        " arrived.";
            break;
        }
        case CommandType::SetGame: {
            cmd.amount = std::clamp(cmd.amount, 0.02, 1.0);
            const auto cells = cells_within(w, cmd.cell, cmd.radius_km, /*taper=*/false);
            for (const WeightedCell& wc : cells) {
                if (w.is_land[wc.cell]) s.env.game[wc.cell] = cmd.amount;
            }
            refresh_capacity(w, s, cells);
            r.cells = cells.size();
            r.message = "Game set to " + fmt("%.0f%%", 100.0 * cmd.amount) + " in " +
                        commas(static_cast<std::int64_t>(r.cells)) + (r.cells == 1 ? " cell." : " cells.");
            break;
        }
        default:
            return fail("Unknown intervention.");
    }

    s.events.push_back({s.tick, EventType::Intervention, cmd.cell, static_cast<std::int64_t>(cmd.type)});
    s.commands.push_back(cmd);
    return r;
}

}  // namespace histsim
