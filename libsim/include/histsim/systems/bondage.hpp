// Forced labour and the slave trade (M3).
//
// Each group of people (cohort) holds a share in bondage (Cohort::bonded).
// Captives keep their own language and culture: carried far from home they
// change the mix of peoples where they end up, and the frontiers they were
// taken from lose people.
//
// Captives: when land is taken in war, kWarCaptives of its people are
// carried off to the conqueror's capital; nomad raids carry off kRaidCaptives
// to the raiders' capital. Fewer are taken from those of the captors' own
// cultural lineage (x kKinCaptives), and few from fellow believers of an
// organized faith (x kFaithCaptives): a shared universal faith protects its
// own.
//
// Demand: towns, mines and the estates of hierarchical farmers want bound
// labour. A cell's wanted share of its people in bondage is kTownDemand x
// its town share + kMineDemand where it has metal or salt to work +
// kEstateDemand x farming x hierarchy, all x (1.5 - sharing), at most
// kMaxDemand.
//
// Trade: each year a cell with more people in bondage than it wants sells
// kSale of the surplus to the place that wants them most within kTradeKm
// (want / (1 + distance / kTradeDistanceKm)), among places on the market
// network (with trade). Routes with their totals are kept for the map.
//
// Freedom and its lack: each year kManumission of the enslaved go free, and
// kManumissionLocal more where they have taken up the main culture of the
// land. The enslaved die younger (mortality x (1 + kBondMortality x share))
// and have fewer children (x (1 - kBondFertility x share)). A polity whose
// people are many in bondage, of other peoples, is more divided (see
// PolitySystem: division).
#pragma once

#include <cstdint>

#include "histsim/scheduler.hpp"

namespace histsim {

inline constexpr double kBondMortality = 0.3;
inline constexpr double kBondFertility = 0.4;

class BondageSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "bondage"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Polities; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kWarCaptives = 0.15;
    static constexpr double kRaidCaptives = 0.05;
    static constexpr double kKinCaptives = 0.3;
    static constexpr double kFaithCaptives = 0.1;

    static constexpr double kTownDemand = 0.4;
    static constexpr double kMineDemand = 0.05;
    static constexpr double kEstateDemand = 0.08;
    static constexpr double kMaxDemand = 0.35;

    static constexpr double kSale = 0.5;
    static constexpr double kTradeKm = 1500.0;
    static constexpr double kTradeDistanceKm = 400.0;
    static constexpr std::int64_t kMinSale = 50;  // smaller lots are not traded

    static constexpr double kManumission = 0.005;
    static constexpr double kManumissionLocal = 0.01;

    // Routes are drawn on the map while used within this many years.
    static constexpr std::int64_t kRouteMemoryYears = 50;
};

// How many captives to take from a cell's people for a captor polity (0 if
// the captor's faith or lineage protects them): `share` x the protections.
double captive_share(const World& world, const SimState& state, std::uint32_t captor, CellId from, double share);

// Carry `share` of a cell's people off into bondage at `to` (appended to
// `out`, as the enslaved), in proportion at every age; returns how many.
std::int64_t take_captives(SimState& state, CellId from, CellId to, double share, RngStream& rng,
                           std::vector<Cohort>& out);

// People in bondage in a cell.
std::int64_t bonded_people(const SimState& state, CellId cell);

// The share of a cell's people its economy wants in bondage.
double bondage_demand(const World& world, const SimState& state, CellId cell);

}  // namespace histsim
