#include "../ModpackFolder.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main()
{
    using namespace MinewaysModpack;
    assert(quoteArgument(L"") == L"\"\"");
    assert(quoteArgument(L"Pack & $name; (test)") == L"\"Pack & $name; (test)\"");
    assert(quoteArgument(L"C:\\Pack\\") == L"\"C:\\Pack\\\\\"");
    assert(quoteArgument(L"a\"b") == L"\"a\\\"b\"");
    assert(quoteArgument(L"\u4e16\u754c") == L"\"\u4e16\u754c\"");
#ifdef _WIN32
    for (const auto& argument : {L"", L"C:\\Games\\Pack & $name; (test)\\", L"quote\"slash\\", L"\u4e16\u754c", L"%PATH%!literal"}) {
        const auto command = quoteArgument(L"test.exe") + L" " + quoteArgument(argument);
        int count = 0;
        LPWSTR* parsed = CommandLineToArgvW(command.c_str(), &count);
        assert(parsed && count == 2 && std::wstring(parsed[1]) == argument);
        LocalFree(parsed);
    }
#endif
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("Mineways modpack tests " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        for (const auto& suffix : {fs::path(), fs::path(".minecraft"), fs::path("minecraft")}) {
            const auto outer = root / (suffix.empty() ? "CurseForge pack" : suffix.string());
            const auto game = outer / suffix;
            fs::create_directories(game / "mods");
            fs::create_directories(game / "saves/World & spaces");
            std::ofstream(game / "saves/World & spaces/level.dat") << "fixture";
            assert(instanceRoot(outer) == fs::canonical(game));
            const auto world = worldFolder(game, game / "saves/World & spaces");
            assert(world == fs::canonical(game / "saves/World & spaces"));
            assert(worldFolder(game, world / "level.dat") == world);
            fs::create_directories(game / "saves/nested/elsewhere");
            std::ofstream(game / "saves/nested/elsewhere/level.dat") << "fixture";
            bool rejected = false;
            try { worldFolder(game, game / "saves/nested/elsewhere"); }
            catch (const std::exception&) { rejected = true; }
            assert(rejected);
            rejected = false;
            try { worldFolder(game, outer); }
            catch (const std::exception&) { rejected = true; }
            assert(rejected);
            std::error_code error;
            fs::create_directory_symlink(root, game / "saves/outside link", error);
            if (!error) {
                rejected = false;
                try { worldFolder(game, game / "saves/outside link"); }
                catch (const std::exception&) { rejected = true; }
                assert(rejected);
            }
        }
        bool rejected = false;
        try { instanceRoot(root); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
    std::cout << "PASS: command argument quoting, launcher roots, immediate world membership, and outside links\n";
}
