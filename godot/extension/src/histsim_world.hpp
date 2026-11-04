// HistSimWorld: the Godot-side handle to one simulation run.
//
// It wraps the Sim API (histsim::Simulation) and the display layer
// (histsim::view). GDScript never touches simulation state directly: it calls
// these methods and receives plain Godot values and images.
#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <memory>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <string>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/view/map_modes.hpp"
#include "histsim/view/raster.hpp"
#include "histsim/view/timeline.hpp"

class HistSimWorld : public godot::RefCounted {
    GDCLASS(HistSimWorld, godot::RefCounted)

public:
    ~HistSimWorld();

    // Lifecycle. Each returns false and sets get_last_error() on failure.
    bool create_world(int64_t seed, int64_t level, double ocean_fraction);
    // kind: "generated" or "earth" (Earth's land and sea; ocean_fraction unused).
    bool create_world_of_kind(int64_t seed, int64_t level, double ocean_fraction, const godot::String& kind);
    godot::String get_world_kind() const;
    bool load_snapshot(const godot::String& path);
    bool save_snapshot(const godot::String& path);
    bool has_world() const { return sim_.has_value(); }
    godot::String get_last_error() const;

    // Time. step_for_budget runs whole years until max_years are done or the
    // time budget is spent, and returns how many years it ran.
    void step_years(int64_t years);
    int64_t step_for_budget(int64_t max_years, double budget_msec);

    // Running in the background: queue_years hands years to a worker thread,
    // which runs them one at a time while the UI goes on drawing;
    // take_finished_years returns how many it has run since the last call.
    // Every other method waits for the year in progress to finish, so it
    // always sees a whole year.
    void queue_years(int64_t years);
    int64_t get_queued_years() const;
    void clear_queue();
    int64_t take_finished_years();
    bool is_running() const;  // years are queued
    // Time the run spent paused for the UI, with years queued, since the
    // last call.
    double take_paused_msec();
    // A turn for the UI to read without waiting: begin_turn returns true and
    // holds the simulation between years until end_turn; if a year is in
    // progress it returns false at once and the run pauses after that year,
    // so a call on a later frame succeeds. Calls made during a turn return
    // at once.
    bool begin_turn();
    void end_turn();

    // The map, drawn on its own thread: prepare_map takes what it needs from
    // the simulation (quickly; in a turn) and hands it to the drawing thread;
    // take_map returns the finished image once (null until then).
    // render_map does both at once and waits.
    bool prepare_map(const godot::String& mode, bool coastlines, bool rivers = true, bool borders = false,
                     bool routes = false, bool migrations = false);
    bool is_map_pending() const;
    godot::Ref<godot::Image> take_map();

    // Run summary.
    int64_t get_year() const;
    int64_t get_population() const;
    int64_t get_inhabited_cells() const;
    int64_t get_cell_count() const;
    int64_t get_seed() const;
    int64_t get_level() const;
    double get_forager_capacity() const;  // baseline, at the start of the run
    double get_capacity() const;           // this year's
    double get_global_anomaly() const;     // global mean temperature vs. baseline, degrees C
    godot::Dictionary get_people_counts() const;
    godot::PackedStringArray get_disease_names() const;  // index = disease id for "outbreak"  // living languages, families and cultures
    godot::String get_state_hash() const;

    // Yearly history, downsampled to at most max_points rows:
    // {year, population, capacity, anomaly} as packed arrays.
    godot::Dictionary get_history(int64_t max_points) const;

    // Map modes: Array of {id, name, description}.
    godot::Array get_modes() const;
    // {title, ramp: [{label, color}], swatches: [{label, color}]}
    godot::Dictionary get_legend(const godot::String& mode) const;

    // Map image. set_map_size rebuilds the pixel-to-cell lookup.
    void set_map_size(int64_t width, int64_t height);
    godot::Ref<godot::Image> render_map(const godot::String& mode, bool coastlines, bool rivers = true,
                                        bool borders = false, bool routes = false, bool migrations = false);
    int64_t cell_at(int64_t x, int64_t y) const;
    // Array of [label, value, section, figure] rows (PackedStringArray each;
    // figure: the id of a person the row names, "0" if none).
    godot::Array describe_cell(int64_t cell) const;
    // Names of the wild species native to a cell.
    godot::PackedStringArray get_native_species(int64_t cell) const;
    // {female, male}: PackedInt64Array of 21 five-year age bins each.
    godot::Dictionary get_cell_pyramid(int64_t cell) const;

