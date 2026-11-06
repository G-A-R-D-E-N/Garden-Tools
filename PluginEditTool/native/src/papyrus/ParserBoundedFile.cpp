#include <fo4ide/papyrus/Parser.hpp>

#include <filesystem>
#include <fstream>

namespace fo4ide::papyrus {

std::optional<Script> Parser::parseFile(const std::filesystem::path& path, std::size_t maxBytes,
                                        std::size_t* bytesRead) {
    std::error_code error;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    if (maxBytes && size > maxBytes) return std::nullopt;
    std::string text;
    text.resize(static_cast<std::size_t>(size));
    stream.read(text.data(), static_cast<std::streamsize>(size));
    if (!stream && size != 0) return std::nullopt;
    if (maxBytes && stream.peek() != std::char_traits<char>::eof()) return std::nullopt;
    if (bytesRead) *bytesRead = text.size();
    return parse(text, path.string());
}


}
