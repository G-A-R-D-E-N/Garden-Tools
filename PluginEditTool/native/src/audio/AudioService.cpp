#include <fo4ide/audio/AudioService.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fo4ide::audio {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool supportedBatchExtension(const std::filesystem::path& path) {
    static const std::set<std::string> extensions{
        ".wav", ".mp3", ".flac", ".ogg", ".oga", ".m4a", ".wma", ".aif", ".aiff", ".mp4", ".avi"
    };
    return extensions.contains(lowerAscii(path.extension().string()));
}

std::filesystem::path tempPath(std::string_view extension) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("FO4RE_Audio_" + std::to_string(ticks) + std::string(extension));
}

class Cleanup {
public:
    explicit Cleanup(std::filesystem::path path = {}) : path_(std::move(path)) {}
    Cleanup(const Cleanup&) = delete;
    Cleanup& operator=(const Cleanup&) = delete;
    Cleanup(Cleanup&&) = default;
    Cleanup& operator=(Cleanup&&) = default;
    ~Cleanup() {
        std::error_code ec;
        if (!path_.empty()) std::filesystem::remove(path_, ec);
    }

    void release() noexcept {
        path_.clear();
    }

private:
    std::filesystem::path path_;
};

Result<void> ensureOutputDirectory(const std::filesystem::path& path) {
    try {
        auto parent = std::filesystem::absolute(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"AudioOutputFailed", ex.what()});
    }
}

std::uint16_t readLe16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t readLe32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0]) |
           static_cast<std::uint32_t>(data[1]) << 8U |
           static_cast<std::uint32_t>(data[2]) << 16U |
           static_cast<std::uint32_t>(data[3]) << 24U;
}

std::string firstLine(std::string value) {
    const auto split = value.find_first_of("\r\n");
    if (split != std::string::npos) value.resize(split);
    return value;
}

bool sameLocation(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    return std::filesystem::weakly_canonical(a, ec) == std::filesystem::weakly_canonical(b, ec);
}

}

AudioService::AudioService(process::Runner& runner, const tools::Locator& tools)
    : runner_(runner), tools_(tools) {}

Result<process::Output> AudioService::runTool(const std::filesystem::path& executable,
                                              std::vector<std::string> arguments,
                                              std::chrono::milliseconds timeout) const {
    if (executable.empty() || !std::filesystem::is_regular_file(executable)) {
        return std::unexpected(Error{"AudioToolNotFound", "Required audio tool was not found"});
    }
    process::Request request;
    request.executable = executable;
    request.arguments = std::move(arguments);
    request.workingDirectory = executable.parent_path();
    request.timeout = timeout;
    return runner_.run(request);
}

bool AudioService::isPcmWav(const std::filesystem::path& path) const {
    if (lowerAscii(path.extension().string()) != ".wav") return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    std::array<unsigned char, 12> header{};
    stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (stream.gcount() != static_cast<std::streamsize>(header.size())) return false;
    if (readLe32(header.data()) != 0x46464952U || readLe32(header.data() + 8) != 0x45564157U) return false;

    std::array<unsigned char, 8> chunk{};
    while (stream.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()))) {
        const auto id = readLe32(chunk.data());
        const auto size = readLe32(chunk.data() + 4);
        if (id == 0x20746D66U) {
            std::array<unsigned char, 2> format{};
            stream.read(reinterpret_cast<char*>(format.data()), static_cast<std::streamsize>(format.size()));
            if (stream.gcount() != static_cast<std::streamsize>(format.size())) return false;
            const auto tag = readLe16(format.data());
            return tag == 1U || tag == 0xFFFEU;
        }
        stream.seekg(static_cast<std::streamoff>(size + (size % 2U)), std::ios::cur);
        if (!stream) return false;
    }
    return false;
}

Result<std::filesystem::path> AudioService::normalizeToPcmWav(const std::filesystem::path& source) const {
    const auto ffmpeg = tools_.resolve(tools::Tool::Ffmpeg);
    if (ffmpeg.empty()) {
        return std::unexpected(Error{"FfmpegNotFound", std::string(tools_.describe(tools::Tool::Ffmpeg))});
    }
    const auto output = tempPath(".wav");
    auto run = runTool(ffmpeg,
        {"-hide_banner", "-loglevel", "warning", "-y", "-i", source.string(), "-vn", "-acodec", "pcm_s16le", output.string()},
        std::chrono::seconds(60));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FfmpegTimeout", "ffmpeg timed out after 60 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(output)) {
        return std::unexpected(Error{"FfmpegFailed", run->combined()});
    }
    return output;
}

