/**
 * @file    test_proxy_result.cpp
 * @brief   Unit tests for Result<T, ProxyError>.
 */

#include "../../base/proxy_result.h"
#include <gtest/gtest.h>
#include <string>

using namespace middleware;

TEST(ProxyResultVoid, OkIsOk) {
    auto r = Result<void>::ok();
    EXPECT_TRUE(r.isOk());
    EXPECT_FALSE(r.isErr());
    EXPECT_EQ(ProxyError::None, r.error());
}

TEST(ProxyResultVoid, ErrIsErr) {
    auto r = Result<void>::err(ProxyError::Timeout, "timed out");
    EXPECT_FALSE(r.isOk());
    EXPECT_TRUE(r.isErr());
    EXPECT_EQ(ProxyError::Timeout, r.error());
    EXPECT_STREQ("timed out", r.message());
}

TEST(ProxyResultInt, OkValue) {
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.isOk());
    EXPECT_EQ(42, r.value());
}

TEST(ProxyResultInt, ErrValue) {
    auto r = Result<int>::err(ProxyError::AuthFailed);
    EXPECT_TRUE(r.isErr());
    EXPECT_EQ(ProxyError::AuthFailed, r.error());
}

TEST(ProxyResultInt, AndThenPropagatesOk) {
    auto r = Result<int>::ok(10)
               .andThen([](int v) { return Result<int>::ok(v * 2); });
    EXPECT_TRUE(r.isOk());
    EXPECT_EQ(20, r.value());
}

TEST(ProxyResultInt, AndThenShortCircuitsOnErr) {
    bool called = false;
    auto r = Result<int>::err(ProxyError::IoError)
               .andThen([&](int) { called = true; return Result<int>::ok(99); });
    EXPECT_FALSE(called);
    EXPECT_TRUE(r.isErr());
    EXPECT_EQ(ProxyError::IoError, r.error());
}

TEST(ProxyResultInt, OrElseRecovery) {
    auto r = Result<int>::err(ProxyError::Timeout)
               .orElse([](ProxyError) { return Result<int>::ok(-1); });
    EXPECT_TRUE(r.isOk());
    EXPECT_EQ(-1, r.value());
}

TEST(ProxyResultInt, OrElseDoesNotFireOnOk) {
    bool called = false;
    auto r = Result<int>::ok(7)
               .orElse([&](ProxyError) { called = true; return Result<int>::ok(0); });
    EXPECT_FALSE(called);
    EXPECT_EQ(7, r.value());
}

TEST(ProxyResultString, MoveSemantics) {
    auto r = Result<std::string>::ok(std::string(64, 'x'));
    EXPECT_TRUE(r.isOk());
    EXPECT_EQ(64u, r.value().size());
}
