#include "histsim_world.hpp"

#include "histsim/view/figures.hpp"

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
#include <thread>

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

String HistSimWorld::get_last_error() const {
    const auto guard = hold();
    return to_godot(last_error_);
}

bool HistSimWorld::create_world(int64_t seed, int64_t level, double ocean_fraction) {
    const auto guard = hold();
    return create_world_of_kind(seed, level, ocean_fraction, "generated");
}

String HistSimWorld::get_world_kind() const {
    if (!sim_) return String();
    return String(histsim::world_kind_name(sim_->world().params.kind));
}

bool HistSimWorld::create_world_of_kind(int64_t seed, int64_t level, double ocean_fraction, const String& kind) {
    const auto guard = hold();
    queued_ = 0;
    finished_ = 0;
    drop_map_jobs();
    const std::string k = to_std(kind);
    if (k != "generated" && k != "earth") {
        fail("world kind must be \"generated\" or \"earth\"");
        return false;
    }
    if (level < 2 || level > 8) {
        fail("grid level must be between 2 and 8");
        return false;
    }
    try {
        histsim::WorldParams p;
        p.seed = static_cast<std::uint64_t>(seed);
        p.grid_level = static_cast<int>(level);
        p.ocean_fraction = ocean_fraction;
        p.kind = k == "earth" ? histsim::WorldKind::Earth : histsim::WorldKind::Generated;
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
    const auto guard = hold();
    queued_ = 0;
    finished_ = 0;
    drop_map_jobs();
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
    const auto guard = hold();
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
    const auto guard = hold();
    if (sim_ && years > 0) {
        sim_->step_years(years);
    }
}

int64_t HistSimWorld::step_for_budget(int64_t max_years, double budget_msec) {
    const auto guard = hold();
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

// ---- Running in the background.

HistSimWorld::~HistSimWorld() { stop_worker(); }

HistSimWorld::Guard HistSimWorld::hold() const { return Guard(*this); }

HistSimWorld::Guard::Guard(const HistSimWorld& w) : w_(w) {
    w_.readers_waiting_.fetch_add(1);
    w_.sim_mutex_.lock();
    w_.readers_waiting_.fetch_sub(1);
}

HistSimWorld::Guard::~Guard() {
    w_.sim_mutex_.unlock();
    w_.wake_.notify_all();
}

void HistSimWorld::stop_worker() {
    {
        std::lock_guard<std::recursive_mutex> lock(sim_mutex_);
        quit_ = true;
        queued_ = 0;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    quit_ = false;
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        map_quit_ = true;
    }
    map_wake_.notify_all();
    if (drawer_.joinable()) drawer_.join();
    map_quit_ = false;
}

void HistSimWorld::worker_loop() {
    std::unique_lock<std::recursive_mutex> lock(sim_mutex_);
    auto may_run = [&] { return quit_ || (queued_.load() > 0 && readers_waiting_.load() == 0 && !want_turn_.load()); };
    for (;;) {
        // The UI goes first: a reader waiting, or a turn asked for, pauses the
        // run after the year in progress. (The timeout covers a wake-up sent
        // while not waiting: the UI changes these flags without the lock.)
        if (!may_run()) {
            const auto paused_at = std::chrono::steady_clock::now();
            const bool had_work = queued_.load() > 0;
            while (!may_run()) wake_.wait_for(lock, std::chrono::milliseconds(10));
            if (had_work) paused_usec_ += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - paused_at).count();
        }
        if (quit_) return;
        try {
            sim_->step_years(1);
            finished_.fetch_add(1);
        } catch (const std::exception& e) {
            queued_ = 0;
            last_error_ = e.what();
            failed_ = true;
            continue;
        }
        // One fewer to run, unless the queue was cleared meanwhile.
        for (int64_t q = queued_.load(); q > 0 && !queued_.compare_exchange_weak(q, q - 1);) {
        }
    }
}

void HistSimWorld::queue_years(int64_t years) {
    if (!sim_ || years <= 0) return;
    queued_.fetch_add(years);
    if (!worker_.joinable()) worker_ = std::thread([this] { worker_loop(); });
    wake_.notify_all();
}

int64_t HistSimWorld::get_queued_years() const {
    return queued_;
}

void HistSimWorld::clear_queue() {
    queued_ = 0;
}

int64_t HistSimWorld::take_finished_years() {
    if (failed_.exchange(false)) {
        const auto guard = hold();
        UtilityFunctions::push_error(to_godot("histsim: " + last_error_));
    }
    return finished_.exchange(0);
}

bool HistSimWorld::begin_turn() {
    if (sim_mutex_.try_lock()) {
        ++turns_held_;
        return true;
    }
    want_turn_ = true;  // the run pauses after the year in progress
    wake_.notify_all();
    return false;
}

void HistSimWorld::end_turn() {
    if (turns_held_ == 0) return;
    --turns_held_;
    want_turn_ = false;
    sim_mutex_.unlock();
    wake_.notify_all();
}

bool HistSimWorld::is_running() const { return queued_.load() > 0; }

double HistSimWorld::take_paused_msec() { return static_cast<double>(paused_usec_.exchange(0)) / 1000.0; }

// ---- Drawing the map on its own thread.

HistSimWorld::MapJob HistSimWorld::make_map_job(const std::string& mode, bool coastlines, bool rivers, bool borders,
                                                bool routes, bool migrations) {
    MapJob job;
    ensure_raster();
    job.raster = raster_;
    job.world = &sim_->world();
    histsim::view::color_cells(*sim_, mode, job.colors);
    job.coastlines = coastlines;
    job.rivers = rivers;
    const std::size_t n = sim_->world().cell_count();
    if (borders && sim_->state().polity_of.size() == n) job.borders = sim_->state().polity_of;
    if (highlight_on_ && highlight_.size() == n) job.highlight = highlight_;
    if (focus_on_ && focus_.size() == n) {
        job.focus = focus_;
        job.focus_color = focus_color_;
    }
    if (routes) job.routes = histsim::view::route_lines(*sim_);
    if (mode == "bondage") {
        job.migrations = histsim::view::slave_route_lines(*sim_);  // the slave routes, always
    } else if (migrations) {
        job.migrations = histsim::view::migration_lines(*sim_);
    }
    job.markers = histsim::view::capital_markers(*sim_, mode);
    return job;
}

void HistSimWorld::draw_map_job(const MapJob& job, std::vector<std::uint8_t>& rgba) {
    histsim::view::RenderOptions options;
    options.coastlines = job.coastlines;
    options.rivers = job.rivers;
    if (!job.borders.empty()) options.borders = &job.borders;
    if (!job.highlight.empty()) options.highlight = &job.highlight;
    if (!job.focus.empty()) {
        options.focus = &job.focus;
        options.focus_color = job.focus_color;
    }
    if (!job.routes.empty()) options.routes = &job.routes;
    if (!job.migrations.empty()) options.migrations = &job.migrations;
    if (!job.markers.empty()) options.markers = &job.markers;
    histsim::view::render(*job.raster, *job.world, job.colors, rgba, options);
}

bool HistSimWorld::prepare_map(const String& mode, bool coastlines, bool rivers, bool borders, bool routes,
                               bool migrations) {
    const auto guard = hold();
    if (!sim_) return false;
    try {
        MapJob job = make_map_job(to_std(mode), coastlines, rivers, borders, routes, migrations);
        {
            std::lock_guard<std::mutex> lock(map_mutex_);
            map_job_ = std::move(job);  // replaces one not yet started
        }
        if (!drawer_.joinable()) drawer_ = std::thread([this] { drawer_loop(); });
        map_wake_.notify_all();
        return true;
    } catch (const std::exception& e) {
        fail(e.what());
        return false;
    }
}

void HistSimWorld::drawer_loop() {
    std::unique_lock<std::mutex> lock(map_mutex_);
    for (;;) {
        map_wake_.wait(lock, [&] { return map_quit_ || map_job_.has_value(); });
        if (map_quit_) return;
        MapJob job = std::move(*map_job_);
        map_job_.reset();
        drawing_ = true;
        lock.unlock();
        std::vector<std::uint8_t> rgba;
        bool ok = true;
        try {
            draw_map_job(job, rgba);
        } catch (const std::exception&) {
            ok = false;
        }
        lock.lock();
        drawing_ = false;
        if (ok) {
            drawn_ = std::move(rgba);
            drawn_width_ = job.raster->width();
            drawn_height_ = job.raster->height();
            drawn_ready_ = true;
        }
        map_idle_.notify_all();
    }
}

void HistSimWorld::drop_map_jobs() {
    std::unique_lock<std::mutex> lock(map_mutex_);
    map_job_.reset();
    drawn_ready_ = false;
    // The map being drawn refers to the old world: let it finish first.
    map_idle_.wait(lock, [&] { return !drawing_; });
    drawn_ready_ = false;
}

bool HistSimWorld::is_map_pending() const {
    std::lock_guard<std::mutex> lock(map_mutex_);
    return map_job_.has_value() || drawing_;
}

Ref<Image> HistSimWorld::take_map() {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        if (!drawn_ready_) return Ref<Image>();
        drawn_ready_ = false;
        rgba.swap(drawn_);
        width = drawn_width_;
        height = drawn_height_;
    }
    PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(rgba.size()));
    std::memcpy(bytes.ptrw(), rgba.data(), rgba.size());
    return Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, bytes);
}

