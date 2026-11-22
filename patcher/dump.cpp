// Prints the game's files for the game testing library, which reads the game's formats only through this program.
// Each command prints one line per answer: JSON, or a bare path or name.
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "game_file.h"
#include "msbt.h"
#include "sarc.h"
#include "size_table.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Raised when what a command prints for the same game file changes, which gives the library's cache a new key.
constexpr int Format = 1;

// check-mod's exit status when it names a file too large. An error exits 1, and a wrong command line 2.
constexpr int TooLarge = 3;

// The formats that hold the names of the game's saved values, and the archives whose members are read for them. The
// game's pictures, models, sounds and other binary data also hold runs of name characters, and their runs name about
// as many saved values as chance gives.
constexpr std::array NameFormats = { ".byml", ".bgyml", ".bfevfl", ".ainb", ".msbt" };
constexpr std::array Archives = { ".pack", ".sarc" };
constexpr size_t ShortestName = 3;
constexpr std::string_view Zs = ".zs";

bool EndsInOneOf(std::string_view name, std::span<const char* const> endings) {
    for (const std::string_view ending : endings) {
        if (name.ends_with(ending)) {
            return true;
        }
    }
    return false;
}

std::string_view WithoutZs(std::string_view path) {
    return path.ends_with(Zs) ? path.substr(0, path.size() - Zs.size()) : path;
}

std::string Hex(const GameFile::Sha256& bytes) {
    std::string text;
    for (const uint8_t byte : bytes) {
        text += std::format("{:02x}", byte);
    }
    return text;
}

void Print(std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string data{ std::istreambuf_iterator<char>(file), {} };
    if (!file) {
        throw std::runtime_error(std::format("{}: cannot read", path.string()));
    }
    return data;
}

class Game {
public:
    explicit Game(const std::filesystem::path& nup) : file(nup), dictionaries(Dictionaries(file)) {
    }

    std::string Opened(std::string_view path, std::string data) const {
        if (!path.ends_with(Zs)) {
            return data;
        }
        const unsigned id = FrameDictionaryId(data);
        if (id == 0) {
            return Decompress(data);
        }
        const auto dictionary = dictionaries.find(id);
        if (dictionary == dictionaries.end()) {
            throw std::runtime_error(std::format("the file names dictionary {}, which the game does not have", id));
        }
        return Decompress(data, dictionary->second);
    }

    std::string Content(const std::string& path) {
        return Opened(path, file.Read(path));
    }

    // The file of the RomFS the path names, or the pack it names a member of, as "<pack>/<member>".
    std::pair<std::string, std::optional<std::string>> Holder(const std::string& path) const {
        if (file.Files().contains(path)) {
            return { path, std::nullopt };
        }
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            if (file.Files().contains(path.substr(0, slash))) {
                return { path.substr(0, slash), path.substr(slash + 1) };
            }
        }
        throw std::runtime_error("the game has no such file");
    }

    std::string Member(const std::string& holder, const std::optional<std::string>& member) {
        std::string data = Content(holder);
        if (!member) {
            return data;
        }
        sarc::Files pack = sarc::Parse(data);
        const auto found = pack.find(*member);
        if (found == pack.end()) {
            throw std::runtime_error(std::format("{} has no {}", holder, *member));
        }
        return std::move(found->second);
    }

    GameFile file;

private:
    std::map<unsigned, std::string> dictionaries;
};

void AddNames(std::string_view data, std::set<std::string>& names) {
    const auto isNameCharacter = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    };
    size_t start = 0;
    for (size_t at = 0; at <= data.size(); ++at) {
        if (at < data.size() && isNameCharacter(data[at])) {
            continue;
        }
        if (at - start >= ShortestName) {
            names.emplace(data.substr(start, at - start));
        }
        start = at + 1;
    }
}

nlohmann::json Tree(std::string_view data) {
    if (data.starts_with("YB") || data.starts_with("BY")) {
        return byml::Json(byml::Parse(data).root);
    }
    if (data.starts_with("MsgStdBn")) {
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& [label, text] : msbt::Parse(data)) {
            messages.push_back({ { "label", label }, { "text", msbt::Decode(text) } });
        }
        return messages;
    }
    throw std::runtime_error("neither a BYML file nor a message file");
}

int Key(Game& game, std::span<const std::string>) {
    const nlohmann::json key = { { "key", Hex(game.file.TableOfContentsSha256()) },
                                 { "build", game.file.ProgramBuildId() },
                                 { "format", Format } };
    Print(key.dump());
    return 0;
}

// After a pack, the members of it, as "<pack>/<member>".
int List(Game& game, std::span<const std::string> args) {
    const std::string_view prefix = args.empty() ? std::string_view() : args[0];
    for (const std::string& path : game.file.Files() | std::views::keys) {
        if (path.starts_with(prefix)) {
            Print(path);
        }
        const bool reached = path.starts_with(prefix) || prefix.starts_with(path + "/");
        if (!reached || !EndsInOneOf(WithoutZs(path), Archives)) {
            continue;
        }
        try {
            for (const std::string& member : sarc::Parse(game.Content(path)) | std::views::keys) {
                if (const std::string inside = path + "/" + member; inside.starts_with(prefix)) {
                    Print(inside);
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("{}: {}", path, e.what()));
        }
    }
    return 0;
}

int Files(Game& game, std::span<const std::string> paths) {
    for (const std::string& path : paths) {
        try {
            const auto [holder, member] = game.Holder(path);
            const nlohmann::json line = {
                { "path", path },
                { "sha256", Hex(game.file.Files().at(holder).sha256) },
                { "root", Tree(game.Member(holder, member)) },
            };
            Print(line.dump());
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("{}: {}", path, e.what()));
        }
    }
    return 0;
}

