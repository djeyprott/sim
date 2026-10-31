#include "histsim_world.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>

using namespace godot;

namespace {

String to_godot(const std::string& s) { return String::utf8(s.c_str()); }
std::string to_std(const String& s) { return std::string(s.utf8().get_data()); }

Color to_color(const histsim::view::Rgba& c) {
    return Color(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Array legend_entries(const std::vector<histsim::view::LegendEntry>& entries) {
    Array out;
    for (const auto& e : entries) {
        Dictionary d;
        d["label"] = to_godot(e.label);
        d["color"] = to_color(e.color);
        out.push_back(d);
    }
    return out;
}

}  // namespace

void HistSimWorld::fail(const std::string& message) {
    last_error_ = message;
    UtilityFunctions::push_error(to_godot("histsim: " + message));
}

String HistSimWorld::get_last_error() const { return to_godot(last_error_); }

bool HistSimWorld::create_world(int64_t seed, int64_t level, double ocean_fraction) {
    if (level < 2 || level > 8) {
        fail("grid level must be between 2 and 8");
        return false;
    }
    try {
        histsim::WorldParams p;
        p.seed = static_cast<std::uint64_t>(seed);
        p.grid_level = static_cast<int>(level);
        p.ocean_fraction = ocean_fraction;
        sim_.emplace(histsim::Simulation::create(p));
        raster_.reset();
        last_error_.clear();
        return true;
    } catch (const std::exception& e) {
        fail(e.what());
        return false;
    }
}

bool HistSimWorld::load_snapshot(const String& path) {
    try {
        sim_.emplace(histsim::Simulation::load(to_std(path)));
        raster_.reset();
        last_error_.clear();
        return true;
    } catch (const std::exception& e) {
        fail(e.what());
        return false;
    }
}

bool HistSimWorld::save_snapshot(const String& path) {
    if (!sim_) {
        fail("no world to save");
        return false;
    }
    try {
        sim_->save(to_std(path));
        return true;
    } catch (const std::exception& e) {
        fail(e.what());
        return false;
    }
}

void HistSimWorld::step_years(int64_t years) {
    if (sim_ && years > 0) {
        sim_->step_years(years);
    }
}

int64_t HistSimWorld::step_for_budget(int64_t max_years, double budget_msec) {
    if (!sim_ || max_years <= 0) {
        return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    int64_t done = 0;
    while (done < max_years) {
        sim_->step_years(1);
        ++done;
        const double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= budget_msec) {
            break;
        }
    }
    return done;
}

int64_t HistSimWorld::get_year() const { return sim_ ? sim_->year() : 0; }
int64_t HistSimWorld::get_population() const { return sim_ ? sim_->state().total_population() : 0; }
int64_t HistSimWorld::get_inhabited_cells() const {
    return sim_ ? static_cast<int64_t>(sim_->summary().inhabited_cells) : 0;
}
int64_t HistSimWorld::get_cell_count() const { return sim_ ? static_cast<int64_t>(sim_->world().cell_count()) : 0; }
int64_t HistSimWorld::get_seed() const { return sim_ ? static_cast<int64_t>(sim_->world().params.seed) : 0; }
int64_t HistSimWorld::get_level() const { return sim_ ? sim_->world().params.grid_level : 0; }

double HistSimWorld::get_forager_capacity() const {
    if (!sim_) return 0.0;
    double total = 0.0;
    for (double k : sim_->world().forager_capacity) total += k;
    return total;
}

double HistSimWorld::get_capacity() const {
    if (!sim_) return 0.0;
    double total = 0.0;
    for (double k : sim_->state().env.capacity) total += k;
    return total;
}

double HistSimWorld::get_global_anomaly() const { return sim_ ? sim_->state().climate.global_anomaly_c : 0.0; }

Dictionary HistSimWorld::get_people_counts() const {
    Dictionary d;
    const histsim::PeopleCounts p = sim_ ? histsim::count_peoples(sim_->state()) : histsim::PeopleCounts{};
    d["languages"] = static_cast<int64_t>(p.languages);
    d["families"] = static_cast<int64_t>(p.families);
    d["cultures"] = static_cast<int64_t>(p.cultures);
    return d;
}

Dictionary HistSimWorld::get_history(int64_t max_points) const {
    Dictionary d;
    PackedInt64Array years, population;
    PackedFloat64Array capacity, anomaly, farmers;
    if (sim_ && max_points > 1) {
        const auto& h = sim_->state().history;
        const std::size_t n = h.size();
        const std::size_t step = std::max<std::size_t>(1, (n + static_cast<std::size_t>(max_points) - 1) /
                                                             static_cast<std::size_t>(max_points));
        for (std::size_t i = 0; i < n; i += step) {
            years.push_back(h[i].year);
            population.push_back(h[i].population);
            capacity.push_back(h[i].capacity);
            anomaly.push_back(h[i].global_anomaly_c);
            farmers.push_back(static_cast<double>(h[i].farmers));
        }
        if (n > 0 && (n - 1) % step != 0) {  // always include the latest year
            years.push_back(h[n - 1].year);
            population.push_back(h[n - 1].population);
            capacity.push_back(h[n - 1].capacity);
            anomaly.push_back(h[n - 1].global_anomaly_c);
            farmers.push_back(static_cast<double>(h[n - 1].farmers));
        }
    }
    d["year"] = years;
    d["population"] = population;
    d["capacity"] = capacity;
    d["anomaly"] = anomaly;
    d["farmers"] = farmers;
    return d;
}

PackedInt64Array HistSimWorld::get_bond_spans() const {
    PackedInt64Array out;
    if (!sim_) return out;
    for (const histsim::BondEvent& b : sim_->state().climate.bond_history) {
        out.push_back(b.start_year);
        out.push_back(b.start_year + b.years);
    }
    return out;
}

String HistSimWorld::get_state_hash() const {
    if (!sim_) return String();
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(sim_->state_hash()));
    return String(buf);
}

Array HistSimWorld::get_modes() const {
    Array out;
    for (const auto& m : histsim::view::map_modes()) {
        Dictionary d;
        d["id"] = to_godot(m.id);
        d["name"] = to_godot(m.name);
        d["description"] = to_godot(m.description);
        out.push_back(d);
    }
    return out;
}

Dictionary HistSimWorld::get_legend(const String& mode) const {
    Dictionary d;
    try {
        const histsim::view::Legend l = sim_ ? histsim::view::legend(*sim_, to_std(mode)) : histsim::view::legend(to_std(mode));
        d["title"] = to_godot(l.title);
        d["ramp"] = legend_entries(l.ramp);
        d["swatches"] = legend_entries(l.swatches);
    } catch (const std::exception& e) {
        UtilityFunctions::push_error(to_godot(std::string("histsim: ") + e.what()));
    }
    return d;
}

void HistSimWorld::set_map_size(int64_t width, int64_t height) {
    map_width_ = static_cast<int>(width);
    map_height_ = static_cast<int>(height);
    raster_.reset();
}

void HistSimWorld::ensure_raster() {
    if (!raster_ && sim_) {
        raster_.emplace(histsim::view::MapRaster::build(sim_->world().grid, map_width_, map_height_));
    }
}

Ref<Image> HistSimWorld::render_map(const String& mode, bool coastlines, bool rivers) {
    if (!sim_) {
        fail("no world to render");
        return Ref<Image>();
    }
    try {
        ensure_raster();
        histsim::view::color_cells(*sim_, to_std(mode), colors_);
        histsim::view::RenderOptions options;
        options.coastlines = coastlines;
        options.rivers = rivers;
        if (highlight_on_ && highlight_.size() == sim_->world().cell_count()) {
            options.highlight = &highlight_;
        }
        histsim::view::render(*raster_, sim_->world(), colors_, rgba_, options);
        PackedByteArray bytes;
        bytes.resize(static_cast<int64_t>(rgba_.size()));
        std::memcpy(bytes.ptrw(), rgba_.data(), rgba_.size());
        return Image::create_from_data(map_width_, map_height_, false, Image::FORMAT_RGBA8, bytes);
    } catch (const std::exception& e) {
        fail(e.what());
        return Ref<Image>();
    }
}

int64_t HistSimWorld::cell_at(int64_t x, int64_t y) const {
    if (!raster_) return -1;
    const histsim::CellId c = raster_->cell_at(static_cast<int>(x), static_cast<int>(y));
    return c == histsim::kInvalidCell ? -1 : static_cast<int64_t>(c);
}

Array HistSimWorld::describe_cell(int64_t cell) const {
    Array out;
    if (!sim_ || cell < 0) return out;
    for (const auto& f : histsim::view::describe_cell(*sim_, static_cast<histsim::CellId>(cell))) {
        PackedStringArray pair;
        pair.push_back(to_godot(f.label));
        pair.push_back(to_godot(f.value));
        out.push_back(pair);
    }
    return out;
}

PackedStringArray HistSimWorld::get_native_species(int64_t cell) const {
    PackedStringArray out;
    if (!sim_ || cell < 0) return out;
    for (const std::string& name : histsim::view::native_species(*sim_, static_cast<histsim::CellId>(cell))) {
        out.push_back(to_godot(name));
    }
    return out;
}

Dictionary HistSimWorld::get_cell_pyramid(int64_t cell) const {
    Dictionary d;
    PackedInt64Array female, male;
    if (sim_ && cell >= 0) {
        const histsim::view::Pyramid p = histsim::view::cell_pyramid(*sim_, static_cast<histsim::CellId>(cell));
        for (int b = 0; b < histsim::kAgeBins; ++b) {
            female.push_back(p.female[b]);
            male.push_back(p.male[b]);
        }
    }
    d["female"] = female;
    d["male"] = male;
    return d;
}

Dictionary HistSimWorld::apply_intervention(const String& type, int64_t cell, double radius_km, double amount,
                                            double amount2, int64_t years) {
    Dictionary d;
    d["ok"] = false;
    d["people"] = 0;
    d["cells"] = 0;
    if (!sim_) {
        d["message"] = "No world.";
        return d;
    }
    using histsim::CommandType;
    const std::string t = to_std(type);
    histsim::Command c;
    if (t == "eruption") c.type = CommandType::Eruption;
    else if (t == "forcing") c.type = CommandType::ClimateForcing;
    else if (t == "climate") c.type = CommandType::RegionalClimate;
    else if (t == "plague") c.type = CommandType::Plague;
    else if (t == "wipeout") c.type = CommandType::WipeOut;
    else if (t == "settlers") c.type = CommandType::Settlers;
    else if (t == "game") c.type = CommandType::SetGame;
    else if (t == "bond") c.type = CommandType::BondEvent;
    else {
        d["message"] = to_godot("Unknown intervention: " + t);
        return d;
    }
    c.cell = cell < 0 ? histsim::kInvalidCell : static_cast<histsim::CellId>(cell);
    c.radius_km = radius_km;
    c.amount = amount;
    c.amount2 = amount2;
    c.years = years;
    try {
        const histsim::InterventionResult r = sim_->apply(c);
        d["ok"] = r.ok;
        d["message"] = to_godot(r.message);
        d["people"] = r.people;
        d["cells"] = static_cast<int64_t>(r.cells);
    } catch (const std::exception& e) {
        d["message"] = to_godot(e.what());
    }
    return d;
}

double HistSimWorld::get_imposed_forcing() const { return sim_ ? sim_->state().climate.imposed_forcing : 0.0; }

int64_t HistSimWorld::get_intervention_count() const {
    return sim_ ? static_cast<int64_t>(sim_->state().commands.size()) : 0;
}

PackedInt64Array HistSimWorld::get_event_years(const String& kind) const {
    PackedInt64Array out;
    if (!sim_) return out;
    const std::string k = to_std(kind);
    const histsim::EventType type = k == "eruption"        ? histsim::EventType::Eruption
                                    : k == "domestication" ? histsim::EventType::Domestication
                                    : k == "bond"          ? histsim::EventType::BondEvent
                                                           : histsim::EventType::Intervention;
    for (const histsim::Event& e : sim_->state().events) {
        if (e.type == type) out.push_back(e.tick / histsim::kTicksPerYear);
    }
    return out;
}

void HistSimWorld::set_highlight(int64_t cell, double radius_km) {
    highlight_on_ = false;
    if (!sim_ || cell < 0 || static_cast<std::size_t>(cell) >= sim_->world().cell_count()) return;
    highlight_.assign(sim_->world().cell_count(), 0);
    for (const auto& wc : histsim::cells_within(sim_->world(), static_cast<histsim::CellId>(cell), radius_km, false)) {
        highlight_[wc.cell] = 1;
    }
    highlight_on_ = true;
}

void HistSimWorld::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_world", "seed", "level", "ocean_fraction"), &HistSimWorld::create_world);
    ClassDB::bind_method(D_METHOD("load_snapshot", "path"), &HistSimWorld::load_snapshot);
    ClassDB::bind_method(D_METHOD("save_snapshot", "path"), &HistSimWorld::save_snapshot);
    ClassDB::bind_method(D_METHOD("has_world"), &HistSimWorld::has_world);
    ClassDB::bind_method(D_METHOD("get_last_error"), &HistSimWorld::get_last_error);
    ClassDB::bind_method(D_METHOD("step_years", "years"), &HistSimWorld::step_years);
    ClassDB::bind_method(D_METHOD("step_for_budget", "max_years", "budget_msec"), &HistSimWorld::step_for_budget);
    ClassDB::bind_method(D_METHOD("get_year"), &HistSimWorld::get_year);
    ClassDB::bind_method(D_METHOD("get_population"), &HistSimWorld::get_population);
    ClassDB::bind_method(D_METHOD("get_inhabited_cells"), &HistSimWorld::get_inhabited_cells);
    ClassDB::bind_method(D_METHOD("get_cell_count"), &HistSimWorld::get_cell_count);
    ClassDB::bind_method(D_METHOD("get_seed"), &HistSimWorld::get_seed);
    ClassDB::bind_method(D_METHOD("get_level"), &HistSimWorld::get_level);
    ClassDB::bind_method(D_METHOD("get_forager_capacity"), &HistSimWorld::get_forager_capacity);
    ClassDB::bind_method(D_METHOD("get_capacity"), &HistSimWorld::get_capacity);
    ClassDB::bind_method(D_METHOD("get_global_anomaly"), &HistSimWorld::get_global_anomaly);
    ClassDB::bind_method(D_METHOD("get_people_counts"), &HistSimWorld::get_people_counts);
    ClassDB::bind_method(D_METHOD("get_state_hash"), &HistSimWorld::get_state_hash);
    ClassDB::bind_method(D_METHOD("get_history", "max_points"), &HistSimWorld::get_history);
    ClassDB::bind_method(D_METHOD("get_cell_pyramid", "cell"), &HistSimWorld::get_cell_pyramid);
    ClassDB::bind_method(D_METHOD("apply_intervention", "type", "cell", "radius_km", "amount", "amount2", "years"),
                         &HistSimWorld::apply_intervention);
    ClassDB::bind_method(D_METHOD("get_imposed_forcing"), &HistSimWorld::get_imposed_forcing);
    ClassDB::bind_method(D_METHOD("get_intervention_count"), &HistSimWorld::get_intervention_count);
    ClassDB::bind_method(D_METHOD("get_event_years", "kind"), &HistSimWorld::get_event_years);
    ClassDB::bind_method(D_METHOD("get_bond_spans"), &HistSimWorld::get_bond_spans);
    ClassDB::bind_method(D_METHOD("set_highlight", "cell", "radius_km"), &HistSimWorld::set_highlight);
    ClassDB::bind_method(D_METHOD("get_modes"), &HistSimWorld::get_modes);
    ClassDB::bind_method(D_METHOD("get_legend", "mode"), &HistSimWorld::get_legend);
    ClassDB::bind_method(D_METHOD("set_map_size", "width", "height"), &HistSimWorld::set_map_size);
    ClassDB::bind_method(D_METHOD("render_map", "mode", "coastlines", "rivers"), &HistSimWorld::render_map,
                         DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_native_species", "cell"), &HistSimWorld::get_native_species);
    ClassDB::bind_method(D_METHOD("cell_at", "x", "y"), &HistSimWorld::cell_at);
    ClassDB::bind_method(D_METHOD("describe_cell", "cell"), &HistSimWorld::describe_cell);
}
