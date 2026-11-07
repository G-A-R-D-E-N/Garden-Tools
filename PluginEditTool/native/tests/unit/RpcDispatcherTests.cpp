#include <memory>
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace {

class EchoTarget final : public fo4ide::rpc::Target {
public:
    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                           const nlohmann::json& args) override {
        if (method == "Echo") return args.at(0);
        if (method == "Fail") return std::unexpected(fo4ide::Error{"ExpectedFailure", "boom"});
        throw std::runtime_error("unexpected exception");
    }
};

}

TEST_CASE("RPC dispatcher preserves the current success envelope") {
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("test", std::make_shared<EchoTarget>()).has_value());

    const auto response = dispatcher.invokeJson({
        {"target", "test"},
        {"method", "Echo"},
        {"args", nlohmann::json::array({"hello"})}
    });

    CHECK(response.toJson() == nlohmann::json{{"ok", true}, {"value", "hello"}});
}

TEST_CASE("RPC dispatcher returns structured target errors") {
    fo4ide::rpc::Dispatcher dispatcher;
    const auto response = dispatcher.invoke({"missing", "Echo", nlohmann::json::array()});

    CHECK_FALSE(response.ok);
    CHECK(response.type == "UnknownTarget");
    CHECK(response.toJson().at("ok") == false);
}

TEST_CASE("RPC dispatcher preserves service errors") {
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("test", std::make_shared<EchoTarget>()).has_value());

    const auto response = dispatcher.invoke({"test", "Fail", nlohmann::json::array()});
    CHECK_FALSE(response.ok);
    CHECK(response.type == "ExpectedFailure");
    CHECK(response.error == "boom");
}

TEST_CASE("RPC dispatcher rejects malformed requests") {
    fo4ide::rpc::Dispatcher dispatcher;
    auto response = dispatcher.invokeJson({{"target", "test"}, {"method", "Echo"}, {"args", 7}});
    CHECK_FALSE(response.ok);
    CHECK(response.type == "InvalidRequest");
}

TEST_CASE("RPC target names are stable and sorted") {
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("zeta", std::make_shared<EchoTarget>()).has_value());
    REQUIRE(dispatcher.registerTarget("alpha", std::make_shared<EchoTarget>()).has_value());
    CHECK(dispatcher.targetNames() == nlohmann::json::array({"alpha", "zeta"}));
}
