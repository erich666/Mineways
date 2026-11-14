#pragma once
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("clamp")
#undef min
#undef max
#undef clamp
#include <filesystem>
#include <stdexcept>
#include <string>
#pragma pop_macro("clamp")
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace MinewaysModpack {
inline std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else {
            quoted.append(character == L'\"' ? 2 * backslashes + 1 : backslashes, L'\\');
            quoted += character;
            backslashes = 0;
        }
    }
    quoted.append(2 * backslashes, L'\\');
    return quoted + L'\"';
}

inline std::filesystem::path instanceRoot(const std::filesystem::path& selected)
{
    for (const auto& candidate : {selected, selected / ".minecraft", selected / "minecraft"}) {
        if (std::filesystem::is_directory(candidate / "mods") &&
            std::filesystem::is_directory(candidate / "saves"))
            return std::filesystem::canonical(candidate);
    }
    throw std::runtime_error("Choose the installed CurseForge or Prism instance folder containing mods and saves. Create a world in Minecraft first if its saves folder is missing.");
}

inline std::filesystem::path worldFolder(const std::filesystem::path& instance,
    const std::filesystem::path& selected)
{
    auto world = std::filesystem::canonical(selected);
    if (std::filesystem::is_regular_file(world)) {
        if (!std::filesystem::equivalent(world, world.parent_path() / "level.dat"))
            throw std::runtime_error("Choose a saved world folder or its level.dat file.");
        world = world.parent_path();
    }
    const auto saves = std::filesystem::canonical(instance / "saves");
    if (!std::filesystem::is_directory(world) ||
        !std::filesystem::equivalent(world.parent_path(), saves) ||
        !std::filesystem::is_regular_file(world / "level.dat") ||
        !std::filesystem::equivalent(std::filesystem::canonical(world / "level.dat").parent_path(), world))
        throw std::runtime_error("Choose an immediate world folder inside this modpack's saves folder. It must contain level.dat and must not link to a world outside that folder.");
    return world;
}
}
