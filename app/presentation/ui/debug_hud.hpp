#pragma once

// The technical read-out: all the numbers (rule 39).
//
// It is the Milestone 5 read-out, preserved. It was not reduced or "improved":
// it is what the headless verification prints, it is what the tolerances of
// docs/validation/tolerances.md are checked against, and a number that left it
// would stop being checkable. What changed is that it stopped being the DEFAULT
// interface and moved behind F3 -- which is what rule 39 asks.

#include <string>
#include <vector>

namespace sf::app {

class FlightApp;

namespace debug_hud {

[[nodiscard]] std::vector<std::string> hud_lines(const FlightApp& flight, bool compact);
[[nodiscard]] std::vector<std::string> mission_lines(const FlightApp& flight);
[[nodiscard]] std::vector<std::string> sky_lines(const FlightApp& flight);
// What the SAME code does at a speed the ship does not have. Labelled, every
// time: the state is untouched, and this is a question asked of the optics, not
// a statement about the flight.
[[nodiscard]] std::vector<std::string> sky_projection_lines(FlightApp& flight, double beta);
// Cut at the blank line nearest the middle, so a section is never split, and
// pad the left column to a fixed width. Only correct because the face is
// monospace -- which it is on purpose, and for this very reason.
[[nodiscard]] std::vector<std::string> two_columns(const std::vector<std::string>& lines);

}  // namespace debug_hud
}  // namespace sf::app
