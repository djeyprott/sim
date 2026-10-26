#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "histsim_world.hpp"

using namespace godot;

namespace {

void initialize_histsim(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(HistSimWorld);
}

void uninitialize_histsim(ModuleInitializationLevel) {}

}  // namespace

extern "C" {

// Entry point named in godot/project/histsim.gdextension.
GDExtensionBool GDE_EXPORT histsim_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                GDExtensionClassLibraryPtr library,
                                                GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_histsim);
    init.register_terminator(uninitialize_histsim);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}

}  // extern "C"
