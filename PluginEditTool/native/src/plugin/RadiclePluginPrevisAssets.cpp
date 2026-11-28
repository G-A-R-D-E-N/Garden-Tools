#include "RadiclePluginPrevisAssets.hpp"

#include <fo4/archive/assets.hpp>
#include <fo4/previs/uvd.hpp>
#include <fo4/previs/uvd_tile_build.hpp>

#include "RadiclePluginPrevisIo.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

namespace fo4ide::plugin::previs_assets {

namespace {

// Constraint: a bounded candidate list keeps a resolution over a large archive set from reading every _OC of a cell.
constexpr std::size_t kMaxShellCandidates = 8;

std::string loweredText(std::string text) {
  for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  return text;
}

std::string relativeToData(const std::filesystem::path& dataDirectory, const std::filesystem::path& path) {
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(dataDirectory, error);
  if (error) return {};
  const auto target = std::filesystem::weakly_canonical(path, error);
  if (error) return {};
  const auto inside = target.lexically_relative(root);
  if (inside.empty() || inside.string().rfind("..", 0) == 0) return {};
  return inside.generic_string();
}

}  // namespace

std::filesystem::path resolveOutputRoot(const std::filesystem::path& dataDirectory,
                                        const std::filesystem::path& configuredRoot,
                                        const std::filesystem::path& pluginPath) {
  if (!configuredRoot.empty()) return configuredRoot;
  // Constraint: the game loads previs output from the data root (Meshes\PreCombined, Vis), and the patch plugin in an MO2 profile lives outside it, so the plugin's own folder cannot be the default output root.
  if (!dataDirectory.empty()) return dataDirectory;
  return pluginPath.parent_path();
}

previs_prep::ReadModelBytes archiveModelReader(const std::filesystem::path& dataDirectory) {
  std::vector<std::filesystem::path> archives;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(dataDirectory, error)) {
    if (entry.path().extension() == ".ba2") archives.push_back(entry.path());
  }
  std::sort(archives.begin(), archives.end());
  auto assets = std::make_shared<const fo4::archive::AssetResolver>(
      fo4::archive::AssetResolver::open(dataDirectory, archives));
  return [assets](const std::string& modelPath) -> std::vector<std::byte> {
    const auto location = assets->locate(modelPath);
    if (!location.has_value()) return {};
    return assets->read(*location);
  };
}

std::vector<OcShell> ocShells(const std::filesystem::path& dataDirectory,
                             std::string_view requested,
                             std::string_view cellPrefix,
                             const std::vector<std::string>& ownerFolders) {
  std::vector<OcShell> shells;
  std::error_code error;
  std::vector<std::filesystem::path> archives;
  for (const auto& entry : std::filesystem::directory_iterator(dataDirectory, error)) {
    if (entry.path().extension() == ".ba2") archives.push_back(entry.path());
  }
  std::sort(archives.begin(), archives.end());
  auto assets = fo4::archive::AssetResolver::open(dataDirectory, archives);

  if (!requested.empty()) {
    const std::filesystem::path named{std::string(requested)};
    // Constraint: a named template is the only one used, so a caller's choice is never silently replaced by another cell's shell.
    // Constraint: a caller-supplied template may be a loose file, sit inside Data, or already be Data-relative.
    if (auto loose = readBinaryFile(named); !loose.empty()) {
      shells.push_back({std::string(requested), std::move(loose)});
      return shells;
    }
    const auto relative = relativeToData(dataDirectory, named);
    const auto key = relative.empty() ? std::string(requested) : relative;
    // Constraint: a typed path outside Data (a drive path) is not an archive path; the asset resolver throws on it, so it resolves to no template.
    if (key.find(':') != std::string::npos) return shells;
    const auto location = assets.locate(key);
    if (location.has_value()) {
      if (auto bytes = assets.read(*location); !bytes.empty()) shells.push_back({key, std::move(bytes)});
    }
    return shells;
  }

  // Constraint: only a file named after the cell can be that cell's template, so every candidate is filtered by the cell prefix.
  std::vector<std::string> owners;
  for (const auto& folder : ownerFolders) owners.push_back(loweredText(folder));
  // Constraint: a shell directly under PreCombined belongs to the base game plugin, as the load-order scan reads it.
  const auto ownerRank = [&](const std::filesystem::path& path) {
    auto folder = loweredText(path.parent_path().filename().string());
    if (folder == "precombined") folder = "fallout4.esm";
    return static_cast<std::size_t>(std::find(owners.begin(), owners.end(), folder) - owners.begin());
  };
  // Constraint: the file name is lowered before the compare, so the cell prefix must be lowered too.
  const auto loweredPrefix = loweredText(std::string(cellPrefix));
  const auto isCellTemplate = [&](const std::filesystem::path& path) {
    if (!owners.empty() && ownerRank(path) == owners.size()) return false;
    const auto name = loweredText(path.filename().string());
    return name.rfind(loweredPrefix, 0) == 0 && name.find("_oc.nif") != std::string::npos;
  };
  // Constraint: the loose tree is walked directly, because resolving a template must not depend on an archive listing that shadows it.
  error.clear();
  std::vector<std::filesystem::path> loose;
  for (std::filesystem::recursive_directory_iterator it(dataDirectory / "Meshes" / "PreCombined", error), end;
       !error && it != end;
       it.increment(error)) {
    std::error_code fileError;
    if (!it->is_regular_file(fileError)) continue;
    if (!isCellTemplate(it->path())) continue;
    loose.push_back(it->path());
  }
  std::sort(loose.begin(), loose.end(), [&](const auto& left, const auto& right) {
    const auto leftRank = ownerRank(left);
    const auto rightRank = ownerRank(right);
    return leftRank != rightRank ? leftRank < rightRank : left < right;
  });
  for (const auto& path : loose) {
    if (shells.size() >= kMaxShellCandidates) break;
    auto bytes = readBinaryFile(path);
    if (bytes.empty()) continue;
    shells.push_back({path.string(), std::move(bytes)});
  }
  std::vector<fo4::archive::AssetListing> archived;
  for (auto& listing : assets.list("Meshes/PreCombined", ".nif", false)) {
    if (!isCellTemplate(std::filesystem::path(listing.path))) continue;
    archived.push_back(std::move(listing));
  }
  std::sort(archived.begin(), archived.end(), [&](const auto& left, const auto& right) {
    const auto leftRank = ownerRank(std::filesystem::path(left.path));
    const auto rightRank = ownerRank(std::filesystem::path(right.path));
    return leftRank != rightRank ? leftRank < rightRank : left.path < right.path;
  });
  for (const auto& candidate : archived) {
    if (shells.size() >= kMaxShellCandidates) break;
    const auto name = loweredText(std::filesystem::path(candidate.path).filename().string());
    const bool shadowed = std::any_of(shells.begin(), shells.end(), [&](const OcShell& shell) {
      return loweredText(std::filesystem::path(shell.path).filename().string()) == name;
    });
    if (shadowed) continue;
    auto bytes = assets.read(candidate.location);
    if (bytes.empty()) continue;
    shells.push_back({candidate.path, std::move(bytes)});
  }
  return shells;
}