Result<std::string> AudioService::convertOneToXwm(const std::filesystem::path& source,
                                                  const std::filesystem::path& output,
                                                  std::optional<int> bitrateBps) const {
    if (!std::filesystem::is_regular_file(source)) {
        return std::unexpected(Error{"AudioSourceNotFound", "Source not found: " + source.string()});
    }
    const auto encoder = tools_.resolve(tools::Tool::XwmaEncode);
    if (encoder.empty()) {
        return std::unexpected(Error{"XwmaEncodeNotFound", std::string(tools_.describe(tools::Tool::XwmaEncode))});
    }

    auto outPath = output;
    if (outPath.empty()) {
        outPath = source;
        outPath.replace_extension(".xwm");
    }
    if (sameLocation(outPath, source)) {
        return std::unexpected(Error{"AudioOutputMatchesSource", "Refusing to overwrite the source audio file: " + source.string()});
    }
    if (auto ready = ensureOutputDirectory(outPath); !ready) return std::unexpected(ready.error());

    std::filesystem::path wavInput = source;
    Cleanup temporary;
    if (!isPcmWav(source)) {
        auto normalized = normalizeToPcmWav(source);
        if (!normalized) return std::unexpected(normalized.error());
        wavInput = *normalized;
        temporary = Cleanup(wavInput);
    }

    std::vector<std::string> arguments;
    if (bitrateBps && *bitrateBps > 0) {
        arguments.emplace_back("-b");
        arguments.push_back(std::to_string(*bitrateBps));
    }
    arguments.push_back(wavInput.string());
    arguments.push_back(outPath.string());

    auto run = runTool(encoder, std::move(arguments), std::chrono::seconds(60));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"XwmaEncodeTimeout", "xWMAEncode timed out after 60 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(outPath)) {
        return std::unexpected(Error{"XwmaEncodeFailed", "xWMAEncode failed: " + run->combined()});
    }

    auto message = "RESULT: success (output -> " + outPath.string() + ")";
    const auto combined = run->combined();
    if (!combined.empty()) message += "\n\n" + combined;
    return message;
}

Result<std::string> AudioService::convertToXwm(const std::filesystem::path& source,
                                               const std::filesystem::path& output,
                                               std::optional<int> bitrateBps) const {
    if (source.empty()) return std::unexpected(Error{"InvalidAudioSource", "Provide an audio/video source"});
    if (std::filesystem::is_regular_file(source)) return convertOneToXwm(source, output, bitrateBps);
    if (!std::filesystem::is_directory(source)) {
        return std::unexpected(Error{"AudioSourceNotFound", "Source not found: " + source.string()});
    }

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && supportedBatchExtension(it->path())) files.push_back(it->path());
    }
    if (files.empty()) return std::unexpected(Error{"AudioBatchEmpty", "No supported audio/video files were found"});

    std::size_t succeeded = 0;
    std::vector<std::string> failures;
    for (const auto& file : files) {
        const auto relative = std::filesystem::relative(file, source, ec);
        auto target = output.empty() ? file : output / relative;
        target.replace_extension(".xwm");
        auto converted = convertOneToXwm(file, target, bitrateBps);
        if (converted) {
            ++succeeded;
        } else if (failures.size() < 20) {
            failures.push_back(relative.string() + ": " + firstLine(converted.error().message));
        }
    }

    std::ostringstream message;
    message << "RESULT: " << succeeded << '/' << files.size() << " converted";
    if (!output.empty()) message << " (output -> " << output.string() << ')';
    else message << " (in place, alongside each source)";
    message << '.';
    if (!failures.empty()) {
        message << "\n" << failures.size() << " FAILED:";
        for (const auto& failure : failures) message << "\n  " << failure;
    }
    return message.str();
}

Result<std::string> AudioService::convertFromXwm(const std::filesystem::path& source,
                                                 const std::filesystem::path& output,
                                                 std::string targetExtension) const {
    if (!std::filesystem::is_regular_file(source)) {
        return std::unexpected(Error{"AudioSourceNotFound", "Source not found: " + source.string()});
    }
    const auto decoder = tools_.resolve(tools::Tool::XwmaEncode);
    if (decoder.empty()) {
        return std::unexpected(Error{"XwmaEncodeNotFound", std::string(tools_.describe(tools::Tool::XwmaEncode))});
    }

    while (!targetExtension.empty() && targetExtension.front() == '.') targetExtension.erase(targetExtension.begin());
    targetExtension = lowerAscii(std::move(targetExtension));
    if (targetExtension.empty()) targetExtension = "wav";

    auto finalOutput = output;
    if (finalOutput.empty()) {
        finalOutput = source;
        finalOutput.replace_extension("." + targetExtension);
    }
    if (sameLocation(finalOutput, source)) {
        return std::unexpected(Error{"AudioOutputMatchesSource", "Refusing to overwrite the source audio file: " + source.string()});
    }
    if (auto ready = ensureOutputDirectory(finalOutput); !ready) return std::unexpected(ready.error());

    const bool wantsWav = targetExtension == "wav";
    const auto wavOutput = wantsWav ? finalOutput : tempPath(".wav");
    Cleanup wavCleanup(wantsWav ? std::filesystem::path{} : wavOutput);

    auto decode = runTool(decoder, {source.string(), wavOutput.string()}, std::chrono::seconds(60));
    if (!decode) return std::unexpected(decode.error());
    if (decode->timedOut) return std::unexpected(Error{"XwmaDecodeTimeout", "xWMAEncode timed out after 60 seconds"});
    if (decode->exitCode != 0 || !std::filesystem::is_regular_file(wavOutput)) {
        return std::unexpected(Error{"XwmaDecodeFailed", "xWMAEncode failed: " + decode->combined()});
    }
    if (wantsWav) return "RESULT: success (output -> " + finalOutput.string() + ')';

    const auto ffmpeg = tools_.resolve(tools::Tool::Ffmpeg);
    if (ffmpeg.empty()) return std::unexpected(Error{"FfmpegNotFound", std::string(tools_.describe(tools::Tool::Ffmpeg))});
    auto convert = runTool(ffmpeg,
        {"-hide_banner", "-loglevel", "warning", "-y", "-i", wavOutput.string(), finalOutput.string()},
        std::chrono::seconds(60));
    if (!convert) return std::unexpected(convert.error());
    if (convert->timedOut) return std::unexpected(Error{"FfmpegTimeout", "ffmpeg timed out after 60 seconds"});
    if (convert->exitCode != 0 || !std::filesystem::is_regular_file(finalOutput)) {
        return std::unexpected(Error{"FfmpegFailed", convert->combined()});
    }
    return "RESULT: success (output -> " + finalOutput.string() + ')';
}

