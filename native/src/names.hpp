// Silly Season: invented names for new talents and technical staff.
//
// A new talent takes over a fictional reserve driver's row, so the game keeps his nationality and his
// three-letter initials. The surname starts with those initials where they read as a word start
// (TRE -> Tre...), so the timing screens still match.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace ss::names {

// nationality (the game's id: "spain", "britain"...) -> a first name and a surname
std::string first(std::string_view nationality, bool female, uint64_t pick);
std::string surname(std::string_view nationality, std::string_view initials, uint64_t pick);

// a nationality for invented staff
std::string nationality(uint64_t pick);

} // namespace ss::names
