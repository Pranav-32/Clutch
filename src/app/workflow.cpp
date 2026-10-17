/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Create project skeletons and clean only owned profile build
 * directories.
 */

#include "workflow.hpp"

#include "core/build_directory.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/terminal.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace clutch
{
    /// Scaffold only missing project files; require_new rejects an existing root.
    void create_project(const fs::path& directory,
                        const std::string& requested_name,
                        const bool c,
                        const bool require_new)
    {
        const auto root = fs::absolute(directory).lexically_normal();
        if (require_new && fs::exists(root))
            throw std::runtime_error("destination already exists: " + root.string());

        // Restrict names to spellings usable as executable and native target names.
        const auto name =
            requested_name.empty() ? root.filename().string() : requested_name;
        if (name.empty() ||
            !std::all_of(name.begin(), name.end(), [](const unsigned char v)
            {
                return (v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z') ||
                    (v >= '0' && v <= '9') || v == '_' || v == '-';
            }))
            throw std::runtime_error("project name must contain only letters, digits, "
                "'_' or '-'; use --name");

        if (fs::exists(root / layout::manifest))
            throw std::runtime_error("clutch.toml already exists");

        // Preserve existing code. Scaffold an entry only for an empty source
        // directory.
        if (fs::exists(root / "src") && !fs::is_directory(root / "src"))
            throw std::runtime_error("src must be a directory");

        if (const bool has_code =
                fs::exists(root / "src") && !fs::is_empty(root / "src");
            !has_code)
        {
            write_if_changed(
                root / "src" / (c ? "main.c" : "main.cpp"),
                c
                    ? "#include <stdio.h>\n\nint main(void) {\n    puts(\"Hello from "
                    "Clutch!\");\n    return 0;\n}\n"
                    : "#include <iostream>\n\nint main() {\n    std::cout << \"Hello "
                    "from "
                    "Clutch!\\n\";\n}\n");
        }

        // Every scaffold uses separate profile directories and an explicit standard.
        write_if_changed(
            root / layout::manifest,
            "[project]\nname = \"" + name + "\"\nversion = \"0.1.0\"\n" +
            (c
                 ? "language = \"c\"\nstd = 11\n"
                 : "language = \"cpp\"\nstd = 17\n") +
            "\n[build]\ngenerator = \"ninja\"\n"
            "\n[profile.debug]\nbuild_dir = \"" +
            layout::default_build_directory(false) +
            "\"\n\n[profile.release]\nbuild_dir = \"" +
            layout::default_build_directory(true) + "\"\n");

        if (!fs::exists(root / ".gitignore"))
            write_if_changed(root / ".gitignore", std::string("/") +
                layout::environment + "/\n/" + layout::build_prefix + "*/\n");

        status("Created", name + " at " + root.string());
    }

    /// Remove only the owned profile after checking its root and every descendant.
    void clean_project(const Options& options)
    {
        const auto root = fs::canonical(options.root);
        if (!fs::is_regular_file(root / layout::manifest))
            throw std::runtime_error("clean requires a project containing clutch.toml");

        // Resolve the path directly from TOML so clean can remove stale
        // configuration.
        const auto profile = build_directory(options);
        validate_build_directory(root, profile, true);

        if (fs::exists(profile))
        {
            // Check descendants too: junctions are not always reported as symlinks.
            for (const auto& entry : fs::recursive_directory_iterator(profile))
            {
                if (entry.is_symlink() || fs::canonical(entry.path()) != entry.path())
                    throw std::runtime_error("refusing to clean redirected build entry: " +
                        entry.path().string());
            }

            fs::remove_all(profile);
        }

        status("Cleaned",
               std::string(options.release ? "release" : "debug") +
               " build artifacts");
    }
} // namespace clutch
