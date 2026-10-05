// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <soundcurrent/project_store.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
int run(const std::vector<std::string> &args) {
    try {
        if (args.size() != 3 || (args[1] != "new" && args[1] != "inspect")) {
            std::cerr << "Usage: sc-project-tool new|inspect PROJECT_DIRECTORY\n";
            return 2;
        }
        ProjectStore store(utf8Path(args[2]));
        if (args[1] == "new") {
            if (std::filesystem::exists(store.root() / "project.json"))
                throw ProjectError(ErrorCode::Io, "Project already exists");
            store.save(makeOneTrackSession("Untitled", "Audio 1"));
        }
        std::cout << encodeProject(store.load());
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0,
                                          nullptr, nullptr);
        if (n <= 0)
            return 2;
        std::string s(static_cast<std::size_t>(n), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, s.data(), n, nullptr,
                                 nullptr))
            return 2;
        s.pop_back();
        args.push_back(std::move(s));
    }
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv, argv + argc});
}
#endif
