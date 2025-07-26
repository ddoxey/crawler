#ifndef RESULT_WRITER_HPP
#define RESULT_WRITER_HPP

#include <string>

class ResultWriter {
 public:
  void save(const std::string& filename, const std::string& data);
};

#endif  // RESULT_WRITER_HPP
