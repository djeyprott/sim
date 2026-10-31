#include "histsim/simulation.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "histsim/hash.hpp"
#include "histsim/systems/climate.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/disease.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/technology.hpp"
#include "histsim/systems/people.hpp"
#include "state_io.hpp"

namespace histsim {

namespace {

constexpr char kMagic[4] = {'H', 'S', 'I', 'M'};

std::uint64_t checksum(const std::uint8_t* data, std::size_t size) {
    Hasher h;
    h.bytes(data, size);
    return h.digest();
}

}  // namespace

void Simulation::install_systems() {
    scheduler_ = Scheduler{};
    scheduler_.add(std::make_unique<ClimateSystem>());
    scheduler_.add(std::make_unique<EnvironmentSystem>());
    scheduler_.add(std::make_unique<FarmingSystem>());
    scheduler_.add(std::make_unique<DiseaseSystem>());  // sets this year's disease mortality
    scheduler_.add(std::make_unique<DemographySystem>());
    scheduler_.add(std::make_unique<MigrationSystem>());
    scheduler_.add(std::make_unique<CultureSystem>());
    scheduler_.add(std::make_unique<TechnologySystem>());
    scheduler_.add(std::make_unique<HistorySystem>());
}

Simulation Simulation::create(const WorldParams& params) {
    Simulation sim;
    sim.world_ = generate_test_world(params);
    const World& w = sim.world_;
    SimState& s = sim.state_;
    const std::size_t n = w.cell_count();

    // Climate starts at the baseline: no anomalies, full game stocks.
    s.climate.regional_temp_anomaly.assign(w.climate_grid.cell_count(), 0.0);
    s.climate.regional_precip_anomaly.assign(w.climate_grid.cell_count(), 0.0);
    s.env.temperature_c = w.temperature_c;
    s.env.precipitation_mm = w.precipitation_mm;
    s.env.capacity = w.forager_capacity;
    s.climate.next_bond_year = first_bond_year(params.seed);
    // Nobody farms yet: the wild ancestors are there to be domesticated.
    s.env.farming.assign(n, 0.0);
    s.env.farm_capacity.assign(n, 0.0);
    s.domesticates.assign(n, 0);
    s.domestication.assign(w.wild_stands.size(), 0.0);
    s.env.game.assign(n, 1.0);
    for (CellId c = 0; c < n; ++c) {
        if (!w.is_land[c]) s.env.game[c] = 0.0;
    }

    // Foragers at half of capacity, in a stationary age structure. Cells too
    // poor to hold a band of two start empty and are settled by migration.
    s.ever_settled.assign(n, 0);
    for (CellId c = 0; c < n; ++c) {
        const auto people = static_cast<std::uint32_t>(0.5 * w.forager_capacity[c]);
        if (people >= 2) {
            s.cohorts.push_back(make_stable_cohort(c, w.initial_group[c], w.initial_group[c], people));
            s.ever_settled[c] = 1;
        }
    }
    s.normalize_cohorts(n);
    s.ledger.initial_population = s.total_population();

    // Each starting group is a people of its own: a culture, and a language
    // family that has already been diversifying for thousands of years.
    std::vector<std::vector<CellId>> group_cells(w.group_count + 1);
    for (CellId c = 0; c < n; ++c) {
        if (w.is_land[c]) group_cells[w.initial_group[c]].push_back(c);
    }
    std::vector<std::uint32_t> cell_culture(n, 0), cell_language(n, 0);
    std::vector<std::array<double, kMaxTraits>> traits(w.group_count + 1);
    for (std::uint32_t g = 1; g <= w.group_count; ++g) {
        RngStream rng(params.seed, 0, Stream::Language, g);
        const double history = kFamilyHistoryMinYears + rng.uniform() * kFamilyHistoryRangeYears;
        const FoundedPeople p = found_people(w, s, params.seed, g, group_cells[g], history);
        traits[g] = p.traits;
        for (std::size_t i = 0; i < group_cells[g].size(); ++i) {
            cell_culture[group_cells[g][i]] = p.culture;
            cell_language[group_cells[g][i]] = p.cell_language[i];
        }
    }
    for (Cohort& co : s.cohorts) {
        co.traits = traits[co.culture];  // still the group number here
        co.culture = cell_culture[co.cell];
        co.language = cell_language[co.cell];
    }
    s.normalize_cohorts(n);

    // Techniques: some starting peoples already know the old forager ones
    // (the bow, nets, canoes), where they can use them.
    s.techniques.assign(n, 0);
    for (std::uint32_t g = 1; g <= w.group_count; ++g) {
        RngStream rng(params.seed, 0, Stream::Technology, 0x80000000u | g);
        std::uint64_t knows = 0;
        for (std::size_t k = 0; k < w.content->techniques.size(); ++k) {
            if (rng.uniform() < w.content->techniques[k].start_chance) knows |= std::uint64_t{1} << k;
        }
        for (CellId c : group_cells[g]) {
            if (s.cell_population[c] == 0) continue;
            for (std::size_t k = 0; k < w.content->techniques.size(); ++k) {
                if (((knows >> k) & 1u) && technique_usable(w, w.content->techniques[k], c, knows, 0)) {
                    s.techniques[c] |= std::uint64_t{1} << k;
                }
            }
        }
    }

    // Diseases: nobody has immunity; endemic diseases that were already
    // established are present in their niche.
    const auto& diseases = w.content->diseases;
    s.disease_present.assign(n * diseases.size(), 0);
    s.susceptible.assign(n * diseases.size(), 1.0);
    s.disease_deaths.assign(n, 0);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        for (std::size_t d = 0; d < diseases.size(); ++d) {
            if (diseases[d].present_at_start && disease_suitability(w, diseases[d], c) > 0.0) {
                s.disease_present[c * diseases.size() + d] = 1;
            }
        }
    }

