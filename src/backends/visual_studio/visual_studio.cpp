/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize Clutch targets as native MSBuild C++ projects with compiler
 * dependency tracking, per-source settings, and deterministic solution IDs.
 */

#ifdef _MSC_VER
// Microsoft's CRT opt-out requires this reserved macro spelling.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "visual_studio.hpp"

#include "core/build_graph.hpp"
#include "core/encoding_constants.hpp"
#include "core/hash_constants.hpp"
#include "core/options.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch
{
    namespace
    {
        // Preserve the second 64-bit half of deterministic project GUIDs with a
        // project-specific alternate seed. This is not a standard FNV offset basis.
        constexpr std::uint64_t guid_secondary_hash_seed =
            UINT64_C(7809847782465536322);
        constexpr auto hash64_hex_digits =
            static_cast<int>(sizeof(std::uint64_t) * encoding::hex_digits_per_byte);
        // Conventional 128-bit GUID text groups, expressed as hexadecimal digit counts.
        constexpr std::array guid_group_hex_digits{8U, 4U, 4U, 4U, 12U};
        // Match compiler option prefixes by their spelling rather than byte offsets.
        constexpr std::string_view cpp_standard_prefix = "/std:c++";
        constexpr std::string_view c_standard_prefix = "/std:c";

        // XML escaping and MSBuild escaping are separate: literal paths must not become
        // property expressions, item lists, or wildcard expansions during evaluation.
        std::string xml(const std::string& value)
        {
            std::string result;
            for (const char c : value)
            {
                switch (c)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '\"':
                    result += "&quot;";
                    break;
                case '\'':
                    result += "&apos;";
                    break;
                case '\0':
                case '\n':
                case '\r':
                    throw std::runtime_error("newlines and NUL bytes are not supported in "
                        "Visual Studio build arguments");
                default:
                    result += c;
                    break;
                }
            }

            return result;
        }

        /// Percent-escape MSBuild syntax before applying XML escaping to literal
/// values.
        std::string literal(const std::string& value)
        {
            std::string escaped;
            for (const unsigned char c : value)
            {
                if (c == '%' || c == '$' || c == '@' || c == '(' || c == ')' || c == ';' ||
                    c == '\'' || c == '?' || c == '*')
                {
                    constexpr char hex[] = "0123456789ABCDEF";
                    const auto byte = static_cast<unsigned int>(c);
                    escaped += '%';
                    escaped += hex[byte >> encoding::bits_per_nibble];
                    escaped += hex[byte & encoding::low_nibble_mask];
                }
                else
                {
                    escaped += static_cast<char>(c);
                }
            }

            return xml(escaped);
        }

        /// Write one escaped MSBuild property; only the caller controls its element
/// name.
        void property(std::ostream& out,
                      const std::string& name,
                      const std::string& value,
                      const std::string& indent = "    ")
        {
            out << indent << '<' << name << '>' << literal(value) << "</" << name
                << ">\n";
        }

        /// Give each application/test project a unique filename independent of its
/// name.
        std::string project_name(const size_t index)
        {
            return "clutch_target_" + std::to_string(index);
        }

        /// Translate Clutch's active profile to the corresponding Visual Studio label.
        std::string configuration(const Options& options)
        {
            return options.release ? "Release" : "Debug";
        }

        /// Choose the newest numerically ordered SDK with both Windows and CRT headers.
        std::string latest_windows_sdk_version(const fs::path& includes)
        {
            std::error_code error;
            std::string latest;
            std::vector<unsigned long> latest_parts;
            for (const auto& entry : fs::directory_iterator(includes, error))
            {
                const auto name = entry.path().filename().string();
                if (name.empty() ||
                    name.find_first_not_of("0123456789.") != std::string::npos)
                    continue;

                if (!fs::exists(entry.path() / "um" / "Windows.h", error) ||
                    !fs::exists(entry.path() / "ucrt" / "stddef.h", error))
                    continue;

                std::vector<unsigned long> parts;
                std::istringstream fields(name);
                std::string field;
                while (std::getline(fields, field, '.'))
                {
                    if (!field.empty())
                        parts.push_back(std::stoul(field));
                }

                if (parts > latest_parts)
                {
                    latest_parts = std::move(parts);
                    latest = name;
                }
            }

            return latest.empty() ? "10.0" : latest;
        }

        /// Honor the developer terminal's SDK, then inspect the standard installation.
        std::string windows_sdk_version()
        {
            if (const auto* selected = std::getenv("WindowsSDKVersion"))
            {
                auto version = std::string(selected);
                while (!version.empty() &&
                    (version.back() == '\\' || version.back() == '/'))
                    version.pop_back();

                if (!version.empty())
                    return version;
            }

            // Keep the selected Windows headers and libraries stable for this
            // configuration.
            if (const auto* program_files = std::getenv("ProgramFiles(x86)"))
                return latest_windows_sdk_version(fs::path(program_files) / "Windows Kits" /
                    "10" / "Include");

            return "10.0";
        }

        // Match the selected compiler's target directory, including cross-hosted MSVC.
        std::string platform(const Graph& graph)
        {
            const auto& compiler =
                graph.toolchain.cxx.empty() ? graph.toolchain.cc : graph.toolchain.cxx;
            const auto architecture =
                fs::path(compiler).parent_path().filename().string();
            if (architecture == "x86")
                return "Win32";

            if (architecture == "arm64" || architecture == "ARM64")
                return "ARM64";

#if defined(_M_ARM64) || defined(__aarch64__)
            if (architecture != "x64")
                return "ARM64";
#endif

            return "x64";
        }

        /// Generate a repeatable solution identity; this is not a random or secure
/// UUID.
        std::string guid(const std::string& seed)
        {
            // Stable identifiers avoid rewriting IDE state between identical configure
            // passes. They are project identities, not security-sensitive hashes.
            const auto hash = [](const std::string& text, uint64_t state)
            {
                for (const unsigned char c : text)
                {
                    state ^= c;
                    state *= hashing::fnv1a64_prime;
                }

                return state;
            };
            std::ostringstream digits;
            digits << std::uppercase << std::hex << std::setfill('0')
                << std::setw(hash64_hex_digits)
                << hash(seed, hashing::fnv1a64_offset_basis)
                << std::setw(hash64_hex_digits)
                << hash(seed, guid_secondary_hash_seed);
            const auto value = digits.str();
            std::string result = "{";
            size_t offset = 0;
            for (const auto length : guid_group_hex_digits)
            {
                if (offset != 0)
                    result += '-';

                result += value.substr(offset, length);
                offset += length;
            }

            return result + '}';
        }

        /// Join individually escaped values without treating literal semicolons as
/// items.
        void list_property(std::ostream& out,
                           const std::string& name,
                           const std::vector<std::string>& values)
        {
            out << "      <" << name << '>';
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i)
                    out << ';';

                out << literal(values[i]);
            }

            out << "</" << name << ">\n";
        }

        /// Translate a graph compile command into per-file MSBuild metadata.
        void write_unit(std::ostream& out, const Unit& unit)
        {
            std::vector<std::string> includes, definitions, extra;
            std::map<std::string, std::string> settings{
                {
                    "CompileAs",
                    unit.source.extension() == ".c" ? "CompileAsC" : "CompileAsCpp"
                },
                {"DebugInformationFormat", ""},
                {"ExceptionHandling", "false"},
                {"ObjectFileName", unit.object.string()},
                {"PrecompiledHeader", "NotUsing"},
                {"RuntimeLibrary", "MultiThreaded"}
            };

            // Promote recognized flags into IDE-visible properties, preserving remaining
            // options verbatim. Explicit metadata prevents project defaults overriding
            // std.
            for (size_t i = 1; i < unit.arguments.size(); ++i)
            {
                const auto& arg = unit.arguments[i];
                if (arg == unit.source.string() || arg == "/nologo" || arg == "/c" ||
                    arg == "/showIncludes" || arg.rfind("/Fo", 0) == 0)
                    continue;

                if (arg.rfind("/I", 0) == 0 || arg.rfind("/D", 0) == 0)
                {
                    auto value = arg.substr(2);
                    if (value.empty() && i + 1 < unit.arguments.size())
                        value = unit.arguments[++i];

                    (arg[1] == 'I' ? includes : definitions).push_back(std::move(value));
                    continue;
                }

                if (arg.rfind(cpp_standard_prefix, 0) == 0)
                {
                    settings["LanguageStandard"] =
                        "stdcpp" + arg.substr(cpp_standard_prefix.size());
                    continue;
                }

                if (arg.rfind(c_standard_prefix, 0) == 0)
                {
                    settings["LanguageStandard_C"] =
                        "stdc" + arg.substr(c_standard_prefix.size());
                    continue;
                }

                if (arg == "/Od" || arg == "/O1" || arg == "/O2")
                {
                    settings["Optimization"] = arg == "/Od"
                                                   ? "Disabled"
                                                   : arg == "/O1"
                                                   ? "MinSpace"
                                                   : "MaxSpeed";
                    continue;
                }

                if (arg.size() == 3 && arg[0] == '/' && arg[1] == 'W' && arg[2] >= '0' &&
                    arg[2] <= '4')
                {
                    settings["WarningLevel"] =
                        arg[2] == '0' ? "TurnOffAllWarnings" : "Level" + arg.substr(2);
                    continue;
                }

                if (arg == "/MT" || arg == "/MTd" || arg == "/MD" || arg == "/MDd")
                {
                    settings["RuntimeLibrary"] = arg == "/MT"
                                                     ? "MultiThreaded"
                                                     : arg == "/MTd"
                                                     ? "MultiThreadedDebug"
                                                     : arg == "/MD"
                                                     ? "MultiThreadedDLL"
                                                     : "MultiThreadedDebugDLL";
                    continue;
                }

                if (arg == "/Z7" || arg == "/Zi")
                {
                    settings["DebugInformationFormat"] =
                        arg == "/Z7" ? "OldStyle" : "ProgramDatabase";
                    continue;
                }

                if (arg == "/EHsc")
                {
                    settings["ExceptionHandling"] = "Sync";
                    continue;
                }

                extra.push_back(arg);
            }

            out << "    <ClCompile Include=\"" << literal(unit.source.string())
                << "\">\n";
            for (const auto& [name, value] : settings)
                property(out, name, value, "      ");

            list_property(out, "AdditionalIncludeDirectories", includes);
            list_property(out, "PreprocessorDefinitions", definitions);
            property(out, "AdditionalOptions", command_line(extra), "      ");
            out << "    </ClCompile>\n";
        }

        /// Write one native executable project with isolated intermediates and tool
