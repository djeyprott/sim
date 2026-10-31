#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "histsim/simulation.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

WorldParams small_world(std::uint64_t seed = 3) {
    WorldParams p;
    p.seed = seed;
    p.grid_level = 4;
    return p;
}

std::string temp_path(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace

TEST(sim_same_seed_same_history) {
    Simulation a = Simulation::create(small_world());
    Simulation b = Simulation::create(small_world());
    for (int i = 0; i < 10; ++i) {
        a.step_years(10);
        b.step_years(10);
        CHECK_EQ(a.state_hash(), b.state_hash());
    }
}

TEST(sim_different_seed_different_history) {
    Simulation a = Simulation::create(small_world(3));
    Simulation b = Simulation::create(small_world(4));
    a.step_years(20);
    b.step_years(20);
    CHECK(a.state_hash() != b.state_hash());
}

TEST(sim_people_are_conserved) {
    Simulation sim = Simulation::create(small_world());
    for (int i = 0; i < 20; ++i) {
        sim.step_years(5);
        const Ledger& l = sim.state().ledger;
        CHECK_EQ(sim.state().total_population(), l.initial_population + l.births - l.deaths + l.added);
    }
    CHECK(sim.state().ledger.migrants > 0);
}

TEST(sim_systems_run_yearly_only) {
    Simulation sim = Simulation::create(small_world());
    const std::uint64_t start = sim.state_hash();
    sim.step_ticks(1);  // tick 0: yearly systems run
    CHECK(sim.state().ledger.births > 0);
    const std::int64_t births = sim.state().ledger.births;
    sim.step_ticks(11);  // ticks 1..11: nothing is due
    CHECK_EQ(sim.state().ledger.births, births);
    CHECK_EQ(sim.year(), std::int64_t{1});
    CHECK(sim.state_hash() != start);
}

TEST(sim_save_and_resume_matches_uninterrupted_run) {
    const std::string path = temp_path("histsim_test_resume.hsim");
    Simulation straight = Simulation::create(small_world());
    straight.step_years(60);

    Simulation first = Simulation::create(small_world());
    first.step_years(25);
    first.save(path);
    Simulation resumed = Simulation::load(path);
    CHECK_EQ(resumed.state_hash(), first.state_hash());
    resumed.step_years(35);
    CHECK_EQ(resumed.state_hash(), straight.state_hash());
    std::filesystem::remove(path);
}

TEST(sim_resume_mid_year_matches) {
    const std::string path = temp_path("histsim_test_midyear.hsim");
    Simulation straight = Simulation::create(small_world());
    straight.step_ticks(12 * 10 + 5);
    Simulation first = Simulation::create(small_world());
    first.step_ticks(12 * 4 + 7);
    first.save(path);
    Simulation resumed = Simulation::load(path);
    resumed.step_ticks(12 * 6 - 2);
    CHECK_EQ(resumed.tick(), straight.tick());
    CHECK_EQ(resumed.state_hash(), straight.state_hash());
    std::filesystem::remove(path);
}

TEST(sim_rejects_corrupt_snapshots) {
    const std::string path = temp_path("histsim_test_corrupt.hsim");
    Simulation sim = Simulation::create(small_world());
    sim.step_years(3);
    sim.save(path);

    std::vector<char> bytes;
    {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(bytes.size() > 100);
    bytes[bytes.size() / 2] ^= 0x01;  // flip one bit
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(throws([&] { (void)Simulation::load(path); }));

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), 20);  // truncated
    }
    CHECK(throws([&] { (void)Simulation::load(path); }));
    CHECK(throws([] { (void)Simulation::load("/nonexistent/dir/none.hsim"); }));
    std::filesystem::remove(path);
}

TEST(sim_events_are_in_tick_order) {
    Simulation sim = Simulation::create(small_world());
    sim.step_years(300);
    const auto& events = sim.state().events;
    for (std::size_t i = 1; i < events.size(); ++i) {
        CHECK(events[i - 1].tick <= events[i].tick);
    }
}

// Golden hash: pins the exact output of a fixed run. CI runs this on Linux,
// macOS and Windows with GCC, Clang and MSVC, so a pass everywhere means the
// sim is bit-identical across platforms.
//
// When you change simulation behavior on purpose, update the constant: run
// `histsim run --seed 1 --level 5 --years 200 --hash-only` and paste the
// output here, and say so in the commit message.
TEST(sim_golden_hash) {
    WorldParams p;
    p.seed = 1;
    p.grid_level = 5;
    Simulation sim = Simulation::create(p);
    sim.step_years(200);
    CHECK_EQ(sim.state_hash(), 0x28abe1a2c69f1e75ull);
}
