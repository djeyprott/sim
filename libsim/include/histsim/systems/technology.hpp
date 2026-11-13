// Early technology (M2).
//
// Technology is knowledge held by populations, not a research bar: each
// cell's people know a set of techniques (data/techniques.json). Toward each
// technique whose requirements are met (earlier techniques, crops or herds,
// goods, the right place) they build know-how by doing what it grows from:
// accounts kept of surplus handed out lead to writing, hunting to the bow,
// farming crowded land to irrigation and terraces (practices(), below). Know-
// how grows faster in larger and more inventive communities whose ways suit
// it, and much faster beside neighbours who use the technique (faster with
// canoes, sails, horses and wheels); it fades when the practice stops. At 1
// the people know the technique. Small populations forget (Henrich 2004:
// Tasmania lost tools as it shrank and was cut off). Techniques act
// through the other systems: crop and herd yields, foraging and fishing,
// stores that soften bad harvests and famines.
#pragma once

#include <array>

#include "histsim/scheduler.hpp"

namespace histsim {

class TechnologySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "technology"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::CultureTech; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Know-how from a neighbour who uses a technique, per year, times
    // (0.5 + openness) x (1 + contact) x min(1, use) (people take up what they
    // have use for), for
    // 50,000 km^2 cells (x sqrt(50,000 / area) for smaller ones, so it spreads
    // as many km a year at any grid level): about 250 years to learn it.
    static constexpr double kLearning = 0.004;
    // A technique out of use fades (this chance a year) where practice would
    // keep less than kKeep of the know-how it takes: scribes with no
    // accounts to keep, sailors with nothing to carry.
    static constexpr double kKeep = 0.25;
    static constexpr double kDisuse = 0.01;
    // Know-how fades over this many years without practice (e-folding), and
    // in communities under kForgetBelowPeople over kMemoryYearsSmall.
    static constexpr double kMemoryYears = 300.0;
    static constexpr double kMemoryYearsSmall = 30.0;
    // Larger communities invent more: x sqrt(people / 10,000), at most this.
    static constexpr double kMaxPeopleFactor = 4.0;
    // Practices (practices(), each 0 to about 1 for a typical case, at most
    // kMaxPractice; crowding at most kMaxCrowding):
    //   foraging        share of food from foraging (1 - farming)
    //   hunting         that x game left (0..1)
    //   fishing         by a river or coast: 1 - farming / 2, else 0
    //   farming         share of food from crops
    //   herding         share of food from herds
    //   crowding        people / what the land feeds
    //   storing         food stored, in years per person
    //   towns           townspeople in the hinterland / kTownPeople
    //   trade           goods traded in the hinterland / (people x kTradePerPerson)
    //   redistribution  (town share / kTownShare + food brought in / (people x
    //                   kImportPerPerson)) x 1 at a realm's seat (kSeatTemple
    //                   with a temple priesthood), kRuled elsewhere in a
    //                   realm, kUnruled outside one
    //   rule            realm size in level-5 cells / kRealmCells, x kRuled
    //                   away from its seat
    static constexpr double kMaxPractice = 3.0;
    static constexpr double kMaxCrowding = 1.5;
    static constexpr double kTownPeople = 10000.0;
    static constexpr double kTradePerPerson = 0.05;
    static constexpr double kTownShare = 0.1;
    static constexpr double kImportPerPerson = 0.02;
    static constexpr double kSeatTemple = 1.5;
    static constexpr double kRuled = 0.4;
    static constexpr double kUnruled = 0.15;
    static constexpr double kRealmCells = 10.0;
    // Populations smaller than this lose each technique with this chance a year.
    static constexpr double kForgetBelowPeople = 500.0;
    static constexpr double kForgetting = 0.01;
    // Techniques whose requirements are gone (crops lost, say) fade faster.
    static constexpr double kFading = 0.05;
    // An invention is an independent origin if nobody this close knows it and
    // it was never invented this close before.
    static constexpr double kOriginKm = 1500.0;
};

// Whether a place suits a technique.
bool technique_place_ok(const World& world, CellId cell, TechPlace place);

// Whether a cell's people could use a technique: earlier techniques, crops,
// herds, species, goods and place.
bool technique_usable(const World& world, const TechniqueDef& technique, CellId cell, std::uint64_t techniques,
                      std::uint32_t domesticates);

// Whether domesticates include crops and herds that feed people, and what
// else the cell has.
struct Keeps {
    bool crops = false, herds = false;
    std::uint32_t goods = ~0u;      // goods on its market (all, if unknown)
};

// What a cell's people do, per TechPractice (see TechnologySystem). The
// inputs are counted over the cell's hinterland.
struct PracticeInputs {
    double community = 0.0;   // people
    double town = 0.0;        // townspeople
    double trade = 0.0;       // value of goods traded
    double imported = 0.0;    // food brought in (people fed)
    double realm_cells = 0.0; // its realm's land, in level-5 cells
    bool ruled = false;
    std::uint8_t seat = 0;    // 1 a realm's seat, 2 with a temple priesthood
};
std::array<double, kPracticeCount> practices(const World& world, const SimState& state, CellId cell,
                                             const PracticeInputs& in);
Keeps keeps(const World& world, std::uint32_t domesticates);
bool technique_usable(const World& world, const TechniqueDef& technique, CellId cell, std::uint64_t techniques,
                      std::uint32_t domesticates, Keeps kept);

// The combined effects of the techniques a cell's people know, where each
// applies.
TechEffects technique_effects(const World& world, CellId cell, std::uint64_t techniques);

// Techniques known in a cell (0 if the state has none yet).
[[nodiscard]] inline std::uint64_t techniques_in(const SimState& s, CellId c) {
    return s.techniques.empty() ? 0 : s.techniques[c];
}

// Goods on a cell's market this year (all, if the state has no economy).
[[nodiscard]] inline std::uint32_t goods_in(const SimState& s, CellId c) {
    return s.goods_available.empty() ? ~0u : s.goods_available[c];
}

}  // namespace histsim
