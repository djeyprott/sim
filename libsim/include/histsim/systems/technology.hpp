// Early technology (M2).
//
// Technology is knowledge held by populations, not a research bar: each
// cell's people know a set of techniques (data/techniques.json). Each year a
// technique whose requirements are met (earlier techniques, crops or herds,
// the right place) may be invented, more often in larger and more inventive
// populations whose ways suit it; neighbours teach what they know (faster with
// canoes, sails, horses and wheels); and small populations forget (Henrich
// 2004: Tasmania lost tools as it shrank and was cut off). Techniques act
// through the other systems: crop and herd yields, foraging and fishing,
// stores that soften bad harvests and famines.
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

class TechnologySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "technology"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::CultureTech; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Learning a technique from a neighbour who knows it, per year, times
    // (0.5 + openness) x (1 + contact), for 50,000 km^2 cells.
    static constexpr double kLearning = 0.004;
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
// herds, species and place.
bool technique_usable(const World& world, const TechniqueDef& technique, CellId cell, std::uint64_t techniques,
                      std::uint32_t domesticates);

// Whether domesticates include crops and herds that feed people.
struct Keeps {
    bool crops = false, herds = false;
};
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

}  // namespace histsim
