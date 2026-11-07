#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

namespace vertex_color_detail {

constexpr std::size_t k_block = 1;
constexpr std::size_t k_kind = 2;
constexpr std::size_t k_channel = 3;
constexpr std::size_t k_axis = 4;
constexpr std::size_t k_value = 5;
constexpr std::size_t k_color = 6;
constexpr std::size_t k_end_color = 7;
constexpr std::size_t k_vertices = 8;

inline std::unexpected<Error> failure(const std::string& message) {
    return std::unexpected(Error{"VertexColorFailed", message});
}

inline const nlohmann::json* positional(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || args[index].is_null()) return nullptr;
    return &args[index];
}

inline Result<std::uint32_t> blockArgument(const nlohmann::json* value) {
    constexpr auto limit = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    if (value == nullptr || !value->is_number_integer() || value->get<std::int64_t>() < 0 ||
        value->get<std::int64_t>() > limit) {
        return failure("block must be a shape block id");
    }
    return static_cast<std::uint32_t>(value->get<std::int64_t>());
}

inline Result<fo4::nif::VertexColorKind> kindArgument(const nlohmann::json* value) {
    if (value != nullptr && value->is_string()) {
        const auto text = value->get<std::string>();
        if (text == "fill") return fo4::nif::VertexColorKind::fill;
        if (text == "channel") return fo4::nif::VertexColorKind::channel;
        if (text == "gradient") return fo4::nif::VertexColorKind::gradient;
        if (text == "enable") return fo4::nif::VertexColorKind::enable;
    }
    return failure("kind must be fill, channel, gradient or enable");
}

inline Result<fo4::nif::ColorChannel> channelArgument(const nlohmann::json* value) {
    if (value == nullptr) return fo4::nif::ColorChannel::red;
    if (value->is_string()) {
        const auto text = value->get<std::string>();
        if (text == "red") return fo4::nif::ColorChannel::red;
        if (text == "green") return fo4::nif::ColorChannel::green;
        if (text == "blue") return fo4::nif::ColorChannel::blue;
        if (text == "alpha") return fo4::nif::ColorChannel::alpha;
    }
    return failure("channel must be red, green, blue or alpha");
}

inline Result<fo4::nif::GradientAxis> axisArgument(const nlohmann::json* value) {
    if (value == nullptr) return fo4::nif::GradientAxis::x;
    if (value->is_string()) {
        const auto text = value->get<std::string>();
        if (text == "x") return fo4::nif::GradientAxis::x;
        if (text == "y") return fo4::nif::GradientAxis::y;
        if (text == "z") return fo4::nif::GradientAxis::z;
    }
    return failure("axis must be x, y or z");
}

inline Result<float> numberArgument(const nlohmann::json* value, float fallback, const std::string& name) {
    if (value == nullptr) return fallback;
    if (!value->is_number()) return failure(name + " must be a number");
    return value->get<float>();
}

inline Result<std::array<float, 4>> rgbaArgument(const nlohmann::json* value, std::array<float, 4> fallback,
                                                 const std::string& name) {
    if (value == nullptr) return fallback;
    if (!value->is_array() || value->size() != 4) return failure(name + " must be four numbers, RGBA");
    std::array<float, 4> out{};
    for (std::size_t c = 0; c < 4; ++c) {
        if (!(*value)[c].is_number()) return failure(name + " must be four numbers, RGBA");
        out[c] = (*value)[c].get<float>();
    }
    return out;
}

inline Result<std::vector<std::uint32_t>> vertexListArgument(const nlohmann::json* value) {
    constexpr auto limit = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint32_t> out;
    if (value == nullptr) return out;
    if (!value->is_array()) return failure("vertices must be a list of vertex indices");
    for (const auto& item : *value) {
        if (!item.is_number_integer() || item.get<std::int64_t>() < 0 || item.get<std::int64_t>() > limit) {
            return failure("vertices must be a list of vertex indices");
        }
        out.push_back(static_cast<std::uint32_t>(item.get<std::int64_t>()));
    }
    return out;
}

inline nlohmann::json colorSummary(const std::vector<float>& colors) {
    constexpr std::array<const char*, 4> names{"red", "green", "blue", "alpha"};
    nlohmann::json out;
    out["present"] = !colors.empty();
    out["values"] = colors.size();
    out["channels"] = nlohmann::json::object();
    const std::size_t count = colors.size() / 4;
    for (std::size_t c = 0; c < names.size() && count > 0; ++c) {
        float low = colors[c];
        float high = colors[c];
        double sum = 0.0;
        for (std::size_t v = 0; v < count; ++v) {
            const float value = colors[4 * v + c];
            low = std::min(low, value);
            high = std::max(high, value);
            sum += value;
        }
        out["channels"][names[c]] = {{"min", low}, {"max", high}, {"mean", sum / static_cast<double>(count)}};
    }
    return out;
}

}  // namespace vertex_color_detail

inline nlohmann::json vertexColorSummaryJson(const std::vector<fo4::nif::Shape>& shapes) {
    nlohmann::json out;
    out["shapes"] = nlohmann::json::array();
    for (const auto& shape : shapes) {
        nlohmann::json item;
        item["block"] = shape.block;
        item["name"] = shape.name;
        item["vertices"] = shape.verts.size() / 3;
        item["color"] = vertex_color_detail::colorSummary(shape.colors);
        out["shapes"].push_back(std::move(item));
    }
    out["shape_count"] = shapes.size();
    return out;
}

inline Result<fo4::nif::VertexColorOp> parseVertexColorOp(const nlohmann::json& args) {
    using namespace vertex_color_detail;
    auto block = blockArgument(positional(args, k_block));
    if (!block) return std::unexpected(block.error());
    auto kind = kindArgument(positional(args, k_kind));
    if (!kind) return std::unexpected(kind.error());
    auto channel = channelArgument(positional(args, k_channel));
    if (!channel) return std::unexpected(channel.error());
    auto axis = axisArgument(positional(args, k_axis));
    if (!axis) return std::unexpected(axis.error());
    auto value = numberArgument(positional(args, k_value), 1.0f, "value");
    if (!value) return std::unexpected(value.error());
    auto color = rgbaArgument(positional(args, k_color), {1.0f, 1.0f, 1.0f, 1.0f}, "color");
    if (!color) return std::unexpected(color.error());
    auto end_color = rgbaArgument(positional(args, k_end_color), {0.0f, 0.0f, 0.0f, 1.0f}, "end_color");
    if (!end_color) return std::unexpected(end_color.error());
    auto vertices = vertexListArgument(positional(args, k_vertices));
    if (!vertices) return std::unexpected(vertices.error());
    fo4::nif::VertexColorOp op;
    op.kind = *kind;
    op.block = *block;
    op.channel = *channel;
    op.axis = *axis;
    op.value = *value;
    op.color = *color;
    op.end_color = *end_color;
    op.vertices = std::move(*vertices);
    return op;
}

}  // namespace fo4ide::assets
