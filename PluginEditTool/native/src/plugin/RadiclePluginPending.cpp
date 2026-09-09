#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <utility>

namespace fo4ide::plugin {

void RadiclePluginBackend::stagePluginMutationLocked(LoadedPlugin& loaded,
                                                     fo4::Plugin mutated) {
    if (!loaded.dirty) loaded.originalPlugin = loaded.plugin;
    loaded.plugin = std::move(mutated);
    loaded.dirty = true;
    rebuildGraphLocked();
}

}
