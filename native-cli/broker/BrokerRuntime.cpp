#include "BrokerRuntime.hpp"
#include "BrokerClient.hpp"
#include "BrokerServer.hpp"
#include "BrokerService.hpp"
#include "BrokerHelperRelay.hpp"
namespace jade::native {
nlohmann::json BrokerRuntime::request(const BrokerOptions &options, const nlohmann::json &request,
                                      Output output) {
    return broker::BrokerClient::request(options, request, std::move(output));
}
int BrokerRuntime::service(const BrokerOptions &options, std::istream &input,
                           std::ostream &output) {
    return broker::BrokerService::run(options, input, output);
}
int BrokerRuntime::relayHelper(const BrokerOptions &options, const std::string &kind,
                               const std::filesystem::path &session, std::istream &input,
                               std::ostream &output, std::ostream &errors) {
    return broker::BrokerHelperRelay::run(options, kind, session, input, output, errors);
}
int BrokerRuntime::runServer(const BrokerOptions &options, Executor execute, Release release) {
    return broker::BrokerServer::run(options, std::move(execute), std::move(release));
}
} // namespace jade::native
