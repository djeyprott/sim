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
//
// Subjects and overlords (stage B). Each year a subject weighs the hold its
// overlord has on it: the overlord's strength x clamp(its control at the
// subject's capital / kProjection, kMinHold, 1), x kDistracted if it is at
// war with someone else, x (0.5 + trust). A subject at least kRebelHold
// times as strong as that rebels (kRebellion a year); a tributary at
// least kWithholdRatio as strong withholds its tribute (kWithhold a year),
// both x unrest = (1 - opinion) x kForeign for another culture root and
// again for another faith, x kNewReign in the year either ruler came to
// the throne (between kMinUnrest and kMaxUnrest). Tribute withheld gives the overlord cause for a war to
// restore it for kGrievanceYears.
// An overlord already at war elsewhere does not come to the aid of an
// attacked subject: trust falls by kAbandonTrust and opinion by
// kAbandonOpinion; one that does, trust rises by kAidTrust. Trust drifts
// back toward a half by kTrustDrift a year.
// A free realm with a threatening neighbour (war.hpp kThreat) and no ally
// to be found may offer it tribute instead (kSubmit a year x (1 + opinion),
// x kKinSubmit for a related culture), if the neighbour is a state (it
// writes), free itself, could bring at least kSubmitRatio times the realm's
// strength to its capital, and has the capacity to rule more.
//
// War and peace (stage C). Each war keeps a score from the attackers' side,
// from -1 to 1: kLandScore x the land gained (cells taken less cells lost,
// over the cells the losing leader held when it began), + kBattleScore x
// (field battles won less lost) / (all battles + 2), + kCapitalScore for a
// field army at the gates of the enemy capital (less for the enemy's at its
// own). Each year the war may end by exhaustion (war.hpp kPeace), or the
// side the score is against sues for peace: kSue x |score| a year once
// |score| >= kSueScore. The terms follow the score: within kWhite of 0 a
// white peace (land taken in the war is given back both ways); otherwise
// the victors keep what they took and, if they fought for tribute and the
// score is at least kTributeScore (or the capital fell), the loser pays it; defenders who prevail take back the land they lost in the war.
// (A fallen capital still means vassalage or annexation.) An ally of a
// side's leader (not its vassal or overlord) with the score at least
// kSeparateScore against it may make a separate peace (kSeparate a year x
// (1 - opinion of the leader / 2)): a betrayal, costing kBetrayTrust trust
// and kBetrayOpinion opinion with the leader and ending their alliance.
// Allies who see a war through gain kLoyalTrust with the leader.
//
// Infamy (the balance of power): a realm that takes land in war gains
// kInfamyLand x the cells taken / max(kInfamyFloor, its cells), and
// kInfamyRealm for each realm it annexes or makes its vassal; it fades by
// kInfamyDecay a year. Others think the worse of it (- kInfamyOpinion x
// infamy, at most 1, on every relation's target), and a neighbour with
// infamy at least kInfamous counts as a threat to ally against if it is
// stronger at all (not only kThreat times stronger), with the chance of
// finding an ally x (1 + infamy).
//
// Restoration: for kRestoreYears after a realm falls, the realms that rose
// from it (its successors, and provinces that broke away from it) are rival
// heirs: each has cause for war on the others (to restore the realm), they
// think kRivalHeirs the worse of each other, and land one takes from another
// costs only kRestoreInfamy of the usual infamy.

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
    // Subjects.
    static constexpr double kMinHold = 0.5;
    static constexpr double kDistracted = 0.6;
    static constexpr double kWithholdRatio = 0.5;
    static constexpr double kRebelHold = 1.0;
    static constexpr double kWithhold = 0.04;
    static constexpr double kForeign = 1.5;
    static constexpr double kMinUnrest = 0.5, kMaxUnrest = 2.0;
    static constexpr double kNewReign = 2.0;
    static constexpr double kWithheldShock = 0.2;
    static constexpr double kGrievanceYears = 30.0;
    static constexpr double kAbandonTrust = 0.25;
    static constexpr double kAbandonOpinion = 0.2;
    static constexpr double kAidTrust = 0.1;
    static constexpr double kTrustDrift = 0.02;
    static constexpr double kSubmit = 0.02;
    static constexpr double kKinSubmit = 1.5;
    static constexpr double kSubmitRatio = 3.0;
    // War score and peace.
    static constexpr double kLandScore = 2.0;
    static constexpr double kBattleScore = 0.4;
    static constexpr double kCapitalScore = 0.3;
    static constexpr double kWhite = 0.1;
    static constexpr double kTributeScore = 0.4;  // a war for tribute must be won by this much to impose it
    static constexpr double kSueScore = 0.5;
    static constexpr double kSue = 0.3;
    static constexpr double kSeparateScore = 0.5;
    static constexpr double kSeparate = 0.08;
    static constexpr double kBetrayTrust = 0.3;
    static constexpr double kBetrayOpinion = 0.3;
    static constexpr double kLoyalTrust = 0.1;
    // Infamy.
    static constexpr double kInfamyLand = 1.0;
    static constexpr double kInfamyFloor = 5.0;
    static constexpr double kInfamyRealm = 0.3;
    static constexpr double kInfamyDecay = 0.05;
    static constexpr double kInfamyOpinion = 0.5;
    static constexpr double kInfamous = 0.3;
    // Restoration.
    static constexpr double kRestoreYears = 200.0;
    static constexpr double kRestoreInfamy = 0.25;
    static constexpr double kRivalHeirs = 0.2;
};

// The relation between two realms, if they have one.
[[nodiscard]] const Relation* find_relation(const SimState& state, std::uint32_t a, std::uint32_t b);
[[nodiscard]] Relation& relation_of(SimState& state, std::uint32_t a, std::uint32_t b, Tick now);
// Their opinion of each other (0 if they have no relation).
[[nodiscard]] double opinion_between(const SimState& state, std::uint32_t a, std::uint32_t b);
// Their trust in each other (a half if they have no relation).
[[nodiscard]] double trust_between(const SimState& state, std::uint32_t a, std::uint32_t b);
// Cells of `of` that `by` claims.
[[nodiscard]] std::uint32_t claims_on(const SimState& state, std::uint32_t by, std::uint32_t of);
// Whether `a` has cause for war on `b`: a claim, a holy war, tribute `b`
// withheld from it, or the restoration of a fallen realm both rose from.
// Whether `a` and `b` rose from the same fallen realm within kRestoreYears.
[[nodiscard]] bool restoration(const SimState& state, std::uint32_t a, std::uint32_t b);
// Whether `b` withheld tribute owed to `a` within kGrievanceYears.
[[nodiscard]] bool tribute_withheld(const SimState& state, std::uint32_t a, std::uint32_t b);
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
// Land lost since `since` goes back to those of `to` who lost it (and claim
// it), from whoever of `from` holds it now; whether any did.
bool return_war_gains(SimState& state, Tick since, const std::vector<std::uint32_t>& to,
                      const std::vector<std::uint32_t>& from);
void note_land_lost(SimState& state, CellId cell, std::uint32_t loser, Tick now);

}  // namespace histsim
