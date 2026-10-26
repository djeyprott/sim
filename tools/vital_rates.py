#!/usr/bin/env python3
"""Derive the forager vital-rate tables used by the demography system.

The simulation must not call exp() at run time (libm results differ between
platforms), so this script computes the tables offline and prints them as C++
constants for libsim/src/systems/vital_rates.inc. Re-run it if the sources or
parameters change, and paste the output into that file.

Sources
- Mortality: Siler competing-hazards model, "average hunter-gatherer"
  parameters from Gurven & Kaplan (2007), Longevity among hunter-gatherers,
  Population and Development Review 33(2), Table 2:
  a1=0.422 b1=1.131 a2=0.013 a3=1.47e-4 b3=0.086. The paper reports e15=37.7.
- Fertility: total fertility 5.4 births per woman, the mean of 47 forager
  populations in Marlowe (2005), as summarized in Borgerhoff Mulder et al.
  (2010) Current Anthropology, Table 1. Age pattern: Coale & Trussell natural
  fertility schedule n(a) for ages 20-49, with ages 15-19 at half the 20-24
  rate (adolescent subfecundity, later marriage).
"""
import math

A1, B1, A2, A3, B3 = 0.422, 1.131, 0.013, 1.47e-4, 0.086
BIN = 5
NBINS = 21  # 0-4, 5-9, ..., 95-99, 100+
TFR = 5.4
FEMALE_SHARE_OF_BIRTHS = 100 / 205  # sex ratio at birth 105 boys : 100 girls

NATURAL = {20: 0.460, 25: 0.431, 30: 0.396, 35: 0.321, 40: 0.167, 45: 0.024}


def hazard(x):
    return A1 * math.exp(-B1 * x) + A2 + A3 * math.exp(B3 * x)


def survival(x):
    # S(x) = exp(-integral of hazard from 0 to x), closed form
    integral = (A1 / B1) * (1 - math.exp(-B1 * x)) + A2 * x + (A3 / B3) * (math.exp(B3 * x) - 1)
    return math.exp(-integral)


def life_expectancy(at):
    step = 0.01
    total, x = 0.0, at
    s0 = survival(at)
    while x < 130:
        total += 0.5 * (survival(x) + survival(x + step)) * step
        x += step
    return total / s0


def annual_death_prob_by_bin():
    """Average annual probability of dying for a person in each 5-year bin,
    weighting each single year of age by the survivors in it."""
    out = []
    for b in range(NBINS):
        lo = b * BIN
        hi = lo + BIN if b < NBINS - 1 else 110
        num = den = 0.0
        for age in range(lo, hi):
            w = survival(age)
            q = 1 - survival(age + 1) / survival(age)
            num += w * q
            den += w
        out.append(num / den)
    return out


def asfr_by_bin():
    base = {15: NATURAL[20] / 2, **NATURAL}
    total = BIN * sum(base.values())
    scale = TFR / total
    return [base.get(b * BIN, 0.0) * scale for b in range(NBINS)]


def stable_age_shares(r=0.0):
    """Share of a stationary (r=0) population in each 5-year bin, from the
    life table: proportional to person-years lived in the bin."""
    shares = []
    for b in range(NBINS):
        lo = b * BIN
        hi = lo + BIN if b < NBINS - 1 else 120
        shares.append(sum(math.exp(-r * (a + 0.5)) * survival(a + 0.5) for a in range(lo, hi)))
    total = sum(shares)
    return [v / total for v in shares]


def growth_rate(fert_mult, mort_mult):
    """Intrinsic growth rate r (Lotka) with fertility and mortality scaled."""
    # survival with hazards scaled: S_m(x) = S(x)^m
    asfr = asfr_by_bin()
    def net(r):
        total = 0.0
        for age in range(0, 60):
            f = asfr[age // BIN] * fert_mult * FEMALE_SHARE_OF_BIRTHS
            total += math.exp(-r * (age + 0.5)) * survival(age + 0.5) ** mort_mult * f
        return total
    lo, hi = -0.1, 0.1
    for _ in range(80):
        mid = (lo + hi) / 2
        if net(mid) > 1:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


if __name__ == "__main__":
    print(f"// e0 = {life_expectancy(0):.1f}, e15 = {life_expectancy(15):.1f} "
          f"(Gurven & Kaplan report e15 = 37.7)")
    print(f"// Intrinsic growth at TFR {TFR}: {100 * growth_rate(1, 1):.2f}% per year")
    rep = 1.0
    lo, hi = 0.2, 2.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if growth_rate(mid, 1) > 0:
            hi = mid
        else:
            lo = mid
    rep = (lo + hi) / 2
    print(f"// Replacement fertility multiplier: {rep:.4f} (TFR {TFR * rep:.2f})")
    for mult in (1.1, 1.2, 1.3):
        print(f"// fertility x{mult}: growth {100 * growth_rate(mult, 1):.2f}% per year")
    print()
    print("// Annual probability of death by 5-year age bin (0-4 ... 95-99, 100+).")
    print("inline constexpr double kForagerDeathProb[21] = {")
    q = annual_death_prob_by_bin()
    print("    " + ",\n    ".join(", ".join(f"{v:.6f}" for v in q[i:i + 7]) for i in range(0, 21, 7)) + "};")
    print()
    print(f"// Births per woman per year by 5-year age bin, summing to TFR {TFR} over 5-year bins.")
    print("inline constexpr double kForagerFertility[21] = {")
    f = asfr_by_bin()
    print("    " + ",\n    ".join(", ".join(f"{v:.6f}" for v in f[i:i + 7]) for i in range(0, 21, 7)) + "};")
    print()
    print("// Share of a stationary population in each 5-year bin (both sexes).")
    print("inline constexpr double kStableAgeShare[21] = {")
    a = stable_age_shares()
    print("    " + ",\n    ".join(", ".join(f"{v:.6f}" for v in a[i:i + 7]) for i in range(0, 21, 7)) + "};")
    print()
    print(f"inline constexpr double kFemaleShareOfBirths = {FEMALE_SHARE_OF_BIRTHS:.6f};")
    print(f"inline constexpr double kReplacementFertilityMultiplier = {rep:.6f};")
