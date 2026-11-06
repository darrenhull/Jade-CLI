#pragma once
#include "BrokerOptions.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <istream>
#include <json.hpp>
#include <ostream>
#include <string>

namespace jade::native {

class BrokerRuntime {
  public:
    using Output = std::function<void(const std::string &)>;
    using Executor =
        std::function<nlohmann::json(const nlohmann::json &, Output, std::atomic_bool *)>;
    using Release = std::function<void()>;
    static nlohmann::json request(const BrokerOptions &, const nlohmann::json &, Output = {});
    static int service(const BrokerOptions &, std::istream &, std::ostream &);
    static int relayHelper(const BrokerOptions &, const std::string &kind,
                           const std::filesystem::path &session, std::istream &, std::ostream &,
                           std::ostream &);
    static int runServer(const BrokerOptions &, Executor, Release);
};
} // namespace jade::native