    // Species tamed long before the run (dogs) are known where they are
    // native and people live.
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        for (std::size_t sp = 0; sp < w.content->species.size(); ++sp) {
            if (w.content->species[sp].domesticated_before_start && ((w.species_mask[c] >> sp) & 1u)) {
                s.domesticates[c] |= 1u << sp;
            }
        }
    }
    sim.install_systems();
    return sim;
}

InterventionResult Simulation::apply(const Command& command) {
    return apply_command(world_, state_, world_.params.seed, command);
}

void Simulation::step_ticks(Tick ticks) {
    for (Tick i = 0; i < ticks; ++i) {
        scheduler_.run_tick(world_, state_, world_.params.seed);
    }
}

RunSummary Simulation::summary() const {
    RunSummary r;
    r.tick = state_.tick;
    r.population = state_.total_population();
    for (std::uint32_t p : state_.cell_population) {
        r.inhabited_cells += p > 0 ? 1 : 0;
    }
    r.births = state_.ledger.births;
    r.deaths = state_.ledger.deaths;
    r.migrants = state_.ledger.migrants;
    r.state_hash = state_hash();
    return r;
}

// Snapshot layout (little-endian): "HSIM" | u32 version | u64 seed | u32 grid
// level | f64 ocean fraction | u64 world hash | SimState (see state_io.hpp)
// | u64 checksum of everything before it.
void Simulation::save(const std::string& path) const {
    io::WriteArchive ar;
    ar.raw(kMagic, sizeof kMagic);
    std::uint32_t version = kSnapshotVersion;
    ar.u32(version);
    std::uint64_t seed = world_.params.seed;
    ar.count(seed);
    std::uint32_t level = static_cast<std::uint32_t>(world_.params.grid_level);
    ar.u32(level);
    double ocean = world_.params.ocean_fraction;
    ar.f64(ocean);
    std::uint64_t world_hash = world_.hash();
    ar.count(world_hash);
    io::serialize(ar, const_cast<SimState&>(state_));  // the writer only reads
    const std::uint64_t sum = checksum(ar.bytes().data(), ar.bytes().size());
    ar.u64(sum);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    out.write(reinterpret_cast<const char*>(ar.bytes().data()), static_cast<std::streamsize>(ar.bytes().size()));
    if (!out) {
        throw std::runtime_error("failed writing " + path);
    }
}

Simulation Simulation::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof kMagic + 8) {
        throw std::runtime_error(path + " is not a histsim snapshot");
    }
    const std::size_t body = bytes.size() - 8;
    io::ReadArchive tail(bytes.data() + body, 8);
    if (tail.u64() != checksum(bytes.data(), body)) {
        throw std::runtime_error(path + " is corrupt (checksum mismatch)");
    }

    io::ReadArchive ar(bytes.data(), body);
    char magic[4];
    ar.raw(magic, sizeof magic);
    if (std::memcmp(magic, kMagic, sizeof kMagic) != 0) {
        throw std::runtime_error(path + " is not a histsim snapshot");
    }
    std::uint32_t version = 0;
    ar.u32(version);
    if (version != kSnapshotVersion) {
        throw std::runtime_error("unsupported snapshot version " + std::to_string(version) + " (this build reads " +
                                 std::to_string(kSnapshotVersion) + ")");
    }
    WorldParams params;
    params.seed = ar.u64();
    std::uint32_t level = 0;
    ar.u32(level);
    params.grid_level = static_cast<int>(level);
    ar.f64(params.ocean_fraction);
    const std::uint64_t world_hash = ar.u64();

    Simulation sim;
    sim.world_ = generate_test_world(params);
    if (sim.world_.hash() != world_hash) {
        throw std::runtime_error("regenerated world does not match the snapshot (different build or platform?)");
    }
    io::serialize(ar, sim.state_);
    if (!ar.done()) {
        throw std::runtime_error(path + " has trailing data");
    }
    const std::size_t n = sim.world_.cell_count();
    for (const Cohort& c : sim.state_.cohorts) {
        if (c.cell >= n) throw std::runtime_error(path + " is corrupt (cohort outside the world)");
    }
    if (sim.state_.env.capacity.size() != n || sim.state_.ever_settled.size() != n ||
        sim.state_.climate.regional_temp_anomaly.size() != sim.world_.climate_grid.cell_count()) {
        throw std::runtime_error(path + " does not match the world's size");
    }
    sim.state_.refresh(n);
    sim.install_systems();
    return sim;
}

}  // namespace histsim
