#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {

struct JournalArchiveReference;
struct JournalApprovedReadRoot;
struct HeldStringTable;

struct JournalReadStringFile {
    std::string filename{};
    std::uint64_t size{0};
    std::string sha256{};
};

struct JournalReadStringInventory {
    std::filesystem::path rootSelector{};
    std::vector<std::string> directories{};
    bool exists{false};
    std::vector<JournalReadStringFile> files{};
};

std::string encodeJournalReadStringInventories(const std::vector<JournalReadStringInventory>& inventories,
                                              const std::filesystem::path& plugin);
std::vector<JournalReadStringInventory> decodeJournalReadStringInventories(std::string_view json,
                                                                           const std::filesystem::path& plugin);
std::vector<JournalReadStringInventory> captureJournalReadStringInventories(
    const std::vector<JournalArchiveReference>& directories, const std::vector<JournalApprovedReadRoot>& approved,
    const std::filesystem::path& plugin);
JournalReadStringInventory journalReadStringInventoryFromHeld(
    const std::filesystem::path& rootSelector, std::vector<std::string> directories, bool exists,
    const std::vector<HeldStringTable>& tables, const std::filesystem::path& plugin);
void verifyJournalReadStringInventories(const std::vector<JournalReadStringInventory>& inventories,
                                        const std::vector<JournalApprovedReadRoot>& approved,
                                        const std::filesystem::path& plugin);

}
