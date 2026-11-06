#pragma once
#include "NativeProcess.hpp"
namespace jade::native {
class PrivateSecurity {
  public:
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{};
    PrivateSecurity();
    ~PrivateSecurity();
    PrivateSecurity(const PrivateSecurity &) = delete;
    PrivateSecurity &operator=(const PrivateSecurity &) = delete;
};
} // namespace jade::native
