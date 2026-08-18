#pragma once

#include <filesystem>

namespace modelharbor::test_support {

class TestDataDir final {
  public:
    TestDataDir();
    ~TestDataDir();

    TestDataDir(const TestDataDir&) = delete;
    TestDataDir& operator=(const TestDataDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

} // namespace modelharbor::test_support
