#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
class PrivateFile {
  public:
    static void write(const std::filesystem::path &file, const std::string &contents);
    static void remove(const std::filesystem::path &file);
    static void protectDirectory(const std::filesystem::path &directory);
};
} // namespace jade::native