std::vector<std::filesystem::path> previsAssetRoots(
    const std::filesystem::path& dataDirectory,
    std::span<const std::filesystem::path> mo2AssetRootsLowToHigh,
    const std::filesystem::path& previsOutputRoot) {
  std::vector<std::filesystem::path> roots(mo2AssetRootsLowToHigh.begin(), mo2AssetRootsLowToHigh.end());
  if (!dataDirectory.empty() && std::find(roots.begin(), roots.end(), dataDirectory) == roots.end()) {
    roots.insert(roots.begin(), dataDirectory);
  }
  if (!previsOutputRoot.empty() &&
      std::find(roots.begin(), roots.end(), previsOutputRoot) == roots.end()) {
    roots.push_back(previsOutputRoot);
  }
  return roots;
}

std::optional<std::vector<std::byte>> readVisTome(const std::filesystem::path& dataDirectory,
                                   std::string_view pluginFolder,
                                   std::uint32_t cellLow24,
                                   std::span<const std::string> pluginLoadOrder,
                                   std::span<const std::filesystem::path> mo2AssetRootsLowToHigh,
                                   const std::filesystem::path& previsOutputRoot) {
  const auto rootsLowToHigh = previsAssetRoots(dataDirectory, mo2AssetRootsLowToHigh, previsOutputRoot);
  const auto loosePath = fo4::previs::uvd_tome_path(dataDirectory, pluginFolder, cellLow24);
  const auto relative = loosePath.lexically_relative(dataDirectory).generic_string();
  if (relative.empty() || relative.rfind("..", 0) == 0) return {};
  bool sawEmpty = false;
  for (auto root = rootsLowToHigh.rbegin(); root != rootsLowToHigh.rend(); ++root) {
    const auto archives = fo4::archive::archive_load_order(*root, {}, pluginLoadOrder);
    const auto assets = fo4::archive::AssetResolver::open(*root, archives);
    if (const auto location = assets.locate(relative); location.has_value()) {
      if (auto bytes = assets.read(*location); !bytes.empty()) {
        return bytes;
      }
      sawEmpty = true;
    }
  }
  if (sawEmpty) return std::vector<std::byte>{};
  return {};
}

std::vector<MaterialRun> materialRuns(const previs_prep::Prepared* prepared, std::size_t groupCount) {
  std::vector<MaterialRun> runs;
  for (std::size_t index = 0; index < groupCount; ++index) {
    const auto material = prepared != nullptr ? prepared->materials[index] : std::string{};
    if (prepared == nullptr || runs.empty() || runs.back().material != material) {
      runs.push_back(MaterialRun{material, {}});
    }
    runs.back().groupIndices.push_back(index);
  }
  return runs;
}

}  // namespace fo4ide::plugin::previs_assets