int64_t HistSimWorld::get_year() const {
    const auto guard = hold();
    return sim_ ? sim_->year() : 0;
}
int64_t HistSimWorld::get_population() const {
    const auto guard = hold();
    return sim_ ? sim_->state().total_population() : 0;
}
int64_t HistSimWorld::get_inhabited_cells() const {
    const auto guard = hold();
    if (!sim_) return 0;
    // Counted here: summary() also hashes the whole state.
    int64_t cells = 0;
    for (std::uint32_t people : sim_->state().cell_population) cells += people > 0 ? 1 : 0;
    return cells;
}
int64_t HistSimWorld::get_cell_count() const {
    return sim_ ? static_cast<int64_t>(sim_->world().cell_count()) : 0;
}
int64_t HistSimWorld::get_seed() const {
    return sim_ ? static_cast<int64_t>(sim_->world().params.seed) : 0;
}
int64_t HistSimWorld::get_level() const {
    return sim_ ? sim_->world().params.grid_level : 0;
}

double HistSimWorld::get_forager_capacity() const {
    const auto guard = hold();
    if (!sim_) return 0.0;
    double total = 0.0;
    for (double k : sim_->world().forager_capacity) total += k;
    return total;
}

double HistSimWorld::get_capacity() const {
    const auto guard = hold();
    if (!sim_) return 0.0;
    double total = 0.0;
    for (double k : sim_->state().env.capacity) total += k;
    return total;
}

