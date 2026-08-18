#include "test_data_dir.h"

#include <chrono>
#include <system_error>

namespace modelharbor::test_support {

TestDataDir::TestDataDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("modelharbor-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
}

TestDataDir::~TestDataDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
}

} // namespace modelharbor::test_support
