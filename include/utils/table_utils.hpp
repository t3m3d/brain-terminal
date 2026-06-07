#pragma once
#include <string>
#include <vector>

namespace brain::utils {

std::string formatTable(const std::vector<std::vector<std::string>>& rows, int padding = 2);

}