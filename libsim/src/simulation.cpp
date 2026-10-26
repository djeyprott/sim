#include "histsim/simulation.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "histsim/hash.hpp"
#include "histsim/systems/foragers.hpp"

namespace histsim {

namespace {

constexpr char kMagic[4] = {'H', 'S', 'I', 'M'};

// Little-endian writer and reader, independent of the host byte order.
class Writer {
public:
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}
    void raw(void* out, std::size_t n) {
        need(n);
        std::memcpy(out, p_, n);
        p_ += n;
    }
    std::uint8_t u8() {
        need(1);
        return *p_++;
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(*p_++) << (8 * i);
        return v;
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(*p_++) << (8 * i);
        return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    double f64() {
        const std::uint64_t bits = u64();
        double v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    [[nodiscard]] bool done() const { return p_ == end_; }

private:
    void need(std::size_t n) const {
        if (static_cast<std::size_t>(end_ - p_) < n) {
            throw std::runtime_error("snapshot is truncated");
        }
    }
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

std::uint64_t checksum(const std::uint8_t* data, std::size_t size) {
    Hasher h;
    h.bytes(data, size);
    return h.digest();
}

}  // namespace

void Simulation::install_systems() {
    scheduler_ = Scheduler{};
    scheduler_.add(std::make_unique<ForagerDemography>());
    scheduler_.add(std::make_unique<ForagerMigration>());
}

Simulation Simulation::create(const WorldParams& params) {
    Simulation sim;
    sim.world_ = generate_test_world(params);
    const std::size_t n = sim.world_.cell_count();

    // Start at half of carrying capacity; cells too poor to hold a band of
    // two start empty and are settled by migration.
    sim.state_.population.assign(n, 0);
    sim.state_.ever_settled.assign(n, 0);
    auto& pop = sim.state_.population.current();
    std::int64_t total = 0;
    for (CellId c = 0; c < n; ++c) {
        const auto people = static_cast<std::uint32_t>(0.5 * sim.world_.forager_capacity[c]);
        if (people >= 2) {
            pop[c] = people;
            sim.state_.ever_settled[c] = 1;
            total += people;
        }
    }
    sim.state_.ledger.initial_population = total;
    sim.install_systems();
    return sim;
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
    for (std::uint32_t p : state_.population.read()) {
        r.inhabited_cells += p > 0 ? 1 : 0;
    }
    r.births = state_.ledger.births;
    r.deaths = state_.ledger.deaths;
    r.migrants = state_.ledger.migrants;
    r.state_hash = state_hash();
    return r;
}

// Snapshot layout (all little-endian):
//   "HSIM" | u32 version | u64 seed | u32 grid level | f64 ocean fraction
//   | u64 world hash | i64 tick | 4 x i64 ledger | u64 n | n x u32 population
//   | n x u8 ever_settled | u64 events | events (i64 tick, u32 type, u32 cell,
//   i64 value) | u64 checksum of everything before it
void Simulation::save(const std::string& path) const {
    Writer w;
    w.raw(kMagic, sizeof kMagic);
    w.u32(kSnapshotVersion);
    w.u64(world_.params.seed);
    w.u32(static_cast<std::uint32_t>(world_.params.grid_level));
    w.f64(world_.params.ocean_fraction);
    w.u64(world_.hash());
    w.i64(state_.tick);
    w.i64(state_.ledger.initial_population);
    w.i64(state_.ledger.births);
    w.i64(state_.ledger.deaths);
    w.i64(state_.ledger.migrants);
    const auto pop = state_.population.read();
    w.u64(pop.size());
    for (std::uint32_t p : pop) w.u32(p);
    for (std::uint8_t s : state_.ever_settled) w.u8(s);
    w.u64(state_.events.size());
    for (const Event& e : state_.events) {
        w.i64(e.tick);
        w.u32(static_cast<std::uint32_t>(e.type));
        w.u32(e.cell);
        w.i64(e.value);
    }
    const std::uint64_t sum = checksum(w.bytes().data(), w.bytes().size());
    w.u64(sum);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    out.write(reinterpret_cast<const char*>(w.bytes().data()), static_cast<std::streamsize>(w.bytes().size()));
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
    Reader tail(bytes.data() + body, 8);
    if (tail.u64() != checksum(bytes.data(), body)) {
        throw std::runtime_error(path + " is corrupt (checksum mismatch)");
    }

    Reader r(bytes.data(), body);
    char magic[4];
    r.raw(magic, sizeof magic);
    if (std::memcmp(magic, kMagic, sizeof kMagic) != 0) {
        throw std::runtime_error(path + " is not a histsim snapshot");
    }
    const std::uint32_t version = r.u32();
    if (version != kSnapshotVersion) {
        throw std::runtime_error("unsupported snapshot version " + std::to_string(version));
    }

    WorldParams params;
    params.seed = r.u64();
    params.grid_level = static_cast<int>(r.u32());
    params.ocean_fraction = r.f64();
    const std::uint64_t world_hash = r.u64();

    Simulation sim;
    sim.world_ = generate_test_world(params);
    if (sim.world_.hash() != world_hash) {
        throw std::runtime_error("regenerated world does not match the snapshot (different build or platform?)");
    }

    SimState& s = sim.state_;
    s.tick = r.i64();
    s.ledger.initial_population = r.i64();
    s.ledger.births = r.i64();
    s.ledger.deaths = r.i64();
    s.ledger.migrants = r.i64();
    const std::uint64_t n = r.u64();
    if (n != sim.world_.cell_count()) {
        throw std::runtime_error("snapshot cell count does not match the world");
    }
    s.population.assign(n, 0);
    for (auto& p : s.population.current()) p = r.u32();
    s.ever_settled.resize(n);
    for (auto& v : s.ever_settled) v = r.u8();
    const std::uint64_t event_count = r.u64();
    s.events.reserve(event_count);
    for (std::uint64_t i = 0; i < event_count; ++i) {
        Event e;
        e.tick = r.i64();
        e.type = static_cast<EventType>(r.u32());
        e.cell = r.u32();
        e.value = r.i64();
        s.events.push_back(e);
    }
    if (!r.done()) {
        throw std::runtime_error(path + " has trailing data");
    }
    sim.install_systems();
    return sim;
}

}  // namespace histsim
