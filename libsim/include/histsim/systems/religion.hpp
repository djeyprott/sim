// Religion (M3).
//
// Every land has a faith. Each people starts with its own folk religion (the
// beliefs of its culture), which belongs to that people: it spreads only with
// it, and when newcomers' culture takes over a land, their folk beliefs
// follow. Organized religions arise from these (data/religions.json): temple
// cults at the capitals of polities with cities and a love of ritual, and,
// rarely, universal faiths in great cities with writing.
//
// Conversion: each year a land's faith is weighed against the faiths around
// it. Its own faith pulls with its people x appeal x kAttachment; each other
// faith with the people of neighbouring lands that hold it x appeal x
// (kNeighbourInfluence + missionary zeal), with the value carried along trade
// routes to lands that hold it x kTradeInfluence x appeal x zeal, and, if it
// is the rulers' faith, with kRulerPull x their control x the land's people x
// appeal. The strongest rival wins over the land with chance kConversion x
// (its pull - own pull) / (its pull + own pull). Folk religions (ethnic)
// convert no one. Rulers hold the faith of their capital, so when the
// capital converts, the state does, and its provinces follow.
//
// A faith that has spread far from where it began splits: a polity ruling
// many of its lands far from its origin may form its own sect (a schism).
// Followers' culture is drawn slowly toward the doctrine of their faith, and
// a polity whose people share its rulers' organized faith is steadier (see
// PolitySystem); one divided by faith is shakier.
#pragma once

#include <string>

#include "histsim/scheduler.hpp"

namespace histsim {

class ReligionSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "religion"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::CultureTech; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kAttachment = 2.0;
    static constexpr double kNeighbourInfluence = 0.05;
    static constexpr double kTradeInfluence = 5.0;
    static constexpr double kRulerPull = 3.0;
    static constexpr double kConversion = 0.03;
    // How open foragers are to preachers and traders' faiths, against
    // farmers and townspeople (1); rulers' pull is not scaled.
    static constexpr double kForagerOpenness = 0.2;
    // A land whose folk faith is not its people's takes theirs up (a year).
    static constexpr double kFolkFollow = 0.02;
    // Founding is checked every kFoundingYears, schisms every kSchismYears:
    // a polity holding at least kSchismCells lands of a faith, with its
    // capital over kSchismKm from where the faith began, forms its own sect
    // with chance kSchism per check.
    static constexpr std::int64_t kFoundingYears = 5;
    static constexpr std::int64_t kSchismYears = 20;
    static constexpr std::size_t kSchismCells = 4;
    static constexpr double kSchismKm = 1000.0;
    static constexpr double kSchism = 0.05;
    // Share of the gap to their faith's doctrine that followers' culture
    // traits close each year.
    static constexpr double kDoctrinePull = 0.002;
};

// The founding ancestor of a religion (itself if it has no parent).
std::uint32_t religion_family(const SimState& state, std::uint32_t religion);

// "Folk religion", "Temple cult", ... for a religion.
const std::string& religion_kind_name(const World& world, const SimState& state, std::uint32_t religion);

// Whether a religion is organized (not a people's own folk beliefs).
bool religion_organized(const World& world, const SimState& state, std::uint32_t religion);

}  // namespace histsim
