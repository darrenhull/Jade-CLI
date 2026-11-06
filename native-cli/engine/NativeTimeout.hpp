#pragma once
#include <stdexcept>
namespace jade::native {
class NativeTimeout : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
} // namespace jade::native
