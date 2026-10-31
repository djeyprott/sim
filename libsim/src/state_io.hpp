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
        ar.u32(r.domesticated);
    });
}

}  // namespace histsim::io
