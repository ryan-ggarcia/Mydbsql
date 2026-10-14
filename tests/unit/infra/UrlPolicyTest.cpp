#include "infra/platform/IPlatform.hpp"

#include <gtest/gtest.h>

#include <string_view>

namespace mydb::infra::platform {
namespace {

TEST(UrlPolicyTest, AcceptsWebAndMailSchemesCaseInsensitive) {
    EXPECT_TRUE(isAllowedExternalUrl("https://mariadb.com/kb/"));
    EXPECT_TRUE(isAllowedExternalUrl("http://localhost:8080"));
    EXPECT_TRUE(isAllowedExternalUrl("HTTPS://EXAMPLE.ORG"));
    EXPECT_TRUE(isAllowedExternalUrl("mailto:dev@example.org"));
}

TEST(UrlPolicyTest, RejectsLocalFilesProgramsAndOtherSchemes) {
    EXPECT_FALSE(isAllowedExternalUrl(""));
    EXPECT_FALSE(isAllowedExternalUrl("https://"));
    EXPECT_FALSE(isAllowedExternalUrl("file:///C:/Windows/System32/calc.exe"));
    EXPECT_FALSE(isAllowedExternalUrl("C:\\Windows\\System32\\calc.exe"));
    EXPECT_FALSE(isAllowedExternalUrl("/usr/bin/xterm"));
    EXPECT_FALSE(isAllowedExternalUrl("javascript:alert(1)"));
    EXPECT_FALSE(isAllowedExternalUrl("ftp://example.org"));
    EXPECT_FALSE(isAllowedExternalUrl(" https://example.org"));
}

TEST(UrlPolicyTest, RejectsControlCharacters) {
    EXPECT_FALSE(isAllowedExternalUrl("https://example.org/\nrm -rf"));
    EXPECT_FALSE(isAllowedExternalUrl(std::string_view("https://example.org/\0x", 21)));
}

} // namespace
} // namespace mydb::infra::platform
