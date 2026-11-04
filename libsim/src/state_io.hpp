// One description of SimState's contents, used three ways: hashing, writing a
// snapshot and reading one back. Because all three walk the same function,
// whatever is saved is exactly what is hashed and restored.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

#include "histsim/hash.hpp"
#include "histsim/state.hpp"

namespace histsim::io {

// Archive interface: each method takes a reference. Writers and the hasher
// read it; the reader assigns it.

class HashArchive {
public:
    static constexpr bool kReading = false;
    void u8(std::uint8_t& v) { h_.u8(v); }
    void u32(std::uint32_t& v) { h_.u32(v); }
    void i64(std::int64_t& v) { h_.i64(v); }
    void f64(double& v) { h_.f64(v); }
    void count(std::uint64_t& n) { h_.u64(n); }
    void id64(std::uint64_t& v) { h_.u64(v); }
    [[nodiscard]] std::uint64_t digest() const { return h_.digest(); }

private:
    Hasher h_;
};

class WriteArchive {
public:
    static constexpr bool kReading = false;
    void u8(std::uint8_t& v) { buf_.push_back(v); }
    void u32(std::uint32_t& v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void i64(std::int64_t& v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double& v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }
    void count(std::uint64_t& n) { u64(n); }
    void id64(std::uint64_t& v) { u64(v); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    [[nodiscard]] std::vector<std::uint8_t>& bytes() { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

class ReadArchive {
public:
    static constexpr bool kReading = true;
    ReadArchive(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}
    void u8(std::uint8_t& v) {
        need(1);
        v = *p_++;
    }
    void u32(std::uint32_t& v) {
        need(4);
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(*p_++) << (8 * i);
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(*p_++) << (8 * i);
        return v;
    }
    void i64(std::int64_t& v) { v = static_cast<std::int64_t>(u64()); }
    void f64(double& v) {
        const std::uint64_t bits = u64();
        std::memcpy(&v, &bits, sizeof v);
    }
    void count(std::uint64_t& n) {
        n = u64();
        // A count can never exceed the bytes left; reject before allocating.
        if (n > static_cast<std::uint64_t>(end_ - p_)) {
            throw std::runtime_error("snapshot is corrupt (impossible length)");
        }
    }
    void id64(std::uint64_t& v) { v = u64(); }
    void raw(void* out, std::size_t n) {
        need(n);
        std::memcpy(out, p_, n);
        p_ += n;
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

// ---- Helpers

template <class Ar, class T, class F>
void vec(Ar& ar, std::vector<T>& v, F&& each) {
    std::uint64_t n = v.size();
    ar.count(n);
    if constexpr (Ar::kReading) {
        v.resize(n);
    }
    for (T& x : v) each(x);
}

template <class Ar>
void doubles(Ar& ar, std::vector<double>& v) {
    vec(ar, v, [&](double& x) { ar.f64(x); });
}

template <class Ar>
void str(Ar& ar, std::string& s) {
    std::uint64_t n = s.size();
    ar.count(n);
    if constexpr (Ar::kReading) s.resize(n);
    for (char& ch : s) {
        auto b = static_cast<std::uint8_t>(ch);
        ar.u8(b);
        ch = static_cast<char>(b);
    }
}

// A bool or a one-byte enum.
template <class Ar, class T>
void u8(Ar& ar, T& v) {
    auto b = static_cast<std::uint8_t>(v);
    ar.u8(b);
    v = static_cast<T>(b);
}

template <class Ar>
void flag(Ar& ar, bool& v) {
    auto b = static_cast<std::uint8_t>(v ? 1 : 0);
    ar.u8(b);
    v = b != 0;
}

// ---- The state

template <class Ar>
void serialize(Ar& ar, SimState& s) {
    ar.i64(s.tick);
    vec(ar, s.cohorts, [&](Cohort& c) {
        ar.u32(c.cell);
        ar.u32(c.culture);
        ar.u32(c.language);
        for (auto& n : c.female) ar.u32(n);
        for (auto& n : c.male) ar.u32(n);
        for (auto& t : c.traits) ar.f64(t);
        for (auto& d : c.dialect) ar.f64(d);
        ar.f64(c.farming);
        ar.f64(c.bonded);
    });
    vec(ar, s.ever_settled, [&](std::uint8_t& v) { ar.u8(v); });

    ar.f64(s.climate.global_anomaly_c);
    ar.f64(s.climate.volcanic_forcing);
    ar.f64(s.climate.solar_forcing);
    ar.f64(s.climate.imposed_forcing);
    doubles(ar, s.climate.regional_temp_anomaly);
    doubles(ar, s.climate.regional_precip_anomaly);
    vec(ar, s.climate.modifiers, [&](ClimateModifier& m) {
        ar.u32(m.center);
        ar.f64(m.radius_km);
        ar.f64(m.temp_c);
        ar.f64(m.precip_frac);
        ar.i64(m.years_left);
    });
    ar.i64(s.climate.bond.start_year);
    ar.i64(s.climate.bond.years);
    ar.f64(s.climate.bond.strength_c);
    ar.i64(s.climate.next_bond_year);
    vec(ar, s.climate.bond_history, [&](BondEvent& b) {
        ar.i64(b.start_year);
        ar.i64(b.years);
        ar.f64(b.strength_c);
    });
    ar.f64(s.climate.bond_anomaly_c);

    doubles(ar, s.env.temperature_c);
    doubles(ar, s.env.precipitation_mm);
    doubles(ar, s.env.game);
    doubles(ar, s.env.capacity);
    doubles(ar, s.env.farming);
    doubles(ar, s.env.farm_capacity);
    doubles(ar, s.env.pastoral);
    doubles(ar, s.env.food_store);
    doubles(ar, s.env.disease_mortality);

    ar.i64(s.ledger.initial_population);
    ar.i64(s.ledger.births);
    ar.i64(s.ledger.deaths);
    ar.i64(s.ledger.migrants);
    ar.i64(s.ledger.added);

    vec(ar, s.events, [&](Event& e) {
        ar.i64(e.tick);
        std::uint32_t type = static_cast<std::uint32_t>(e.type);
        ar.u32(type);
        if constexpr (Ar::kReading) e.type = static_cast<EventType>(type);
        ar.u32(e.cell);
        ar.i64(e.value);
    });
    vec(ar, s.commands, [&](Command& c) {
        ar.i64(c.tick);
        std::uint32_t type = static_cast<std::uint32_t>(c.type);
        ar.u32(type);
        if constexpr (Ar::kReading) c.type = static_cast<CommandType>(type);
        ar.u32(c.cell);
        ar.f64(c.radius_km);
        ar.f64(c.amount);
        ar.f64(c.amount2);
        ar.i64(c.years);
    });
    vec(ar, s.languages, [&](Language& l) {
        ar.u32(l.id);
        ar.u32(l.parent);
        ar.u32(l.family);
        ar.i64(l.founded);
        ar.i64(l.extinct);
        str(ar, l.name);
        vec(ar, l.words, [&](Word& w) {
            vec(ar, w.sounds, [&](PhonemeId& p) { ar.u8(p); });
            ar.id64(w.cognate);
        });
        ar.u32(l.syllables);
        ar.u8(l.word_order);
        ar.u8(l.morphology);
        ar.u8(l.tone);
        vec(ar, l.sound_changes, [&](std::uint16_t& r) {
            std::uint32_t v = r;
            ar.u32(v);
            r = static_cast<std::uint16_t>(v);
        });
    });
    vec(ar, s.cultures, [&](Culture& c) {
        ar.u32(c.id);
        ar.u32(c.parent);
        ar.u32(c.root);
        ar.u32(c.language);
        ar.i64(c.founded);
        ar.i64(c.extinct);
        str(ar, c.name);
    });
    ar.id64(s.next_cognate);
    vec(ar, s.domesticates, [&](std::uint32_t& m) { ar.u32(m); });
    doubles(ar, s.domestication);
    vec(ar, s.disease_present, [&](std::uint8_t& v) { ar.u8(v); });
    doubles(ar, s.susceptible);
    vec(ar, s.disease_deaths, [&](std::uint32_t& v) { ar.u32(v); });
    ar.i64(s.disease_deaths_total);
    vec(ar, s.disease_origins, [&](DiseaseOrigin& o) {
        ar.i64(o.tick);
        ar.u32(o.cell);
        ar.u32(o.disease);
    });
    vec(ar, s.techniques, [&](std::uint64_t& t) { ar.id64(t); });
    vec(ar, s.inventions, [&](Invention& v) {
        ar.i64(v.tick);
        ar.u32(v.cell);
        ar.u32(v.technique);
        ar.u32(v.culture);
        ar.u32(v.figure);
    });
    doubles(ar, s.urban);
    vec(ar, s.city_of, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.cities, [&](City& c) {
        ar.u32(c.id);
        ar.u32(c.cell);
        str(ar, c.name);
        ar.i64(c.founded);
        ar.i64(c.abandoned);
    });
    vec(ar, s.polity_of, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.polities, [&](Polity& p) {
        ar.u32(p.id);
        str(ar, p.name);
        ar.u32(p.capital);
        ar.i64(p.founded);
        ar.i64(p.ended);
        ar.u32(p.culture);
        ar.u32(p.language);
        ar.u32(p.parent);
        flag(ar, p.nomadic);
        ar.u32(p.overlord);
        u8(ar, p.bond);
        ar.i64(p.bond_since);
        ar.f64(p.stability);
        ar.f64(p.shock);
        for (double& x : p.pressure) ar.f64(x);
        u8(ar, p.fate);
        ar.u8(p.fate_cause);
        ar.i64(p.settling_since);
        ar.f64(p.plunder);
        ar.u32(p.religion);
        ar.i64(p.wandered);
        ar.i64(p.arrived);
        ar.u32(p.ruler);
        ar.u8(p.institutions);
        flag(ar, p.contested);
        ar.u32(p.trek_to);
        ar.u32(p.trek_record);
        ar.u32(p.founder);
        ar.f64(p.unrest);
        ar.u32(p.rebel);
    });
    doubles(ar, s.stocks);
    doubles(ar, s.prices);
    vec(ar, s.goods_available, [&](std::uint32_t& v) { ar.u32(v); });
    doubles(ar, s.food_import);
    doubles(ar, s.trade_value);
    doubles(ar, s.route_value);
    vec(ar, s.wars, [&](War& w) {
        ar.u32(w.id);
        str(ar, w.name);
        vec(ar, w.attackers, [&](std::uint32_t& v) { ar.u32(v); });
        vec(ar, w.defenders, [&](std::uint32_t& v) { ar.u32(v); });
        u8(ar, w.goal);
        ar.i64(w.started);
        ar.i64(w.ended);
        u8(ar, w.outcome);
        ar.i64(w.deaths);
        ar.u32(w.cells_taken);
        ar.u32(w.cells_lost);
    });
    vec(ar, s.alliances, [&](Alliance& a) {
        ar.u32(a.a);
        ar.u32(a.b);
        ar.i64(a.formed);
        ar.i64(a.ended);
    });
    vec(ar, s.truces, [&](Truce& t) {
        ar.u32(t.a);
        ar.u32(t.b);
        ar.i64(t.until);
    });
    vec(ar, s.war_deaths, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.strife, [&](std::uint8_t& v) { ar.u8(v); });
    ar.i64(s.war_deaths_total);
    ar.u32(s.raids);
    vec(ar, s.last_raid, [&](Tick& t) { ar.i64(t); });
    vec(ar, s.religions, [&](Religion& r) {
        ar.u32(r.id);
        ar.u32(r.parent);
        ar.u8(r.kind);
        ar.u32(r.culture_root);
        str(ar, r.name);
        ar.i64(r.founded);
        ar.i64(r.extinct);
        ar.u32(r.origin);
        ar.u32(r.polity);
        ar.u32(r.founder);
    });
    vec(ar, s.faith_of, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.rulers, [&](Ruler& r) {
        ar.u32(r.id);
        ar.u32(r.polity);
        ar.u32(r.dynasty);
        str(ar, r.name);
        ar.u32(r.number);
        ar.i64(r.born);
        ar.i64(r.reign_start);
        ar.i64(r.reign_end);
        u8(ar, r.end);
        ar.f64(r.ability);
        ar.f64(r.martial);
        ar.u32(r.figure);
    });
    vec(ar, s.dynasties, [&](Dynasty& d) {
        ar.u32(d.id);
        str(ar, d.name);
        ar.u32(d.polity);
        ar.u32(d.founder);
        ar.i64(d.founded);
        ar.i64(d.ended);
    });
    vec(ar, s.provinces, [&](Province& v) {
        ar.u32(v.id);
        ar.u32(v.polity);
        ar.u32(v.seat);
        str(ar, v.name);
        ar.u32(v.governor);
        ar.f64(v.loyalty);
        ar.f64(v.unrest);
        ar.i64(v.founded);
        ar.i64(v.ended);
        ar.u32(v.became);
    });
    vec(ar, s.province_of, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.uprisings, [&](Uprising& u) {
        ar.i64(u.tick);
        ar.u32(u.polity);
        ar.u32(u.province);
        ar.u32(u.cell);
        ar.u32(u.leader);
        u8(ar, u.outcome);
        ar.i64(u.rebels);
        ar.i64(u.dead);
        ar.u32(u.became);
    });
    vec(ar, s.figures, [&](Figure& f) {
        ar.u32(f.id);
        str(ar, f.name);
        flag(ar, f.female);
        ar.i64(f.born);
        ar.i64(f.died);
        ar.u32(f.father);
        ar.u32(f.mother);
        ar.u32(f.spouse);
        ar.u32(f.dynasty);
        ar.u32(f.polity);
        ar.u32(f.culture);
        ar.u32(f.language);
        ar.u32(f.home);
        ar.u32(f.ruler);
        {
            std::uint32_t roles = f.roles;
            ar.u32(roles);
            f.roles = static_cast<std::uint16_t>(roles);
        }
        u8(ar, f.goal);
        ar.f64(f.ability);
        ar.f64(f.martial);
        ar.f64(f.piety);
        ar.f64(f.ambition);
    });
    vec(ar, s.migrations, [&](FolkMovement& m) {
        ar.i64(m.tick);
        ar.u32(m.from);
        ar.u32(m.to);
        ar.i64(m.people);
        ar.u32(m.language);
        ar.u32(m.culture);
        u8(ar, m.cause);
        ar.u32(m.pushed_by);
        ar.u32(m.polity);
    });
    vec(ar, s.last_arrival, [&](std::uint32_t& v) { ar.u32(v); });
    vec(ar, s.last_arrival_tick, [&](Tick& t) { ar.i64(t); });
    doubles(ar, s.recent_capacity);
    doubles(ar, s.usual_capacity);
    vec(ar, s.slave_routes, [&](SlaveRoute& r) {
        ar.u32(r.from);
        ar.u32(r.to);
        ar.i64(r.people);
        ar.i64(r.first);
        ar.i64(r.last);
        ar.u32(r.language);
    });
    ar.i64(s.captives);
    ar.i64(s.sold);
    ar.i64(s.march_deaths);
    ar.i64(s.colonists);
    ar.i64(s.folk_movers);
    vec(ar, s.domestications, [&](Domestication& d) {
        ar.i64(d.tick);
        ar.u32(d.cell);
        ar.u32(d.species);
        ar.u32(d.culture);
        ar.u32(d.language);
    });
    vec(ar, s.history, [&](YearRecord& r) {
        ar.i64(r.year);
        ar.i64(r.population);
        ar.i64(r.births_total);
        ar.i64(r.deaths_total);
        ar.i64(r.migrants_total);
        ar.u32(r.inhabited_cells);
        ar.f64(r.global_anomaly_c);
        ar.f64(r.capacity);
        ar.u32(r.languages);
        ar.u32(r.language_families);
        ar.u32(r.cultures);
        ar.i64(r.farmers);
        ar.i64(r.disease_deaths_total);
        ar.u32(r.techniques);
        ar.i64(r.townspeople);
        ar.u32(r.cities);
        ar.u32(r.polities);
        ar.u32(r.states);
        ar.f64(r.largest_polity_km2);
        ar.i64(r.largest_polity_people);
        ar.u32(r.domesticated);
        ar.f64(r.trade_value);
        ar.f64(r.food_traded);
        ar.u32(r.wars);
        ar.i64(r.war_deaths_total);
        ar.u32(r.alliances);
        ar.u32(r.subjects);
        ar.u32(r.nomads);
        ar.u32(r.raids);
        ar.i64(r.colonists);
        ar.i64(r.folk_movers);
        ar.u32(r.religions);
        ar.i64(r.organized_faithful);
        ar.i64(r.enslaved);
        ar.i64(r.captives);
        ar.i64(r.sold);
    });
}

}  // namespace histsim::io