double HistSimWorld::get_global_anomaly() const {
    const auto guard = hold();
    return sim_ ? sim_->state().climate.global_anomaly_c : 0.0;
}

PackedStringArray HistSimWorld::get_disease_names() const {
    const auto guard = hold();
    PackedStringArray out;
    const histsim::Content& c = sim_ ? *sim_->world().content : histsim::default_content();
    for (const auto& d : c.diseases) out.push_back(to_godot(d.name));
    return out;
}

Dictionary HistSimWorld::get_people_counts() const {
    const auto guard = hold();
    Dictionary d;
    const histsim::PeopleCounts p = sim_ ? histsim::count_peoples(sim_->state()) : histsim::PeopleCounts{};
    d["languages"] = static_cast<int64_t>(p.languages);
    d["families"] = static_cast<int64_t>(p.families);
    d["cultures"] = static_cast<int64_t>(p.cultures);
    return d;
}

Dictionary HistSimWorld::get_history(int64_t max_points) const {
    const auto guard = hold();
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
    const auto guard = hold();
    PackedInt64Array out;
    if (!sim_) return out;
    for (const histsim::BondEvent& b : sim_->state().climate.bond_history) {
        out.push_back(b.start_year);
        out.push_back(b.start_year + b.years);
    }
    return out;
}

String HistSimWorld::get_state_hash() const {
    const auto guard = hold();
    if (!sim_) return String();
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(sim_->state_hash()));
    return String(buf);
}

