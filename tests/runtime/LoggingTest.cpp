#include "runtime/Logging.hpp"

#include "config/Constants.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <string>

namespace {

// Mirrors tests/runtime/test_logging.py: configure_logging sets the level and is
// idempotent (no duplicate sinks on repeated calls). spdlog's logger registry is
// global, so each case drops the application logger to stay isolated.
class LoggingTest : public ::testing::Test {
public:
    void SetUp() override { spdlog::drop(std::string{voidclip::config::kAppId}); }
    void TearDown() override { spdlog::drop(std::string{voidclip::config::kAppId}); }
};

TEST_F(LoggingTest, SetsLevel) {
    voidclip::runtime::configure_logging(spdlog::level::debug);
    const auto logger = spdlog::get(std::string{voidclip::config::kAppId});
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::debug);
}

TEST_F(LoggingTest, IsIdempotent) {
    voidclip::runtime::configure_logging();
    voidclip::runtime::configure_logging();
    const auto logger = spdlog::get(std::string{voidclip::config::kAppId});
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 1U);
}

} // namespace
