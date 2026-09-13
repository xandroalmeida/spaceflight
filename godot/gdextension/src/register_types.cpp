// GDExtension entry point.  Registers two classes and nothing else; everything
// the project needs beyond them is scene-side (ADR-0002).
//
//   SpaceflightSimulation  owns the state and hands out snapshots
//   SpaceflightSky         owns a star catalogue and a colour table
//
// They are separate because they are separate concerns: the only thing that
// passes between them is a velocity, and GDScript carries it without touching it
// (docs/architecture/relativistic-shaders.md section 6).

#include "simulation_node.hpp"
#include "sky_node.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_spaceflight_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(spaceflight_godot::SpaceflightSimulation);
    GDREGISTER_CLASS(spaceflight_godot::SpaceflightSky);
}

void uninitialize_spaceflight_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT spaceflight_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                    const GDExtensionClassLibraryPtr library,
                                                    GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init_object(get_proc_address, library, initialization);

    init_object.register_initializer(initialize_spaceflight_module);
    init_object.register_terminator(uninitialize_spaceflight_module);
    init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_object.init();
}
}
