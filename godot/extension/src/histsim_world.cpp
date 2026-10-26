#include "histsim_world.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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
        const histsim::view::Legend l = histsim::view::legend(to_std(mode));
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

Ref<Image> HistSimWorld::render_map(const String& mode, bool coastlines) {
    if (!sim_) {
        fail("no world to render");
        return Ref<Image>();
    }
    try {
        ensure_raster();
        histsim::view::color_cells(*sim_, to_std(mode), colors_);
        histsim::view::RenderOptions options;
        options.coastlines = coastlines;
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
    ClassDB::bind_method(D_METHOD("get_state_hash"), &HistSimWorld::get_state_hash);
    ClassDB::bind_method(D_METHOD("get_modes"), &HistSimWorld::get_modes);
    ClassDB::bind_method(D_METHOD("get_legend", "mode"), &HistSimWorld::get_legend);
    ClassDB::bind_method(D_METHOD("set_map_size", "width", "height"), &HistSimWorld::set_map_size);
    ClassDB::bind_method(D_METHOD("render_map", "mode", "coastlines"), &HistSimWorld::render_map);
    ClassDB::bind_method(D_METHOD("cell_at", "x", "y"), &HistSimWorld::cell_at);
    ClassDB::bind_method(D_METHOD("describe_cell", "cell"), &HistSimWorld::describe_cell);
}