Array HistSimWorld::get_modes() const {
    const auto guard = hold();
    Array out;
    for (const auto& m : histsim::view::map_modes()) {
        Dictionary d;
        d["id"] = to_godot(m.id);
        d["name"] = to_godot(m.name);
        d["description"] = to_godot(m.description);
        d["group"] = to_godot(m.group);
        out.push_back(d);
    }
    return out;
}

Dictionary HistSimWorld::get_legend(const String& mode) const {
    const auto guard = hold();
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
        raster_ = std::make_shared<const histsim::view::MapRaster>(
            histsim::view::MapRaster::build(sim_->world().grid, map_width_, map_height_));
    }
}

Ref<Image> HistSimWorld::render_map(const String& mode, bool coastlines, bool rivers, bool borders, bool routes,
                                    bool migrations) {
    MapJob job;
    {
        const auto guard = hold();
        if (!sim_) {
            fail("no world to render");
            return Ref<Image>();
        }
        try {
            job = make_map_job(to_std(mode), coastlines, rivers, borders, routes, migrations);
        } catch (const std::exception& e) {
            fail(e.what());
            return Ref<Image>();
        }
    }
    try {
        draw_map_job(job, rgba_);  // the simulation goes on meanwhile
    } catch (const std::exception& e) {
        fail(e.what());
        return Ref<Image>();
    }
    PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(rgba_.size()));
    std::memcpy(bytes.ptrw(), rgba_.data(), rgba_.size());
    return Image::create_from_data(job.raster->width(), job.raster->height(), false, Image::FORMAT_RGBA8, bytes);
}

int64_t HistSimWorld::cell_at(int64_t x, int64_t y) const {
    if (!raster_) return -1;
    const histsim::CellId c = raster_->cell_at(static_cast<int>(x), static_cast<int>(y));
    return c == histsim::kInvalidCell ? -1 : static_cast<int64_t>(c);
}

Array HistSimWorld::describe_cell(int64_t cell) const {
    const auto guard = hold();
    Array out;
    if (!sim_ || cell < 0) return out;
    for (const auto& f : histsim::view::describe_cell(*sim_, static_cast<histsim::CellId>(cell))) {
        PackedStringArray pair;
        pair.push_back(to_godot(f.label));
        pair.push_back(to_godot(f.value));
        pair.push_back(to_godot(f.section));
        pair.push_back(String::num_int64(f.figure));
        out.push_back(pair);
    }
    return out;
}

PackedStringArray HistSimWorld::get_native_species(int64_t cell) const {
    const auto guard = hold();
    PackedStringArray out;
    if (!sim_ || cell < 0) return out;
    for (const std::string& name : histsim::view::native_species(*sim_, static_cast<histsim::CellId>(cell))) {
        out.push_back(to_godot(name));
    }
    return out;
}

