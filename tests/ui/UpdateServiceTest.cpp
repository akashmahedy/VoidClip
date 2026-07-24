#include "ui/UpdateService.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using voidclip::ui::UpdateCheckState;

[[nodiscard]] std::string
release_json(std::string tag = "v0.3.3",
             std::string package_url = "https://github.com/akashmahedy/VoidClip/releases/"
                                       "download/v0.3.3/voidclip_0.3.3_amd64.deb",
             std::string checksum_url = "https://github.com/akashmahedy/VoidClip/releases/"
                                        "download/v0.3.3/SHA256SUMS-x86_64") {
    return R"({"draft":false,"prerelease":false,"tag_name":")" + tag +
           R"(","assets":[{"name":"voidclip_0.3.3_amd64.deb","browser_download_url":")" +
           package_url + R"("},{"name":"SHA256SUMS-x86_64","browser_download_url":")" +
           checksum_url + R"("}]})";
}

TEST(UpdateServiceTest, ComparesNumericVersions) {
    EXPECT_TRUE(voidclip::ui::is_newer_version("0.3.3", "0.3.2"));
    EXPECT_TRUE(voidclip::ui::is_newer_version("v0.3.10", "0.3.9"));
    EXPECT_FALSE(voidclip::ui::is_newer_version("0.3.2", "0.3.2"));
    EXPECT_FALSE(voidclip::ui::is_newer_version("0.3.1", "0.3.2"));
    EXPECT_FALSE(voidclip::ui::is_newer_version("0.3.3-beta", "0.3.2"));
    EXPECT_FALSE(voidclip::ui::is_newer_version("not-a-version", "0.3.2"));
}

TEST(UpdateServiceTest, SelectsExactDebAndChecksumAssets) {
    const auto result =
        voidclip::ui::check_release_json(release_json(), "0.3.2", "x86_64", "amd64");

    EXPECT_EQ(result.state, UpdateCheckState::Available);
    EXPECT_EQ(result.release.version, "0.3.3");
    EXPECT_EQ(result.release.tag, "v0.3.3");
    EXPECT_EQ(result.release.package_name, "voidclip_0.3.3_amd64.deb");
    EXPECT_TRUE(result.release.package_url.ends_with("/voidclip_0.3.3_amd64.deb"));
    EXPECT_TRUE(result.release.checksum_url.ends_with("/SHA256SUMS-x86_64"));
}

TEST(UpdateServiceTest, ReportsCurrentOrOlderReleaseAsUpToDate) {
    EXPECT_EQ(
        voidclip::ui::check_release_json(release_json("v0.3.3"), "0.3.3", "x86_64", "amd64").state,
        UpdateCheckState::UpToDate);
    EXPECT_EQ(
        voidclip::ui::check_release_json(release_json("v0.3.3"), "0.3.4", "x86_64", "amd64").state,
        UpdateCheckState::UpToDate);
}

TEST(UpdateServiceTest, RejectsMissingOrUntrustedAssets) {
    const auto missing = voidclip::ui::check_release_json(
        R"({"draft":false,"prerelease":false,"tag_name":"v0.3.3","assets":[]})", "0.3.2", "x86_64",
        "amd64");
    EXPECT_EQ(missing.state, UpdateCheckState::Error);

    const auto untrusted = voidclip::ui::check_release_json(
        release_json("v0.3.3", "https://example.com/voidclip_0.3.3_amd64.deb"), "0.3.2", "x86_64",
        "amd64");
    EXPECT_EQ(untrusted.state, UpdateCheckState::Error);
}

TEST(UpdateServiceTest, RejectsDraftPrereleaseAndMalformedMetadata) {
    EXPECT_EQ(voidclip::ui::check_release_json("not json", "0.3.2", "x86_64", "amd64").state,
              UpdateCheckState::Error);
    EXPECT_EQ(
        voidclip::ui::check_release_json(R"({"draft":true,"prerelease":false,"tag_name":"v0.3.3"})",
                                         "0.3.2", "x86_64", "amd64")
            .state,
        UpdateCheckState::Error);
    EXPECT_EQ(
        voidclip::ui::check_release_json(R"({"draft":false,"prerelease":true,"tag_name":"v0.3.3"})",
                                         "0.3.2", "x86_64", "amd64")
            .state,
        UpdateCheckState::Error);
}

TEST(UpdateServiceTest, ReadsOnlyExactChecksumEntry) {
    constexpr auto hash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string manifest = std::string{hash} + "  voidclip_0.3.3_amd64.deb.old\n" +
                                 std::string{hash} + " *voidclip_0.3.3_amd64.deb\n";

    EXPECT_EQ(voidclip::ui::checksum_for_asset(manifest, "voidclip_0.3.3_amd64.deb"), hash);
    EXPECT_TRUE(voidclip::ui::checksum_for_asset(manifest, "voidclip_0.3.3_arm64.deb").empty());
}

} // namespace
