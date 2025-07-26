#include "ResultWriter.hpp"
#include <fstream>

void ResultWriter::save(const std::string& filename, const std::string& data) {
  std::ofstream file("results/" + filename);
  file << data;
}