Dictionary HistSimWorld::get_cell_pyramid(int64_t cell) const {
    const auto guard = hold();
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
    const auto guard = hold();
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
    else if (t == "outbreak") c.type = CommandType::Outbreak;
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

double HistSimWorld::get_imposed_forcing() const {
    const auto guard = hold();
    return sim_ ? sim_->state().climate.imposed_forcing : 0.0;
}

int64_t HistSimWorld::get_intervention_count() const {
    const auto guard = hold();
    return sim_ ? static_cast<int64_t>(sim_->state().commands.size()) : 0;
}

PackedInt64Array HistSimWorld::get_event_years(const String& kind) const {
    const auto guard = hold();
    PackedInt64Array out;
    if (!sim_) return out;
    const std::string k = to_std(kind);
    const histsim::EventType type = k == "eruption"        ? histsim::EventType::Eruption
                                    : k == "domestication" ? histsim::EventType::Domestication
                                    : k == "bond"          ? histsim::EventType::BondEvent
                                    : k == "disease"       ? histsim::EventType::DiseaseEmerged
                                                           : histsim::EventType::Intervention;
    for (const histsim::Event& e : sim_->state().events) {
        if (e.type == type) out.push_back(e.tick / histsim::kTicksPerYear);
    }
    return out;
}

bool HistSimWorld::set_focus(const String& mode, int64_t cell) {
    const auto guard = hold();
    focus_on_ = false;
    if (!sim_ || cell < 0 || static_cast<std::size_t>(cell) >= sim_->world().cell_count()) return false;
    try {
        focus_on_ = histsim::view::same_group_cells(*sim_, to_std(mode), static_cast<histsim::CellId>(cell), focus_,
                                                    &focus_color_);
    } catch (const std::exception& e) {
        fail(e.what());
    }
    return focus_on_;
}

Array HistSimWorld::get_polity_labels() const {
    const auto guard = hold();
    Array out;
    if (!sim_) return out;
    for (const auto& l : histsim::view::polity_labels(*sim_)) {
        Dictionary d;
        d["name"] = to_godot(l.name);
        d["u"] = l.u;
        d["v"] = l.v;
        d["area_km2"] = l.area_km2;
        d["capital_u"] = l.capital_u;
        d["capital_v"] = l.capital_v;
        d["level"] = to_godot(l.level);
        out.push_back(d);
    }
    return out;
}

Array HistSimWorld::get_timeline(const String& kind, int64_t polity, int64_t cell, int64_t max_entries) const {
    const auto guard = hold();
    Array out;
    if (!sim_ || max_entries <= 0) return out;
    const histsim::CellId at = cell < 0 ? histsim::kInvalidCell : static_cast<histsim::CellId>(cell);
    for (const auto& e : histsim::view::timeline(*sim_, to_std(kind), static_cast<std::uint32_t>(std::max<int64_t>(0, polity)), at,
                                                 static_cast<std::size_t>(max_entries))) {
        Dictionary d;
        d["year"] = static_cast<int64_t>(e.year);
        d["kind"] = to_godot(e.kind);
        d["text"] = to_godot(e.text);
        d["cell"] = e.cell == histsim::kInvalidCell ? int64_t{-1} : static_cast<int64_t>(e.cell);
        d["figure"] = static_cast<int64_t>(e.figure);
        out.push_back(d);
    }
    return out;
}

Dictionary HistSimWorld::get_figure(int64_t id) const {
    const auto guard = hold();
    Dictionary d;
    if (!sim_ || id <= 0) return d;
    const histsim::view::FigurePage page = histsim::view::figure_page(*sim_, static_cast<std::uint32_t>(id));
    if (page.id == 0) return d;
    auto lines = [](const std::vector<histsim::view::FigureLine>& v) {
        Array out;
        for (const auto& l : v) {
            Dictionary e;
            e["label"] = to_godot(l.label);
            e["text"] = to_godot(l.text);
            e["figure"] = static_cast<int64_t>(l.figure);
            e["cell"] = l.cell == histsim::kInvalidCell ? int64_t{-1} : static_cast<int64_t>(l.cell);
            out.push_back(e);
        }
        return out;
    };
    d["id"] = static_cast<int64_t>(page.id);
    d["title"] = to_godot(page.title);
    d["subtitle"] = to_godot(page.subtitle);
    d["life"] = lines(page.life);
    d["family"] = lines(page.family);
    d["deeds"] = lines(page.deeds);
    return d;
}

Array HistSimWorld::get_notable_figures(int64_t max) const {
    const auto guard = hold();
    Array out;
    if (!sim_ || max <= 0) return out;
    for (std::uint32_t id : histsim::view::notable_figures(*sim_, static_cast<std::size_t>(max))) {
        const histsim::view::FigurePage page = histsim::view::figure_page(*sim_, id);
        Dictionary d;
        d["id"] = static_cast<int64_t>(id);
        d["title"] = to_godot(page.title);
        d["subtitle"] = to_godot(page.subtitle);
        out.push_back(d);
    }
    return out;
}

int64_t HistSimWorld::get_cell_polity(int64_t cell) const {
    const auto guard = hold();
    if (!sim_ || cell < 0 || static_cast<std::size_t>(cell) >= sim_->world().cell_count()) return 0;
    const auto& of = sim_->state().polity_of;
    return of.empty() ? 0 : static_cast<int64_t>(of[static_cast<std::size_t>(cell)]);
}

Vector2 HistSimWorld::get_cell_uv(int64_t cell) const {
    if (!sim_ || cell < 0) return Vector2(-1, -1);
    double u = 0.0, v = 0.0;
    histsim::view::cell_uv(sim_->world(), static_cast<histsim::CellId>(cell), u, v);
    return Vector2(static_cast<real_t>(u), static_cast<real_t>(v));
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
    ClassDB::bind_method(D_METHOD("create_world_of_kind", "seed", "level", "ocean_fraction", "kind"),
                         &HistSimWorld::create_world_of_kind);
    ClassDB::bind_method(D_METHOD("get_world_kind"), &HistSimWorld::get_world_kind);
    ClassDB::bind_method(D_METHOD("load_snapshot", "path"), &HistSimWorld::load_snapshot);
    ClassDB::bind_method(D_METHOD("save_snapshot", "path"), &HistSimWorld::save_snapshot);
    ClassDB::bind_method(D_METHOD("has_world"), &HistSimWorld::has_world);
    ClassDB::bind_method(D_METHOD("get_last_error"), &HistSimWorld::get_last_error);
    ClassDB::bind_method(D_METHOD("step_years", "years"), &HistSimWorld::step_years);
    ClassDB::bind_method(D_METHOD("step_for_budget", "max_years", "budget_msec"), &HistSimWorld::step_for_budget);
    ClassDB::bind_method(D_METHOD("queue_years", "years"), &HistSimWorld::queue_years);
    ClassDB::bind_method(D_METHOD("get_queued_years"), &HistSimWorld::get_queued_years);
    ClassDB::bind_method(D_METHOD("clear_queue"), &HistSimWorld::clear_queue);
    ClassDB::bind_method(D_METHOD("take_finished_years"), &HistSimWorld::take_finished_years);
    ClassDB::bind_method(D_METHOD("begin_turn"), &HistSimWorld::begin_turn);
    ClassDB::bind_method(D_METHOD("end_turn"), &HistSimWorld::end_turn);
    ClassDB::bind_method(D_METHOD("is_running"), &HistSimWorld::is_running);
    ClassDB::bind_method(D_METHOD("take_paused_msec"), &HistSimWorld::take_paused_msec);
    ClassDB::bind_method(D_METHOD("prepare_map", "mode", "coastlines", "rivers", "borders", "routes", "migrations"),
                         &HistSimWorld::prepare_map, DEFVAL(true), DEFVAL(false), DEFVAL(false), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("is_map_pending"), &HistSimWorld::is_map_pending);
    ClassDB::bind_method(D_METHOD("take_map"), &HistSimWorld::take_map);
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
    ClassDB::bind_method(D_METHOD("get_disease_names"), &HistSimWorld::get_disease_names);
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
    ClassDB::bind_method(D_METHOD("set_focus", "mode", "cell"), &HistSimWorld::set_focus);
    ClassDB::bind_method(D_METHOD("get_figure", "id"), &HistSimWorld::get_figure);
    ClassDB::bind_method(D_METHOD("get_notable_figures", "max"), &HistSimWorld::get_notable_figures);
    ClassDB::bind_method(D_METHOD("get_polity_labels"), &HistSimWorld::get_polity_labels);
    ClassDB::bind_method(D_METHOD("get_timeline", "kind", "polity", "cell", "max_entries"), &HistSimWorld::get_timeline);
    ClassDB::bind_method(D_METHOD("get_cell_polity", "cell"), &HistSimWorld::get_cell_polity);
    ClassDB::bind_method(D_METHOD("get_cell_uv", "cell"), &HistSimWorld::get_cell_uv);
    ClassDB::bind_method(D_METHOD("get_modes"), &HistSimWorld::get_modes);
    ClassDB::bind_method(D_METHOD("get_legend", "mode"), &HistSimWorld::get_legend);
    ClassDB::bind_method(D_METHOD("set_map_size", "width", "height"), &HistSimWorld::set_map_size);
    ClassDB::bind_method(D_METHOD("render_map", "mode", "coastlines", "rivers", "borders", "routes", "migrations"),
                         &HistSimWorld::render_map, DEFVAL(true), DEFVAL(false), DEFVAL(false), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_native_species", "cell"), &HistSimWorld::get_native_species);
    ClassDB::bind_method(D_METHOD("cell_at", "x", "y"), &HistSimWorld::cell_at);
    ClassDB::bind_method(D_METHOD("describe_cell", "cell"), &HistSimWorld::describe_cell);
}
