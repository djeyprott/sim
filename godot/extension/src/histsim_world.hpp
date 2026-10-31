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

#include <optional>
#include <string>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/view/map_modes.hpp"
#include "histsim/view/raster.hpp"

class HistSimWorld : public godot::RefCounted {
    GDCLASS(HistSimWorld, godot::RefCounted)

public:
    // Lifecycle. Each returns false and sets get_last_error() on failure.
    bool create_world(int64_t seed, int64_t level, double ocean_fraction);
    bool load_snapshot(const godot::String& path);
    bool save_snapshot(const godot::String& path);
    bool has_world() const { return sim_.has_value(); }
    godot::String get_last_error() const;

    // Time. step_for_budget runs whole years until max_years are done or the
    // time budget is spent, and returns how many years it ran.
    void step_years(int64_t years);
    int64_t step_for_budget(int64_t max_years, double budget_msec);

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
                                        bool borders = false);
    int64_t cell_at(int64_t x, int64_t y) const;
    // Array of [label, value] pairs (PackedStringArray each).
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

protected:
    static void _bind_methods();

private:
    void fail(const std::string& message);
    void ensure_raster();

    std::optional<histsim::Simulation> sim_;
    std::optional<histsim::view::MapRaster> raster_;
    int map_width_ = 2048;
    int map_height_ = 1024;
    std::string last_error_;
    std::vector<histsim::view::Rgba> colors_;
    std::vector<std::uint8_t> rgba_;
    std::vector<std::uint8_t> highlight_;
    bool highlight_on_ = false;
};
