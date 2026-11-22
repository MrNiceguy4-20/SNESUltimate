#include "cheats.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
int main() {
    const std::filesystem::path db = std::filesystem::path(__FILE__).parent_path().parent_path() / "cheats.db";
    CheatManager manager;
    if (!manager.loadDatabase(db, "SUPER MARIO WORLD")) { std::cerr << "no Super Mario World cheats\n"; return 1; }
    bool multi = false;
    for (const auto& entry : manager.entries()) {
        if (entry.patches.empty()) { std::cerr << "empty patch list\n"; return 1; }
        if (entry.patches.size() > 1) multi = true;
    }
    CheatManager none;
    if (none.loadDatabase(db, "ZZZZ NOT A GAME") || !none.entries().empty()) { std::cerr << "unexpected match\n"; return 1; }
    std::cout << "cheat database ok: " << manager.entries().size() << " entries, multi-part " << multi << "\n";
    return 0;
}
