#pragma once

// docs/gameplay/controls.md and controls.json, written from the key table
// (rule 75).
//
// Generated and not written by hand, for the same reason the help panel is
// generated: a key table copied into a document is a table that ages in
// silence. Whoever wants to change a key changes input_actions.cpp and runs
// `spaceflight --dump-controls`.

#include <string>

namespace sf::app::controls_doc {

[[nodiscard]] std::string markdown();
// The SAME table as JSON, for whoever is not a human reader. The pilot's manual
// resolves `{{key:camera_cycle}}` from it and fails to build if the action does
// not exist -- without that, the manual would be a third copy of the keys.
[[nodiscard]] std::string json();

}  // namespace sf::app::controls_doc
