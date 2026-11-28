#include <fo4ide/audio/AudioService.hpp>

#include <fo4ide/core/SameFile.hpp>

#include <fo4/io/atomic_write.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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

std::filesystem::path stagedOutput(const std::filesystem::path& destination) {
    auto extension = destination.extension().string();
    if (extension.empty()) extension = ".tmp";
    return tempPath(extension);
}

Result<void> publishStaged(const std::filesystem::path& staged,
                           const std::filesystem::path& destination) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(staged, ec);
    std::ifstream stream(staged, std::ios::binary);
    if (ec || !stream) {
        return std::unexpected(Error{"AudioPublishFailed",
            "Could not read the staged audio output: " + staged.string()});
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected(Error{"AudioPublishFailed",
            "Could not read the staged audio output: " + staged.string()});
    }
    try {
        fo4::io::write_file_atomic(destination, bytes);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"AudioPublishFailed", ex.what()});
    }
    std::filesystem::remove(staged, ec);
    return {};
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
    Cleanup cleanup(output);
    auto run = runTool(ffmpeg,
        {"-hide_banner", "-loglevel", "warning", "-y", "-i", source.string(), "-vn", "-acodec", "pcm_s16le", output.string()},
        std::chrono::seconds(60));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FfmpegTimeout", "ffmpeg timed out after 60 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(output)) {
        return std::unexpected(Error{"FfmpegFailed", run->combined()});
    }
    cleanup.release();
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
    if (core::sameFile(outPath, source)) {
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
    const auto staged = stagedOutput(outPath);
    Cleanup stagedCleanup(staged);
    arguments.push_back(wavInput.string());
    arguments.push_back(staged.string());

    auto run = runTool(encoder, std::move(arguments), std::chrono::seconds(60));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"XwmaEncodeTimeout", "xWMAEncode timed out after 60 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(staged)) {
        return std::unexpected(Error{"XwmaEncodeFailed", "xWMAEncode failed: " + run->combined()});
    }
    if (auto published = publishStaged(staged, outPath); !published) {
        return std::unexpected(published.error());
    }
    stagedCleanup.release();

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
    if (ec) {
        return std::unexpected(Error{"AudioBatchScanFailed",
            "Could not scan the whole source directory: " + ec.message()});
    }
    if (files.empty()) return std::unexpected(Error{"AudioBatchEmpty", "No supported audio/video files were found"});

    std::vector<std::filesystem::path> targets;
    targets.reserve(files.size());
    std::vector<std::filesystem::path> relatives;
    relatives.reserve(files.size());
    std::set<std::string> claimed;
    for (const auto& file : files) {
        const auto& relative = relatives.emplace_back(std::filesystem::relative(file, source, ec));
        auto target = output.empty() ? file : output / relative;
        target.replace_extension(".xwm");
        if (!claimed.insert(lowerAscii(target.lexically_normal().string())).second) {
            return std::unexpected(Error{"AudioOutputCollision",
                "Two sources map to the same output file: " + target.string()});
        }
        targets.push_back(std::move(target));
    }

    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::vector<std::string> failures;
    for (std::size_t index = 0; index < files.size(); ++index) {
        auto converted = convertOneToXwm(files[index], targets[index], bitrateBps);
        if (converted) {
            ++succeeded;
        } else {
            ++failed;
            if (failures.size() < 20) {
                failures.push_back(relatives[index].string() + ": " + firstLine(converted.error().message));
            }
        }
    }

    std::ostringstream message;
    message << "RESULT: " << succeeded << '/' << files.size() << " converted";
    if (!output.empty()) message << " (output -> " << output.string() << ')';
    else message << " (in place, alongside each source)";
    message << '.';
    if (failed != 0) {
        message << "\n" << failed << " FAILED";
        if (failed > failures.size()) message << " (first " << failures.size() << " shown)";
        message << ':';
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
    const auto plainExtensionCharacter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (targetExtension.size() > 8 || !std::all_of(targetExtension.begin(), targetExtension.end(), plainExtensionCharacter)) {
        return std::unexpected(Error{"AudioInvalidTargetExtension",
            "Invalid target extension '" + targetExtension + "': use 1 to 8 letters or digits, for example wav or mp3"});
    }

    auto finalOutput = output;
    if (finalOutput.empty()) {
        finalOutput = source;
        finalOutput.replace_extension("." + targetExtension);
    }
    if (core::sameFile(finalOutput, source)) {
        return std::unexpected(Error{"AudioOutputMatchesSource", "Refusing to overwrite the source audio file: " + source.string()});
    }
    if (!output.empty()) {
        auto requested = lowerAscii(output.extension().string());
        if (!requested.empty() && requested.front() == '.') requested.erase(requested.begin());
        if (!requested.empty() && requested != targetExtension) {
            return std::unexpected(Error{"AudioFormatMismatch",
                "Output extension ." + requested + " does not match the selected format " + targetExtension});
        }
    }
    if (auto ready = ensureOutputDirectory(finalOutput); !ready) return std::unexpected(ready.error());

    const bool wantsWav = targetExtension == "wav";
    const auto wavStage = tempPath(".wav");
    Cleanup wavCleanup(wavStage);

    auto decode = runTool(decoder, {source.string(), wavStage.string()}, std::chrono::seconds(60));
    if (!decode) return std::unexpected(decode.error());
    if (decode->timedOut) return std::unexpected(Error{"XwmaDecodeTimeout", "xWMAEncode timed out after 60 seconds"});
    if (decode->exitCode != 0 || !std::filesystem::is_regular_file(wavStage)) {
        return std::unexpected(Error{"XwmaDecodeFailed", "xWMAEncode failed: " + decode->combined()});
    }
    if (wantsWav) {
        if (auto published = publishStaged(wavStage, finalOutput); !published) {
            return std::unexpected(published.error());
        }
        wavCleanup.release();
        return "RESULT: success (output -> " + finalOutput.string() + ')';
    }

    const auto staged = stagedOutput(finalOutput);
    Cleanup stagedCleanup(staged);
    const auto ffmpeg = tools_.resolve(tools::Tool::Ffmpeg);
    if (ffmpeg.empty()) return std::unexpected(Error{"FfmpegNotFound", std::string(tools_.describe(tools::Tool::Ffmpeg))});
    auto convert = runTool(ffmpeg,
        {"-hide_banner", "-loglevel", "warning", "-y", "-i", wavStage.string(), staged.string()},
        std::chrono::seconds(60));
    if (!convert) return std::unexpected(convert.error());
    if (convert->timedOut) return std::unexpected(Error{"FfmpegTimeout", "ffmpeg timed out after 60 seconds"});
    if (convert->exitCode != 0 || !std::filesystem::is_regular_file(staged)) {
        return std::unexpected(Error{"FfmpegFailed", convert->combined()});
    }
    if (auto published = publishStaged(staged, finalOutput); !published) {
        return std::unexpected(published.error());
    }
    stagedCleanup.release();
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

    const bool explicitLip = !lipPath.empty();
    auto lip = lipPath;
    if (!explicitLip) {
        lip = audioSource;
        lip.replace_extension(".lip");
    } else if (!noLip && !std::filesystem::is_regular_file(lip)) {
        return std::unexpected(Error{"LipFileNotFound", "Lip file not found: " + lip.string()});
    }
    const bool includeLip = !noLip && std::filesystem::is_regular_file(lip);
    if (core::sameFile(fuzOutput, audioSource) || (includeLip && core::sameFile(fuzOutput, lip))) {
        return std::unexpected(Error{"AudioOutputMatchesSource",
            "Refusing to overwrite an input audio file: " + fuzOutput.string()});
    }
    if (includeLip && core::sameFile(lip, audioSource)) {
        return std::unexpected(Error{"AudioInputsCollide",
            "Lip file and audio source are the same file: " + lip.string()});
    }

    auto xwm = audioSource;
    Cleanup xwmCleanup;
    if (lowerAscii(audioSource.extension().string()) != ".xwm") {
        xwm = tempPath(".xwm");
        auto converted = convertOneToXwm(audioSource, xwm, {});
        if (!converted) return std::unexpected(converted.error());
        xwmCleanup = Cleanup(xwm);
    }
    if (auto ready = ensureOutputDirectory(fuzOutput); !ready) return std::unexpected(ready.error());

    const auto staged = stagedOutput(fuzOutput);
    Cleanup stagedCleanup(staged);
    std::vector<std::string> arguments;
    if (noLip) arguments.emplace_back("-nolip");
    arguments.push_back(staged.string());
    arguments.push_back(xwm.string());
    if (includeLip) arguments.push_back(lip.string());

    auto run = runTool(encoder, std::move(arguments), std::chrono::seconds(30));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FuzEncodeTimeout", "BmlFuzEncode timed out after 30 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(staged)) {
        return std::unexpected(Error{"FuzEncodeFailed", run->combined()});
    }
    if (auto published = publishStaged(staged, fuzOutput); !published) {
        return std::unexpected(published.error());
    }
    stagedCleanup.release();
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
    if (core::sameFile(xwm, fuzPath) || core::sameFile(lip, fuzPath)) {
        return std::unexpected(Error{"AudioOutputMatchesSource",
            "Refusing to overwrite the source fuz: " + fuzPath.string()});
    }
    if (core::sameFile(xwm, lip)) {
        return std::unexpected(Error{"AudioOutputsCollide",
            "xwm and lip outputs are the same file: " + xwm.string()});
    }
    if (auto ready = ensureOutputDirectory(xwm); !ready) return std::unexpected(ready.error());
    if (auto ready = ensureOutputDirectory(lip); !ready) return std::unexpected(ready.error());

    const auto stagedXwm = tempPath(".xwm");
    const auto stagedLip = tempPath(".lip");
    Cleanup xwmCleanup(stagedXwm);
    Cleanup lipCleanup(stagedLip);

    auto run = runTool(decoder, {fuzPath.string(), stagedXwm.string(), stagedLip.string()}, std::chrono::seconds(30));
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"FuzDecodeTimeout", "BmlFuzDecode timed out after 30 seconds"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(stagedXwm)) {
        return std::unexpected(Error{"FuzDecodeFailed", run->combined()});
    }
    if (auto published = publishStaged(stagedXwm, xwm); !published) {
        return std::unexpected(published.error());
    }
    xwmCleanup.release();

    std::string message = "RESULT: success (xwm -> " + xwm.string();
    if (std::filesystem::is_regular_file(stagedLip)) {
        if (auto published = publishStaged(stagedLip, lip); !published) {
            message += ", lip publish failed: " + published.error().message + ')';
        } else {
            lipCleanup.release();
            message += ", lip -> " + lip.string() + ')';
        }
    } else {
        message += ", no lip in this fuz)";
    }

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