    // Interventions. type: "eruption", "forcing", "climate", "plague",
    // "wipeout", "settlers" or "game" (see libsim interventions.hpp for what
    // the numbers mean). Returns {ok, message, people, cells}.
    godot::Dictionary apply_intervention(const godot::String& type, int64_t cell, double radius_km, double amount,
                                         double amount2, int64_t years);
    double get_imposed_forcing() const;
    int64_t get_intervention_count() const;
    // Years (since the run began) of events of a kind: "eruption" or "intervention".
    godot::PackedInt64Array get_event_years(const godot::String& kind) const;
    godot::PackedInt64Array get_bond_spans() const;  // [start, end, start, end, ...] years of Bond events

    // Outline the cells within radius_km of a cell on the next render_map
    // (cell < 0 clears it).
    void set_highlight(int64_t cell, double radius_km);
    // Fades all but the cells in the same group as `cell` (its language,
    // culture, faith, ruler, ...) in modes that group cells; -1 clears it.
    // Returns whether anything is focused.
    bool set_focus(const godot::String& mode, int64_t cell);
    // Names to write over the map: [{name, u, v, area_km2, level}, ...].
    godot::Array get_polity_labels() const;
    // Names for a map mode: languages, families, cultures, faiths or realms
    // where they are, in the modes that colour them; polities otherwise.
    // Same entries as get_polity_labels (level "" and capital_u -1 for groups).
    godot::Array get_map_labels(const godot::String& mode) const;
    // Timeline entries, newest first: [{year, kind, text, cell}, ...] of a
    // kind ("" all), about a polity (0 any), at a cell (-1 anywhere).
    godot::Array get_timeline(const godot::String& kind, int64_t polity, int64_t cell, int64_t max_entries) const;
    // A historical figure's page: {id, title, subtitle, life, family, deeds},
    // the last three arrays of {label, text, figure, cell} (figure: a person
    // to link to, 0 none; cell -1 none). Empty for an unknown id.
    godot::Dictionary get_figure(int64_t id) const;
    // The figures most worth reading about now: [{id, title, subtitle}, ...].
    godot::Array get_notable_figures(int64_t max) const;
    // The polity ruling a cell (0 none).
    int64_t get_cell_polity(int64_t cell) const;
    // Where a cell is on the flat map, 0-1 across and down.
    godot::Vector2 get_cell_uv(int64_t cell) const;

protected:
    static void _bind_methods();

private:
    // Holds the simulation for one call from the UI.
    class Guard {
    public:
        explicit Guard(const HistSimWorld& w);
        ~Guard();
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        const HistSimWorld& w_;
    };
    [[nodiscard]] Guard hold() const;
    void worker_loop();
    void stop_worker();

    // What the drawing thread needs, copied out of the simulation.
    struct MapJob {
        std::shared_ptr<const histsim::view::MapRaster> raster;
        const histsim::World* world = nullptr;  // never changes while it lives
        std::vector<histsim::view::Rgba> colors;
        bool coastlines = true, rivers = false;
        std::vector<std::uint32_t> borders;
        std::vector<std::uint8_t> highlight, focus;
        histsim::view::Rgba focus_color{255, 255, 255, 255};
        std::vector<histsim::view::RouteLine> routes, migrations;
        std::vector<histsim::view::MapMarker> markers;
    };
    MapJob make_map_job(const std::string& mode, bool coastlines, bool rivers, bool borders, bool routes,
                        bool migrations);
    static void draw_map_job(const MapJob& job, std::vector<std::uint8_t>& rgba);
    void drawer_loop();
    void drop_map_jobs();

    mutable std::mutex map_mutex_;
    std::condition_variable map_wake_, map_idle_;
    std::thread drawer_;
    std::optional<MapJob> map_job_;
    bool drawing_ = false, map_quit_ = false, drawn_ready_ = false;
    std::vector<std::uint8_t> drawn_;
    int drawn_width_ = 0, drawn_height_ = 0;
    std::atomic<bool> want_turn_{false};
    std::atomic<int64_t> paused_usec_{0};
    int turns_held_ = 0;

    mutable std::recursive_mutex sim_mutex_;
    mutable std::condition_variable_any wake_;
    mutable std::atomic<int> readers_waiting_{0};
    std::thread worker_;
    bool quit_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<int64_t> queued_{0};
    std::atomic<int64_t> finished_{0};

    void fail(const std::string& message);
    void ensure_raster();

    std::optional<histsim::Simulation> sim_;
    std::shared_ptr<const histsim::view::MapRaster> raster_;
    int map_width_ = 2048;
    int map_height_ = 1024;
    std::string last_error_;
    std::vector<histsim::view::Rgba> colors_;
    std::vector<std::uint8_t> rgba_;
    std::vector<std::uint8_t> highlight_;
    bool highlight_on_ = false;
    std::vector<std::uint8_t> focus_;
    bool focus_on_ = false;
    histsim::view::Rgba focus_color_{255, 255, 255, 255};
};
