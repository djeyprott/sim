#pragma once

// Diplomacy: how realms regard each other, and what they remember.
//
// Each pair of realms that border each other, are bound (tribute,
// vassalage) or allied, or fought within living memory, has a Relation:
// an opinion from hostile (-1) to friendly (1) and a trust (0-1). Opinion
// moves toward a target each year (kDrift of the gap) and is jolted by
// events (a war begun). The target:
//   + kCultureOpinion  if their founders' cultures share a root
//   + kFaithOpinion    if their rulers share a faith; else - kZealOpinion x
//                      the zeal of their high priests (piety above a half)
//   + kTradeOpinion    x how much both capitals live by trade
//   - kWarOpinion      x the memory of their last war (fading:
//                      kMemoryYears / (kMemoryYears + years since))
//   - kClaimOpinion    per cell either claims of the other's (at most
//                      kMaxClaimOpinion)
//   + kAllyOpinion     if allied; - kBondOpinion if one pays the other
// Land lost in war is claimed by the loser for kClaimYears, unless it is
// won back first. A claim or a holy war (a zealous high priest against
// another faith) gives a realm cause for war. (Hostility alone does not:
// the victors of the last war would then have the most cause for the
// next, and conquest would snowball; it weighs through opinion instead.)

#include <cstdint>
#include <vector>

#include "histsim/state.hpp"
#include "histsim/world.hpp"

namespace histsim {

struct Diplomacy {
    static constexpr double kDrift = 0.1;
    static constexpr double kCultureOpinion = 0.3;
    static constexpr double kFaithOpinion = 0.2;
    static constexpr double kZealOpinion = 0.4;
    static constexpr double kTradeOpinion = 0.2;
    static constexpr double kWarOpinion = 0.7;
    static constexpr double kMemoryYears = 25.0;
    static constexpr double kClaimOpinion = 0.15;
    static constexpr double kMaxClaimOpinion = 0.6;
    static constexpr double kAllyOpinion = 0.3;
    static constexpr double kBondOpinion = 0.2;
    static constexpr double kWarShock = 0.3;   // opinion lost when a war begins
    static constexpr double kForgetYears = 150.0;  // relations of realms no longer in touch are kept this long after a war
    static constexpr std::int64_t kClaimYears = 100;
    static constexpr double kHolyPiety = 0.7;  // a high priest this pious preaches holy war on other faiths
    // Declaring war: the chance is scaled by kCause with cause for war,
    // kNoCause without, and by (1 - kOpinionWeight x opinion). A realm judges
    // its strength by what it can bring to the enemy's capital: its power
    // x clamp(control there / kProjection, kMinProjection, 1).
    static constexpr double kCause = 3.0;
    static constexpr double kNoCause = 1.5;
    static constexpr double kOpinionWeight = 0.5;
    static constexpr double kProjection = 0.2;
    static constexpr double kMinProjection = 0.85;
};

// The relation between two realms, if they have one.
[[nodiscard]] const Relation* find_relation(const SimState& state, std::uint32_t a, std::uint32_t b);
[[nodiscard]] Relation& relation_of(SimState& state, std::uint32_t a, std::uint32_t b, Tick now);
// Their opinion of each other (0 if they have no relation).
[[nodiscard]] double opinion_between(const SimState& state, std::uint32_t a, std::uint32_t b);
// Cells of `of` that `by` claims.
[[nodiscard]] std::uint32_t claims_on(const SimState& state, std::uint32_t by, std::uint32_t of);
// Whether `a` has cause for war on `b`: a claim or holy war.
[[nodiscard]] bool cause_for_war(const SimState& state, std::uint32_t a, std::uint32_t b);
// The opinion two realms drift toward this year.
[[nodiscard]] double opinion_target(const World& world, const SimState& state, const Relation& r, Tick now);

// Yearly (WarSystem): claims lapse or are won back; relations are kept for
// the realms in touch and drift toward their targets. `borders` lists each
// living polity's neighbours (sorted).
void update_relations(const World& world, SimState& state, Tick now,
                      const std::vector<std::vector<std::uint32_t>>& borders);
// A war began between two realms; ended.
void note_war_begun(SimState& state, std::uint32_t a, std::uint32_t b, Tick now);
void note_war_ended(SimState& state, std::uint32_t a, std::uint32_t b, Tick now);
// `loser` lost `cell` in war: it claims it for kClaimYears.
void note_land_lost(SimState& state, CellId cell, std::uint32_t loser, Tick now);

}  // namespace histsim
