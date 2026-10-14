#include "sdk/IModule.hpp"

#include <algorithm>

namespace mydb::sdk {

std::string moduleFileStem(std::string_view moduleId) {
    std::string stem = "mydb_";
    stem += moduleId;
    std::ranges::replace(stem, '.', '_');
    return stem;
}

} // namespace mydb::sdk