/// paths.
        void write_project(const Graph& graph,
                           const Options& options,
                           const Target& target,
                           const size_t index,
                           const std::string& id)
        {
            const auto config = configuration(options);
            const auto arch = platform(graph);
            const fs::path compiler(target.units.front().arguments.front());
            const auto intermediate =
                graph.project.build / "msbuild" / project_name(index);
            fs::create_directories(intermediate);
            fs::create_directories(target.output.parent_path());
            for (const auto& unit : target.units)
                fs::create_directories(unit.object.parent_path());

            std::ostringstream project;
            project << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<!-- Generated by Clutch. Configure clutch.toml to change build "
                "settings. -->\n"
                "<Project DefaultTargets=\"Build\" ToolsVersion=\"Current\" "
                "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                "  <ItemGroup Label=\"ProjectConfigurations\">\n"
                "    <ProjectConfiguration Include=\""
                << config << '|' << arch << "\">\n";
            property(project, "Configuration", config, "      ");
            property(project, "Platform", arch, "      ");
            project << "    </ProjectConfiguration>\n  </ItemGroup>\n"
                "  <PropertyGroup Label=\"Globals\">\n";
            property(project, "ProjectGuid", id);
            property(project, "ProjectName", target.name);
            property(project, "Keyword", "Win32Proj");
            property(project, "WindowsTargetPlatformVersion", windows_sdk_version());
            property(project, "Configuration", config);
            property(project, "Platform", arch);
            project << "  </PropertyGroup>\n"
                "  <Import "
                "Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
                "  <PropertyGroup Label=\"Configuration\">\n";
            property(project, "ConfigurationType", "Application");
            property(project, "UseDebugLibraries", options.release ? "false" : "true");
            // Each installed Visual Studio supplies its own current toolset. Do not tie
            // generated projects to v143/v145 or a particular Visual Studio release.
            project << "    <PlatformToolset Condition=\"'$(PlatformToolset)' == ''\">"
                "$(DefaultPlatformToolset)</PlatformToolset>\n";
            project << "  </PropertyGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
                "  <ImportGroup Label=\"ExtensionSettings\" />\n"
                "  <ImportGroup Label=\"PropertySheets\" />\n"
                "  <PropertyGroup Label=\"UserMacros\" />\n"
                "  <PropertyGroup>\n";
            property(project, "OutDir", target.output.parent_path().string() + "\\");
            property(project, "IntDir", intermediate.string() + "\\");
            property(project, "TargetName", target.output.stem().string());
            property(project, "TargetExt", target.output.extension().string());
            property(project, "LinkIncremental", "false");
            property(project, "TrackFileAccess", "true");
            // Pin the configured compiler/linker instead of inheriting an unrelated
            // toolset.
            property(project, "CLToolPath", compiler.parent_path().string());
            property(project, "CLToolExe", compiler.filename().string());
            property(project, "LinkToolPath", compiler.parent_path().string());
            property(project, "LinkToolExe", "link.exe");
            project << "  </PropertyGroup>\n  <ItemDefinitionGroup>\n    <Link>\n";
            property(project, "SubSystem", "Console", "      ");
            property(project, "OutputFile", target.output.string(), "      ");

            // CL's /link separates linker-only options from driver and object arguments.
            std::vector<std::string> link_options;
            bool linker_options = false;
            bool debug = false;
            for (size_t i = 1; i < target.link.size(); ++i)
            {
                const auto& arg = target.link[i];
                if (arg == "/link")
                {
                    linker_options = true;
                    continue;
                }

                if (arg == "/DEBUG")
                {
                    debug = true;
                    continue;
                }

                if (linker_options)
                    link_options.push_back(arg);
            }

            property(project,
                     "GenerateDebugInformation",
                     debug ? "true" : "false",
                     "      ");
            property(project, "AdditionalOptions", command_line(link_options), "      ");
            project << "    </Link>\n  </ItemDefinitionGroup>\n  <ItemGroup>\n";
            for (const auto& unit : target.units)
                write_unit(project, unit);

            project
                << "  </ItemGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
                "  <ImportGroup Label=\"ExtensionTargets\" />\n</Project>\n";
            write_if_changed(graph.project.build / (project_name(index) + ".vcxproj"),
                             project.str());
        }
    } // namespace

    /// Emit native projects, their solution and explicit application/test