Result<std::string> AudioService::makeFuz(const std::filesystem::path& audioSource,
                                          const std::filesystem::path& lipPath,
                                          const std::filesystem::path& fuzOutput,
                                          bool noLip) const {
    if (!std::filesystem::is_regular_file(audioSource)) {
        return std::unexpected(Error{"AudioSourceNotFound", "Source not found: " + audioSource.string()});
    }
    if (fuzOutput.empty()) return std::unexpected(Error{"InvalidFuzOutput", "FUZ output path is empty"});
    const auto encoder = tools_.resolve(tools::Tool::BmlFuzEncode);
    if (encoder.empty()) return std::unexpected(Error{"FuzEncodeNotFound", std::string(tools_.describe(tools::Tool::BmlFuzEncode))});

    auto xwm = audioSource;
    Cleanup xwmCleanup;
    if (lowerAscii(audioSource.extension().string()) != ".xwm") {
        xwm = tempPath(".xwm");
        auto converted = convertOneToXwm(audioSource, xwm, {});
        if (!converted) return std::unexpected(converted.error());
        xwmCleanup = Cleanup(xwm);
    }

    auto lip = lipPath;
    if (lip.empty()) {
        lip = audioSource;
        lip.replace_extension(".lip");
    }
    const bool includeLip = !noLip && std::filesystem::is_regular_file(lip);
    if (auto ready = ensureOutputDirectory(fuzOutput); !ready) return std::unexpected(ready.error());

    std::vector<std::string> arguments;
    if (noLip) arguments.emplace_back("-nolip");
    arguments.push_back(fuzOutput.string());
    arguments.push_back(xwm.string());
    if (includeLip) arguments.push_back(lip.string());

    auto run = runTool(encoder, std::move(arguments), std::chrono::seconds(30));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FuzEncodeTimeout", "BmlFuzEncode timed out after 30 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(fuzOutput)) {
        return std::unexpected(Error{"FuzEncodeFailed", run->combined()});
    }
    return "RESULT: success (output -> " + fuzOutput.string() + ", lip " + (includeLip ? "included" : "not included") + ')';
}

Result<std::string> AudioService::extractFuz(const std::filesystem::path& fuzPath,
                                             const std::filesystem::path& xwmOutput,
                                             const std::filesystem::path& lipOutput,
                                             bool alsoWav) const {
    if (!std::filesystem::is_regular_file(fuzPath)) {
        return std::unexpected(Error{"FuzSourceNotFound", "Source not found: " + fuzPath.string()});
    }
    const auto decoder = tools_.resolve(tools::Tool::BmlFuzDecode);
    if (decoder.empty()) return std::unexpected(Error{"FuzDecodeNotFound", std::string(tools_.describe(tools::Tool::BmlFuzDecode))});

    auto xwm = xwmOutput;
    if (xwm.empty()) {
        xwm = fuzPath;
        xwm.replace_extension(".xwm");
    }
    auto lip = lipOutput;
    if (lip.empty()) {
        lip = fuzPath;
        lip.replace_extension(".lip");
    }
    if (auto ready = ensureOutputDirectory(xwm); !ready) return std::unexpected(ready.error());

    auto run = runTool(decoder, {fuzPath.string(), xwm.string(), lip.string()}, std::chrono::seconds(30));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FuzDecodeTimeout", "BmlFuzDecode timed out after 30 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(xwm)) {
        return std::unexpected(Error{"FuzDecodeFailed", run->combined()});
    }

    std::string message = "RESULT: success (xwm -> " + xwm.string();
    if (std::filesystem::is_regular_file(lip)) message += ", lip -> " + lip.string() + ')';
    else message += ", no lip in this fuz)";

    if (alsoWav) {
        auto wav = xwm;
        wav.replace_extension(".wav");
        auto decoded = convertFromXwm(xwm, wav, "wav");
        if (decoded) message += "\nwav -> " + wav.string();
        else message += "\nwav decode failed: " + decoded.error().message;
    }
    return message;
}

}
