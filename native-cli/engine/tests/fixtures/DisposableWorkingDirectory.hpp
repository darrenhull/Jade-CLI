#pragma once
#include "ProjectModel.hpp"
#include <chrono>
#include <iostream>
#include <thread>
namespace jade::native {
/** Declare before owned native sessions, so they exit before fixture deletion. */
class DisposableWorkingDirectory {
    std::filesystem::path directory_;

  public:
    explicit DisposableWorkingDirectory(const std::string &prefix)
        : directory_(std::filesystem::temp_directory_path() / (prefix + uniqueId())) {
        std::filesystem::create_directories(directory_);
    }
    const std::filesystem::path &path() const {
        return directory_;
    }
    void remove() {
        const auto temp = std::filesystem::temp_directory_path();
        if (pathKey(directory_) == pathKey(temp) || !contains(temp, directory_))
            throw std::runtime_error("Disposable Working fixture must stay inside TEMP.");
        std::error_code error;
        for (int attempt = 0; attempt < 20; ++attempt) {
            std::filesystem::remove_all(directory_, error);
            if (!error)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::filesystem::filesystem_error("Cannot remove disposable Working fixture",
                                                directory_, error);
    }
    ~DisposableWorkingDirectory() {
        try {
            remove();
        } catch (const std::exception &error) {
            std::cerr << "Fixture cleanup failed: " << error.what() << '\n';
        }
    }
};
} // namespace jade::native