/// traversal.
    void generate_visual_studio(const Graph& graph,
                                const Options& options,
                                const fs::path& /*clutch_executable*/)
    {
        if (!graph.toolchain.msvc)
            throw std::runtime_error("the Visual Studio backend requires an MSVC "
                "compiler; select cl with --cc and --cxx");

        // One Clutch profile maps to one solution configuration in this build
        // directory.
        const auto config = configuration(options);
        const auto arch = platform(graph);
        std::vector<std::string> ids;
        std::ostringstream solution;
        solution << "Microsoft Visual Studio Solution File, Format Version 12.00\n"
            "# Generated by Clutch\n";
        for (size_t i = 0; i < graph.targets.size(); ++i)
        {
            const auto& target = graph.targets[i];
            if (target.units.empty())
                throw std::runtime_error(
                    "Visual Studio target has no compilation units: " + target.name);

            ids.push_back(
                guid(graph.project.root.generic_string() + ":" + project_name(i)));
            write_project(graph, options, target, i, ids.back());
            solution << "Project(\"{BC8A1FFA-BEE3-4634-8014-F334798102B3}\") = \""
                << project_name(i) << "\", \"" << project_name(i)
                << ".vcxproj\", \"" << ids.back() << "\"\nEndProject\n";
        }

        solution << "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = "
            "preSolution\n\t\t"
            << config << '|' << arch << " = " << config << '|' << arch
            << "\n\tEndGlobalSection\n"
            "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
        for (size_t i = 0; i < ids.size(); ++i)
        {
            solution << "\t\t" << ids[i] << '.' << config << '|' << arch
                << ".ActiveCfg = " << config << '|' << arch << '\n';
            // Ordinary solution builds compile the application. Test projects are
            // explicitly selected by clutch test, matching the other backend drivers.
            if (i == 0)
                solution << "\t\t" << ids[i] << '.' << config << '|' << arch
                    << ".Build.0 = " << config << '|' << arch << '\n';
        }

        solution << "\tEndGlobalSection\nEndGlobal\n";
        write_if_changed(graph.project.build / "clutch.sln", solution.str());

        // A traversal project selects tests independently of the solution's default
        // Build.0 entries. MSBuild solution selection can otherwise silently exclude
        // a test project that is intentionally absent from ordinary solution builds.
        std::ostringstream traversal;
        traversal
            << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<!-- Generated by Clutch. Native MSBuild target selection. -->\n"
            "<Project DefaultTargets=\"clutch_target_0\" "
            "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
        for (size_t i = 0; i < graph.targets.size(); ++i)
        {
            traversal << "  <Target Name=\"" << project_name(i) << "\">\n"
                << "    <MSBuild Projects=\"" << project_name(i)
                << ".vcxproj\" Targets=\"Build\" BuildInParallel=\"true\" />\n"
                "  </Target>\n";
        }

        traversal << "</Project>\n";
        write_if_changed(graph.project.build / "clutch.proj", traversal.str());
    }

    /// Build selected traversal targets without including unrelated solution
/// projects.
    std::vector<std::string>
    visual_studio_build_command(const Graph& graph,
                                const Options& options,
                                const std::vector<size_t>& target_indices)
    {
        std::string targets;
        for (const auto index : target_indices)
        {
            if (index >= graph.targets.size())
                throw std::runtime_error("invalid Visual Studio build target index");

            if (!targets.empty())
                targets += ';';

            targets += project_name(index);
        }

        if (targets.empty())
            throw std::runtime_error("no Visual Studio build targets were selected");

        std::vector<std::string> command{
            graph.backend.executable,
            (graph.project.build / "clutch.proj").string(),
            "/nologo",
            "/verbosity:minimal",
            "/t:" + targets,
            "/p:Configuration=" + configuration(options),
            "/p:Platform=" + platform(graph)
        };
        command.push_back(options.jobs > 0
                              ? "/m:" + std::to_string(options.jobs)
                              : "/m");
        return command;
    }
} // namespace clutch
