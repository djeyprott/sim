#include "histsim/content.hpp"

#include <stdexcept>

#include "histsim/hash.hpp"
#include "histsim/json.hpp"

namespace histsim {

// Generated at configure time from data/species.json (see libsim/CMakeLists.txt).
extern const unsigned char kSpeciesJson[];
extern const std::size_t kSpeciesJsonSize;

namespace {

Trapezoid trapezoid(const json::Value& v) {
    const auto& a = v.as_array();
    if (a.size() != 4) {
        throw std::runtime_error("content: " + v.path() + " should be [min, best_from, best_to, max]");
    }
    Trapezoid t{a[0].as_number(), a[1].as_number(), a[2].as_number(), a[3].as_number()};
    if (!(t.lo <= t.best_lo && t.best_lo <= t.best_hi && t.best_hi <= t.hi) || t.lo == t.hi) {
        throw std::runtime_error("content: " + v.path() + " must be in increasing order with min < max");
    }
    // A zero-width ramp would divide by zero: nudge it to a step.
    if (t.best_lo == t.lo) t.lo -= 1e-9;
    if (t.best_hi == t.hi) t.hi += 1e-9;
    return t;
}

double unit(const json::Value& obj, std::string_view key, double fallback) {
    const double v = obj.number_or(key, fallback);
    if (v < 0.0 || v > 1.0) {
        throw std::runtime_error("content: " + obj.get(key).path() + " must be between 0 and 1");
    }
    return v;
}

}  // namespace

Content load_content(std::string_view species_json, std::string_view source_name) {
    const json::Value doc = json::parse(species_json, source_name);
    if (doc.at("version").as_int() != 1) {
        throw std::runtime_error("content: unsupported species pack version");
    }
    Content c;
    for (const json::Value& s : doc.at("species").as_array()) {
        SpeciesDef d;
        d.id = s.at("id").as_string();
        d.name = s.at("name").as_string();
        const std::string& kind = s.at("kind").as_string();
        if (kind == "plant") d.kind = SpeciesKind::Plant;
        else if (kind == "animal") d.kind = SpeciesKind::Animal;
        else throw std::runtime_error("content: " + s.at("kind").path() + " must be \"plant\" or \"animal\"");
        d.food = s.string_or("food", "");
        d.coldest_month_c = trapezoid(s.at("coldest_month_c"));
        d.warmest_month_c = trapezoid(s.at("warmest_month_c"));
        d.annual_precip_mm = trapezoid(s.at("annual_precip_mm"));
        d.rain_season = trapezoid(s.at("rain_season"));
        d.elevation_m = trapezoid(s.at("elevation_m"));
        d.river_affinity = unit(s, "river_affinity", 0.0);
        d.native_regions = static_cast<int>(s.at("native_regions").as_int());
        if (d.native_regions < 1 || d.native_regions > 8) {
            throw std::runtime_error("content: " + s.at("native_regions").path() + " must be 1-8");
        }
        d.range_km = s.at("range_km").as_number();
        if (d.range_km <= 0.0 || d.range_km > 20000.0) {
            throw std::runtime_error("content: " + s.at("range_km").path() + " must be 0-20000");
        }
        d.domesticability = unit(s, "domesticability", 0.5);
        d.food_value = s.number_or("food_value", 0.5);
        for (const SpeciesDef& other : c.species) {
            if (other.id == d.id) throw std::runtime_error("content: duplicate species id \"" + d.id + "\"");
        }
        c.species.push_back(std::move(d));
    }
    if (c.species.size() > kMaxSpecies) {
        throw std::runtime_error("content: at most " + std::to_string(kMaxSpecies) + " species are supported");
    }
    Hasher h;
    h.bytes(species_json.data(), species_json.size());
    c.hash = h.digest();
    return c;
}

const Content& default_content() {
    static const Content content = load_content(
        std::string_view(reinterpret_cast<const char*>(kSpeciesJson), kSpeciesJsonSize), "data/species.json");
    return content;
}

}  // namespace histsim
