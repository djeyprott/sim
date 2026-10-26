#include "histsim/systems/climate.hpp"

#include <algorithm>
#include <cmath>

#include "histsim/interventions.hpp"
#include "histsim/portable_math.hpp"

namespace histsim {

namespace {

double normal(RngStream& rng) {
    const double a = rng.uniform(), b = rng.uniform(), c = rng.uniform(), d = rng.uniform();
    return pmath::normal_from_uniforms(a, b, c, d);
}

// Milankovitch periods (years) and global-mean forcing amplitudes (W/m^2).
// The global annual-mean effect of orbital change is small; ice-sheet and
// carbon feedbacks that turn it into ice ages are not modeled yet.
struct OrbitalTerm {
    double period;
    double amplitude;
};
constexpr OrbitalTerm kOrbital[] = {
    {23000.0, 0.30},   // precession
    {19000.0, 0.20},   // precession
    {41000.0, 0.40},   // obliquity
    {100000.0, 0.20},  // eccentricity
};

}  // namespace

double orbital_forcing(std::uint64_t seed, std::int64_t year) {
    double f = 0.0;
    for (std::uint32_t i = 0; i < std::size(kOrbital); ++i) {
        RngStream phase_rng(seed, 0, Stream::Climate, 0x0B000000u + i);
        const double phase = 2.0 * kPi * phase_rng.uniform();
        f += kOrbital[i].amplitude * pmath::sin(2.0 * kPi * static_cast<double>(year) / kOrbital[i].period + phase);
    }
    return f;
}

void ClimateSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    ClimateState& cl = s.climate;
    const std::int64_t year = ctx.tick / kTicksPerYear;

    // ---- Global energy balance
    // volcanic_forcing holds aerosols still to act: this year's eruption (or
    // one forced by an intervention since the last step) acts at full
    // strength now, and what remains decays before next year.
    RngStream rng = ctx.rng(Stream::Climate, 0);
    if (rng.uniform() < kEruptionChance) {
        const double hit = -(1.0 + 3.0 * rng.uniform());  // -1 to -4 W/m^2
        cl.volcanic_forcing += hit;
        s.events.push_back({ctx.tick, EventType::Eruption, kInvalidCell, static_cast<std::int64_t>(hit * 1000.0)});
    }
    cl.solar_forcing = 0.95 * cl.solar_forcing + 0.047 * normal(rng);  // sd ~0.15 W/m^2
    const double forcing =
        orbital_forcing(w.params.seed, year) + cl.volcanic_forcing + cl.solar_forcing + cl.imposed_forcing;
    cl.global_anomaly_c += (forcing - kFeedback * cl.global_anomaly_c) / kHeatCapacity + kInternalNoiseC * normal(rng);
    cl.volcanic_forcing *= kVolcanicDecay;

    // ---- Regional anomalies (AR(1) on the coarse grid)
    const double t_innov = kRegionalTempSd * std::sqrt(1.0 - kRegionalTempPersistence * kRegionalTempPersistence);
    const double p_innov =
        kRegionalPrecipSd * std::sqrt(1.0 - kRegionalPrecipPersistence * kRegionalPrecipPersistence);
    for (std::size_t k = 0; k < cl.regional_temp_anomaly.size(); ++k) {
        RngStream r = ctx.rng(Stream::Climate, static_cast<std::uint32_t>(k + 1));
        cl.regional_temp_anomaly[k] = kRegionalTempPersistence * cl.regional_temp_anomaly[k] + t_innov * normal(r);
        cl.regional_precip_anomaly[k] =
            kRegionalPrecipPersistence * cl.regional_precip_anomaly[k] + p_innov * normal(r);
    }

    // ---- This year's conditions per cell
    for (CellId c = 0; c < w.cell_count(); ++c) {
        double ta = 0.0, pa = 0.0;
        for (int i = 0; i < 3; ++i) {
            const CellId k = w.climate_cells[c][i];
            ta += w.climate_weights[c][i] * cl.regional_temp_anomaly[k];
            pa += w.climate_weights[c][i] * cl.regional_precip_anomaly[k];
        }
        const double z = w.grid.center(c).z;
        s.env.temperature_c[c] =
            w.temperature_c[c] + cl.global_anomaly_c * (1.0 + kPolarAmplification * z * z) + ta;
        s.env.precipitation_mm[c] =
            w.precipitation_mm[c] * std::max(0.05, (1.0 + kPrecipPerDegree * cl.global_anomaly_c) * (1.0 + pa));
    }

    // ---- Regional events imposed by interventions
    for (ClimateModifier& m : cl.modifiers) {
        for (const WeightedCell& wc : cells_within(w, m.center, m.radius_km, /*taper=*/true)) {
            s.env.temperature_c[wc.cell] += wc.weight * m.temp_c;
            s.env.precipitation_mm[wc.cell] *= std::max(0.0, 1.0 + wc.weight * m.precip_frac);
        }
        --m.years_left;
    }
    std::erase_if(cl.modifiers, [](const ClimateModifier& m) { return m.years_left <= 0; });
}

double environment_capacity(const World& w, const SimState& s, CellId c) {
    if (!w.is_land[c]) return 0.0;
    const double climate_capacity =
        forager_density(s.env.temperature_c[c], s.env.precipitation_mm[c]) * w.grid.area_km2(c);
    constexpr double kShare = EnvironmentSystem::kGameShareOfCapacity;
    return climate_capacity * ((1.0 - kShare) + kShare * s.env.game[c]);
}

void EnvironmentSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (!w.is_land[c]) {
            s.env.capacity[c] = 0.0;
            continue;
        }
        const double climate_capacity =
            forager_density(s.env.temperature_c[c], s.env.precipitation_mm[c]) * w.grid.area_km2(c);
        const double crowding =
            climate_capacity > 0.0 ? static_cast<double>(s.cell_population[c]) / climate_capacity : 0.0;
        double& g = s.env.game[c];
        g += kGameRegrowth * g * (1.0 - g) + kGameInflow * (1.0 - g) -
             kHuntingPressure * std::max(0.0, crowding - kSustainableCrowding) * g;
        g = std::clamp(g, 0.02, 1.0);
        s.env.capacity[c] = environment_capacity(w, s, c);
    }
}

}  // namespace histsim
