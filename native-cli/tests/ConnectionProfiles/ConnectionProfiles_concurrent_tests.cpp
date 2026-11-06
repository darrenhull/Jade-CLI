#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    ConnectionProfiles profiles(root / "connections.json");
    std::atomic_bool start = false;
    std::atomic_int failures = 0;
    std::vector<std::thread> clients;
    for (int i = 0; i < 8; ++i)
        clients.emplace_back([&, i] {
            while (!start.load())
                std::this_thread::yield();
            try {
                profiles.save(
                    "profile" + std::to_string(i),
                    {{"installPath", utf8((root / ("Jade" + std::to_string(i))).wstring())}});
            } catch (...) {
                ++failures;
            }
        });
    start = true;
    for (auto &client : clients)
        client.join();
    assert(failures == 0);
    assert(profiles.names().size() == 8);
    std::filesystem::remove_all(root);
    std::cout << "ConnectionProfiles concurrent registrations retain every profile\n";
}