std::set<std::string> GameNames(Game& game) {
    std::set<std::string> names;
    for (const std::string& path : game.file.Files() | std::views::keys) {
        try {
            if (EndsInOneOf(WithoutZs(path), NameFormats)) {
                AddNames(game.Content(path), names);
            } else if (EndsInOneOf(WithoutZs(path), Archives)) {
                for (const auto& [name, data] : sarc::Parse(game.Content(path))) {
                    if (EndsInOneOf(name, NameFormats)) {
                        AddNames(data, names);
                    }
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("{}: {}", path, e.what()));
        }
    }
    return names;
}

int Strings(Game& game, std::span<const std::string> paths) {
    if (paths.empty()) {
        for (const std::string& name : GameNames(game)) {
            Print(name);
        }
        return 0;
    }
    for (const std::string& path : paths) {
        try {
            const auto [holder, member] = game.Holder(path);
            std::set<std::string> names;
            AddNames(game.Member(holder, member), names);
            Print(nlohmann::json({ { "path", path }, { "strings", names } }).dump());
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("{}: {}", path, e.what()));
        }
    }
    return 0;
}

// The game reads the update's size table before the files it lists, and ignores the base game's (seen in the game).
bool IsSizeTable(std::string_view path) {
    return path.ends_with(".rsizetable.zs");
}

// Names each file the game would drop without a word, as larger than the entry the size table it reads gives it, and
// each mod file it cannot measure. A mod file the game also has needs its size rounded up, plus the overhead the game's
// own entry gives that file. A file the mod does not carry needs the game's own entry.
int CheckMod(Game& game, std::span<const std::string> args) {
    const std::filesystem::path mod = args[0];
    if (!std::filesystem::is_directory(mod)) {
        throw std::runtime_error(std::format("{}: not a folder", mod.string()));
    }
    const std::filesystem::path romfs = mod / "romfs";
    std::map<std::string, std::filesystem::path> carried;
    if (std::filesystem::exists(romfs)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(romfs)) {
            const std::string path = std::filesystem::relative(entry.path(), romfs).generic_string();
            if (entry.is_regular_file() && !IsSizeTable(path)) {
                carried.emplace(path, entry.path());
            }
        }
    }
    const SizeEntries games(ReadSizeTable(game.file.Read(std::string(SizeTablePath))));
    std::optional<SizeEntries> mods;
    if (std::filesystem::exists(romfs / SizeTablePath)) {
        mods.emplace(ReadSizeTable(ReadFile(romfs / SizeTablePath)));
    }
    const SizeEntries& table = mods ? *mods : games;
    constexpr std::string_view NoEntry = "the size table the game reads has no entry for it";

    std::map<std::string, nlohmann::json> lines;
    const auto tooLarge = [&](const std::string& path, size_t needs, uint32_t entry) {
        lines[path] = { { "path", path }, { "needs", needs }, { "entry", entry } };
    };
    const auto unchecked = [&](const std::string& path, std::string_view why) {
        lines[path] = { { "path", path }, { "unchecked", why } };
    };
    for (const auto& [path, file] : carried) {
        const std::optional<uint32_t> entry = table.Find(path);
        const std::optional<uint32_t> own = games.Find(path);
        if (!game.file.Files().contains(path)) {
            unchecked(path, "the game has no copy of it to measure it by");
        } else if (!entry) {
            unchecked(path, NoEntry);
        } else if (!own) {
            unchecked(path, "the game's own size table has no entry for its copy");
        } else {
            try {
                const size_t overhead = *own - AlignUp(game.Content(path).size(), SizeAlignment);
                const size_t needs = AlignUp(game.Opened(path, ReadFile(file)).size(), SizeAlignment) + overhead;
                if (needs > *entry) {
                    tooLarge(path, needs, *entry);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("{}: {}", path, e.what()));
            }
        }
    }
    if (mods) {
        for (const std::string& path : game.file.Files() | std::views::keys) {
            const std::optional<uint32_t> own = games.Find(path);
            if (!own || carried.contains(path) || IsSizeTable(path)) {
                continue;
            }
            if (const std::optional<uint32_t> entry = mods->Find(path); !entry) {
                unchecked(path, NoEntry);
            } else if (*entry < *own) {
                tooLarge(path, *own, *entry);
            }
        }
    }
    int status = 0;
    for (const nlohmann::json& line : lines | std::views::values) {
        Print(line.dump());
        if (line.contains("needs")) {
            status = TooLarge;
        }
    }
    return status;
}

struct Command {
    std::string_view name;
    std::string_view arguments;
    size_t least;
    size_t most;
    int (*run)(Game&, std::span<const std::string>);
};

constexpr size_t Any = std::numeric_limits<size_t>::max();

constexpr std::array Commands = {
    Command{ "key", "", 0, 0, Key },
    Command{ "list", " [prefix]", 0, 1, List },
    Command{ "files", " <path>...", 1, Any, Files },
    Command{ "strings", " [path...]", 0, Any, Strings },
    Command{ "check-mod", " <mod-dir>", 1, 1, CheckMod },
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto command = std::ranges::find_if(Commands, [&](const Command& c) {
        return args.size() >= 2 && args[0] == c.name && args.size() - 2 >= c.least && args.size() - 2 <= c.most;
    });
    if (command == Commands.end()) {
        for (const Command& c : Commands) {
            std::fprintf(stderr, "usage: %s %s <game.nup>%s\n", argv[0], c.name.data(), c.arguments.data());
        }
        return 2;
    }
    try {
        Game game(args[1]);
        const int status = command->run(game, std::span(args).subspan(2));
        if (std::fflush(stdout) != 0) {
            throw std::runtime_error("cannot write the output");
        }
        return status;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
