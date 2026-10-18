/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize a deterministic OpenStep project and shared schemes for
 * native C/C++ compilation with Xcode's macOS toolchain.
 *
 * Each profile owns a separate project directory and one build configuration.
 * Xcode schedules compilation and linking itself; compiler database commands
 * remain available independently through Clutch's normal configuration pass.
 */

#include "xcode.hpp"

#include "core/build_graph.hpp"
#include "core/encoding_constants.hpp"
#include "core/hash_constants.hpp"
#include "core/options.hpp"
#include "platform/files.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace clutch {
namespace {
// PBX IDs concatenate a 64-bit FNV-1a value and a 32-bit FNV-1a value as hex.
constexpr auto hash64_hex_digits =
    static_cast<int>(sizeof(std::uint64_t) * encoding::hex_digits_per_byte);
constexpr auto hash32_hex_digits =
    static_cast<int>(sizeof(std::uint32_t) * encoding::hex_digits_per_byte);

// Xcode's serialized project and scheme format identifiers, not compiler
// versions.
constexpr auto pbx_archive_version = 1;
constexpr auto pbx_object_version = 56;
constexpr auto pbx_compatibility_version = "Xcode 14.0";
constexpr auto scheme_format_version = "1.3";
constexpr auto scheme_last_upgrade_version = "1500";
// Enable every standard build action for the sources and frameworks phases.
constexpr std::uint32_t all_build_actions_mask = 0x7FFFFFFFU;

// PBX objects use stable 96-bit hexadecimal identifiers, independent of the
// platform's implementation-defined std::hash. Detect collisions explicitly.
class ObjectIds {
  public:
  /// Return a stable PBX ID and reject collisions with any previously seen key.
  std::string get(const std::string &key) {
    auto first = hashing::fnv1a64_offset_basis;
    auto second = hashing::fnv1a32_offset_basis;
    for (const unsigned char c : key) {
      first = (first ^ c) * hashing::fnv1a64_prime;
      second = (second ^ c) * hashing::fnv1a32_prime;
    }

    std::ostringstream value;
    value << std::uppercase << std::hex << std::setfill('0')
          << std::setw(hash64_hex_digits) << first
          << std::setw(hash32_hex_digits) << second;
    const auto id = value.str();
    if (const auto [entry, inserted] = keys_.emplace(id, key);
        !inserted && entry->second != key)
      throw std::runtime_error("Xcode object identifier collision");

    return id;
  }

  private:
  std::map<std::string, std::string> keys_;
};

// Always quote OpenStep strings. Control bytes and build-setting substitutions
// in user input are rejected instead of silently changing literal filenames.
void validate_literal(const std::string &value) {
  for (const unsigned char c : value) {
    if (c < encoding::first_printable_ascii || c == encoding::ascii_delete)
      throw std::runtime_error(
          "Xcode paths and arguments cannot contain control characters");
  }

  if (value.find("$(") != std::string::npos ||
      value.find("${") != std::string::npos)
    throw std::runtime_error(
        "Xcode paths and arguments cannot contain build-setting expressions");
}

/// Escape OpenStep string syntax; validation is separate for internal
/// expressions.
std::string quoted(const std::string &value) {
  std::string result = "\"";
  for (const char c : value) {
    if (c == '\\' || c == '"')
      result += '\\';

    result += c;
  }

  return result + '"';
}

/// Validate user-controlled text before serializing it as an OpenStep literal.
std::string literal(const std::string &value) {
  validate_literal(value);
  return quoted(value);
}

/// Escape validated literal text for shared-scheme XML attributes.
std::string xml(const std::string &value) {
  validate_literal(value);
  std::string result;
  for (const char c : value) {
    switch (c) {
    case '&':
      result += "&amp;";
      break;
    case '<':
      result += "&lt;";
      break;
    case '>':
      result += "&gt;";
      break;
    case '"':
      result += "&quot;";
      break;
    case '\'':
      result += "&apos;";
      break;
    default:
      result += c;
    }
  }

  return result;
}

/// Keep the generated IDE bundle inside the selected profile's build directory.
fs::path project_path(const Graph &graph) {
  return graph.project.build / (graph.project.name + ".xcodeproj");
}

/// Separate application/test target names even when their manifest names match.
std::string target_name(const Target &target, const size_t /*index*/) {
  // Manifest project names exclude '.', so tests cannot shadow the main app.
  const auto name = target.name.find("::") == std::string::npos
                        ? target.name
                        : target.project_name + "." + target.local_name;
  return target.is_test ? "test." + name : name;
}

/// Namespace PBX object identity by graph target index.
std::string target_key(const size_t index) {
  return "target:" + std::to_string(index);
}

/// Recognize supported Apple compiler locations; this is not a version probe.
bool apple_clang(const std::string &compiler) {
  if (compiler.empty())
    return true;

  const fs::path path(compiler);
  if (const auto name = path.filename().string();
      name != "clang" && name != "clang++")
    return false;

  if (const auto parent = path.parent_path().generic_string();
      parent == "/usr/bin") {
    return true;
  } else {
    constexpr std::string_view suffix =
        "/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin";
    return parent.size() >= suffix.size() &&
           parent.compare(parent.size() - suffix.size(),
                          suffix.size(),
                          suffix) == 0;
  }
}

/// Refuse compiler substitutions that native Xcode targets cannot honor.
void validate_toolchain(const Graph &graph) {
  if (graph.toolchain.msvc || !apple_clang(graph.toolchain.cc) ||
      !apple_clang(graph.toolchain.cxx))
    throw std::runtime_error(
        "Xcode requires Apple's Clang toolchain; select --cc /usr/bin/clang "
        "and --cxx /usr/bin/clang++, or use Ninja/Make for another compiler");

  if (graph.targets.empty())
    throw std::runtime_error(
        "cannot generate an Xcode project without targets");
}

// PBXBuildFile.COMPILER_FLAGS is a command-line string, unlike compilation
// database argument arrays. Escape each argument before OpenStep serialization.
std::string flags(const std::vector<std::string> &arguments) {
  std::string result;
  for (const auto &argument : arguments) {
    validate_literal(argument);
    if (!result.empty())
      result += ' ';

    result += '"';
    for (const char c : argument) {
      if (c == '\\' || c == '"')
        result += '\\';

      result += c;
    }

    result += '"';
  }

  return result;
}

/// Preserve semantic flags while leaving object/depfile scheduling to Xcode.
std::vector<std::string> compile_flags(const Unit &unit) {
  std::vector<std::string> result;
  for (size_t i = 1; i < unit.arguments.size(); ++i) {
    const auto &argument = unit.arguments[i];
    if (argument == "-o" || argument == "-MF" || argument == "-MT" ||
        argument == "-MQ" || argument == "-MJ") {
      if (++i == unit.arguments.size())
        throw std::runtime_error("incomplete Xcode compiler argument: " +
                                 argument);

      continue;
    }

    if (argument == "-c" || argument == "-MMD" || argument == "-MD" ||
        argument == "-MP" || argument == "-MG" ||
        argument == unit.source.string() || argument == unit.object.string())
      continue;

    if (argument.rfind("-MF", 0) == 0 || argument.rfind("-MT", 0) == 0 ||
        argument.rfind("-MQ", 0) == 0 || argument.rfind("-MJ", 0) == 0)
      continue;

    result.push_back(argument);
  }

  return result;
}

/// Remove graph object/output paths because Xcode supplies its own
/// intermediates.
std::vector<std::string> link_flags(const Target &target) {
  if (target.type == TargetType::StaticLibrary)
    return {};

  std::set<std::string> objects;
  for (const auto &unit : target.units)
    objects.insert(unit.object.string());

  std::vector<std::string> result;
  for (size_t i = 1; i < target.link.size(); ++i) {
    const auto &argument = target.link[i];
    if (argument == "-o") {
      if (++i == target.link.size())
        throw std::runtime_error("incomplete Xcode linker output argument");

      continue;
    }

    if (!objects.count(argument))
      result.push_back(argument);
  }

  return result;
}

/// Serialize a build-setting list through both command-fragment and plist
/// layers.
void list(std::ostream &out,
          const std::string &name,
          const std::vector<std::string> &values) {
  out << "\t\t\t\t" << name << " = (\n";
  for (const auto &value : values)
    // Xcode parses list entries as command-line fragments after parsing the
    // plist. Preserve whitespace and literal quotes through both layers.
    out << "\t\t\t\t\t" << literal(flags({value})) << ",\n";

  out << "\t\t\t\t);\n";
}

/// Match standards, warnings, output paths and tool selections to the Clutch
/// graph.
void target_settings(std::ostream &out,
                     const Graph &graph,
                     const Target &target,
                     const Options &options) {
  const auto &p = graph.project;
  out << "\t\t\t\tPRODUCT_NAME = " << literal(target.output.filename().string())
      << ";\n"
      << "\t\t\t\tEXECUTABLE_NAME = \"$(PRODUCT_NAME)\";\n"
      << "\t\t\t\tEXECUTABLE_PREFIX = \"\";\n"
      << "\t\t\t\tEXECUTABLE_SUFFIX = \"\";\n"
      << "\t\t\t\tCONFIGURATION_BUILD_DIR = "
      << literal(target.output.parent_path().generic_string()) << ";\n"
      << "\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = "
      << literal("c++" + target.cxx_standard) << ";\n"
      << "\t\t\t\tCLANG_CXX_LIBRARY = \"libc++\";\n"
      << "\t\t\t\tGCC_C_LANGUAGE_STANDARD = "
      << literal("c" + target.c_standard) << ";\n"
      << "\t\t\t\tGCC_OPTIMIZATION_LEVEL = "
      << literal(p.optimize == "speed"  ? "3"
                 : p.optimize == "size" ? "s"
                                        : "0")
      << ";\n"
      << "\t\t\t\tGCC_GENERATE_DEBUGGING_SYMBOLS = "
      << (options.release ? "NO" : "YES") << ";\n"
      << "\t\t\t\tGCC_WARN_INHIBIT_ALL_WARNINGS = "
      << (p.warnings == "none" ? "YES" : "NO") << ";\n"
      << "\t\t\t\tDEBUG_INFORMATION_FORMAT = dwarf;\n"
      << "\t\t\t\tCOPY_PHASE_STRIP = NO;\n"
      << "\t\t\t\tSTRIP_INSTALLED_PRODUCT = NO;\n"
      << "\t\t\t\tSKIP_INSTALL = YES;\n";

  std::vector<std::string> includes;
  includes.reserve(target.includes.size());
  for (const auto &include : target.includes)
    includes.push_back(include.generic_string());

  list(out, "HEADER_SEARCH_PATHS", includes);
  list(out, "GCC_PREPROCESSOR_DEFINITIONS", target.defines);
  list(out, "OTHER_LDFLAGS", link_flags(target));

  if (!graph.toolchain.cc.empty())
    out << "\t\t\t\tCC = " << literal(graph.toolchain.cc) << ";\n";

  if (!graph.toolchain.cxx.empty())
    out << "\t\t\t\tCXX = " << literal(graph.toolchain.cxx) << ";\n";

  if (!target.link.empty() && target.type != TargetType::StaticLibrary)
    out << "\t\t\t\tLD = " << literal(target.link.front()) << ";\n"
        << "\t\t\t\tLDPLUSPLUS = " << literal(target.link.front()) << ";\n";
}

/// Create a shared build/run scheme with the source root as its working
/// directory.
std::string scheme(const Graph &graph,
                   const Target &target,
                   const size_t index,
                   const std::string &id,
                   const std::string &configuration) {
  const auto reference =
      "<BuildableReference BuildableIdentifier=\"primary\" "
      "BlueprintIdentifier=\"" +
      xml(id) + "\" BuildableName=\"" + xml(target.output.filename().string()) +
      "\" BlueprintName=\"" + xml(target_name(target, index)) +
      "\" ReferencedContainer=\"container:" +
      xml(project_path(graph).filename().string()) + "\"/>";
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<Scheme LastUpgradeVersion=\"" << scheme_last_upgrade_version
      << "\" version=\"" << scheme_format_version << "\">\n"
      << "  <BuildAction parallelizeBuildables=\"YES\" "
         "buildImplicitDependencies=\"YES\">\n"
      << "    <BuildActionEntries>\n"
      << "      <BuildActionEntry buildForTesting=\"YES\" "
         "buildForRunning=\"YES\" buildForProfiling=\"YES\" "
         "buildForArchiving=\"YES\" buildForAnalyzing=\"YES\">\n"
      << "        " << reference << "\n"
      << "      </BuildActionEntry>\n"
      << "    </BuildActionEntries>\n"
      << "  </BuildAction>\n";
  if (target.type == TargetType::Executable)
    out << "  <LaunchAction buildConfiguration=\"" << xml(configuration)
        << "\" "
           "selectedDebuggerIdentifier=\"Xcode.DebuggerFoundation.Debugger."
           "LLDB\" "
           "selectedLauncherIdentifier=\"Xcode.IDEFoundation.Launcher.LLDB\" "
           "launchStyle=\"0\" useCustomWorkingDirectory=\"YES\" "
           "customWorkingDirectory=\""
        << xml(target.source_root.generic_string()) << "\">\n"
        << "    <BuildableProductRunnable runnableDebuggingMode=\"0\">\n"
        << "      " << reference << "\n"
        << "    </BuildableProductRunnable>\n"
        << "  </LaunchAction>\n";
  out << "  <AnalyzeAction buildConfiguration=\"" << xml(configuration)
      << "\"/>\n"
      << "</Scheme>\n";
  return out.str();
}
}  // namespace

/// Serialize native targets and shared schemes without invoking xcodebuild.
void generate_xcode(const Graph &graph,
                    const Options &options,
                    const fs::path &clutch_executable) {
  // This project consumes the explicitly configured graph. Native compilation
  // does not recursively invoke Clutch or another generator.
  (void)clutch_executable;
  validate_toolchain(graph);
  ObjectIds ids;
  const auto project = ids.get("project");
  const auto main_group = ids.get("group:main");
  const auto products = ids.get("group:products");
  const auto configuration = std::string(options.release ? "Release" : "Debug");
  std::ostringstream out;
  out << "// !$*UTF8*$!\n"
      << "// Generated by Clutch. Configure again to change build settings.\n"
      << "{\n\tarchiveVersion = " << pbx_archive_version
      << ";\n\tclasses = {};\n\tobjectVersion = " << pbx_object_version << ";\n"
      << "\tobjects = {\n";

  // Each target owns source references, build phases, a product and
  // configuration.
  for (size_t index = 0; index < graph.targets.size(); ++index) {
    const auto &target = graph.targets[index];
    const auto key = target_key(index);
    const auto *const product_type =
        target.type == TargetType::StaticLibrary
            ? "com.apple.product-type.library.static"
        : target.type == TargetType::SharedLibrary
            ? "com.apple.product-type.library.dynamic"
            : "com.apple.product-type.tool";
    const auto *const file_type = target.type == TargetType::StaticLibrary
                                      ? "archive.ar"
                                  : target.type == TargetType::SharedLibrary
                                      ? "compiled.mach-o.dylib"
                                      : "compiled.mach-o.executable";
    for (const auto dependency : target.dependencies)
      out << "\t\t"
          << ids.get(key + ":dependency:" + std::to_string(dependency))
          << " = { isa = PBXTargetDependency; target = "
          << ids.get(target_key(dependency)) << "; };\n";
    for (const auto &unit : target.units) {
      const auto source_key = key + ":source:" + unit.source.generic_string();
      out << "\t\t" << ids.get(source_key + ":file") << " = {\n"
          << "\t\t\tisa = PBXFileReference;\n"
          << "\t\t\tlastKnownFileType = "
          << (unit.source.extension() == ".c" ? "sourcecode.c.c"
                                              : "sourcecode.cpp.cpp")
          << ";\n"
          << "\t\t\tname = " << literal(unit.source.filename().string())
          << ";\n"
          << "\t\t\tpath = " << literal(unit.source.generic_string()) << ";\n"
          << "\t\t\tsourceTree = \"<absolute>\";\n\t\t};\n"
          << "\t\t" << ids.get(source_key + ":build") << " = {\n"
          << "\t\t\tisa = PBXBuildFile;\n"
          << "\t\t\tfileRef = " << ids.get(source_key + ":file") << ";\n"
          << "\t\t\tsettings = { COMPILER_FLAGS = "
          << literal(flags(compile_flags(unit))) << "; };\n\t\t};\n";
    }

    out << "\t\t" << ids.get(key + ":sources") << " = {\n"
        << "\t\t\tisa = PBXSourcesBuildPhase;\n"
        << "\t\t\tbuildActionMask = " << all_build_actions_mask << ";\n"
        << "\t\t\tfiles = (\n";
    for (const auto &unit : target.units)
      out << "\t\t\t\t"
          << ids.get(key + ":source:" + unit.source.generic_string() + ":build")
          << ",\n";

    out << "\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
        << "\t\t" << ids.get(key + ":frameworks") << " = {\n"
        << "\t\t\tisa = PBXFrameworksBuildPhase;\n"
        << "\t\t\tbuildActionMask = " << all_build_actions_mask << ";\n"
        << "\t\t\tfiles = ();\n"
        << "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
        << "\t\t" << ids.get(key + ":group") << " = {\n"
        << "\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n";
    for (const auto &unit : target.units)
      out << "\t\t\t\t"
          << ids.get(key + ":source:" + unit.source.generic_string() + ":file")
          << ",\n";

    out << "\t\t\t);\n\t\t\tname = " << literal(target_name(target, index))
        << ";\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
        << "\t\t" << ids.get(key + ":product") << " = {\n"
        << "\t\t\tisa = PBXFileReference;\n"
        << "\t\t\texplicitFileType = " << quoted(file_type) << ";\n"
        << "\t\t\tincludeInIndex = 0;\n"
        << "\t\t\tpath = " << literal(target.output.filename().string())
        << ";\n\t\t\tsourceTree = BUILT_PRODUCTS_DIR;\n\t\t};\n"
        << "\t\t" << ids.get(key) << " = {\n"
        << "\t\t\tisa = PBXNativeTarget;\n"
        << "\t\t\tbuildConfigurationList = " << ids.get(key + ":config-list")
        << ";\n\t\t\tbuildPhases = (" << ids.get(key + ":sources") << ", "
        << ids.get(key + ":frameworks") << ");\n"
        << "\t\t\tbuildRules = ();\n\t\t\tdependencies = (";
    for (const auto dependency : target.dependencies)
      out << ids.get(key + ":dependency:" + std::to_string(dependency)) << ',';
    out << ");\n\t\t\tname = " << literal(target_name(target, index)) << ";\n"
        << "\t\t\tproductName = " << literal(target.output.filename().string())
        << ";\n\t\t\tproductReference = " << ids.get(key + ":product")
        << ";\n\t\t\tproductType = " << quoted(product_type) << ";\n\t\t};\n"
        << "\t\t" << ids.get(key + ":config") << " = {\n"
        << "\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n";
    target_settings(out, graph, target, options);
    out << "\t\t\t};\n\t\t\tname = " << quoted(configuration) << ";\n\t\t};\n"
        << "\t\t" << ids.get(key + ":config-list") << " = {\n"
        << "\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = ("
        << ids.get(key + ":config") << ");\n"
        << "\t\t\tdefaultConfigurationIsVisible = 0;\n"
        << "\t\t\tdefaultConfigurationName = " << quoted(configuration)
        << ";\n\t\t};\n";
  }

  // Connect target groups/products to the root project and shared macOS
  // settings.
  out << "\t\t" << main_group << " = {\n\t\t\tisa = PBXGroup;\n"
      << "\t\t\tchildren = (\n";
  for (size_t index = 0; index < graph.targets.size(); ++index)
    out << "\t\t\t\t" << ids.get(target_key(index) + ":group") << ",\n";

  out << "\t\t\t\t" << products << ",\n\t\t\t);\n"
      << "\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
      << "\t\t" << products << " = {\n\t\t\tisa = PBXGroup;\n"
      << "\t\t\tchildren = (\n";
  for (size_t index = 0; index < graph.targets.size(); ++index)
    out << "\t\t\t\t" << ids.get(target_key(index) + ":product") << ",\n";

  out << "\t\t\t);\n\t\t\tname = Products;\n"
      << "\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
      << "\t\t" << ids.get("project:config") << " = {\n"
      << "\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n"
      << "\t\t\t\tSDKROOT = macosx;\n"
      << "\t\t\t\tARCHS = \"$(NATIVE_ARCH_ACTUAL)\";\n"
      << "\t\t\t\tONLY_ACTIVE_ARCH = YES;\n"
      << "\t\t\t\tCODE_SIGNING_ALLOWED = NO;\n"
      << "\t\t\t\tCODE_SIGNING_REQUIRED = NO;\n"
      << "\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;\n"
      << "\t\t\t\tUSE_HEADERMAP = NO;\n"
      << "\t\t\t\tCLANG_ENABLE_MODULES = NO;\n"
      << "\t\t\t\tSYMROOT = " << literal(graph.project.build.generic_string())
      << ";\n\t\t\t\tOBJROOT = "
      << literal((graph.project.build / "xcode-obj").generic_string())
      << ";\n\t\t\t};\n\t\t\tname = " << quoted(configuration) << ";\n\t\t};\n"
      << "\t\t" << ids.get("project:config-list") << " = {\n"
      << "\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = ("
      << ids.get("project:config") << ");\n"
      << "\t\t\tdefaultConfigurationIsVisible = 0;\n"
      << "\t\t\tdefaultConfigurationName = " << quoted(configuration)
      << ";\n\t\t};\n"
      << "\t\t" << project << " = {\n\t\t\tisa = PBXProject;\n"
      << "\t\t\tattributes = { BuildIndependentTargetsInParallel = YES; };\n"
      << "\t\t\tbuildConfigurationList = " << ids.get("project:config-list")
      << ";\n\t\t\tcompatibilityVersion = " << quoted(pbx_compatibility_version)
      << ";\n"
      << "\t\t\tdevelopmentRegion = en;\n"
      << "\t\t\thasScannedForEncodings = 0;\n"
      << "\t\t\tknownRegions = (en, Base);\n"
      << "\t\t\tmainGroup = " << main_group << ";\n"
      << "\t\t\tproductRefGroup = " << products << ";\n"
      << "\t\t\tprojectDirPath = "
      << literal(graph.project.root.generic_string())
      << ";\n\t\t\tprojectRoot = \"\";\n\t\t\ttargets = (\n";
  for (size_t index = 0; index < graph.targets.size(); ++index)
    out << "\t\t\t\t" << ids.get(target_key(index)) << ",\n";

  out << "\t\t\t);\n\t\t};\n\t};\n\trootObject = " << project << ";\n}\n";
  write_if_changed(project_path(graph) / "project.pbxproj", out.str());

  for (size_t index = 0; index < graph.targets.size(); ++index) {
    const auto &target = graph.targets[index];
    // Shared schemes are available immediately to IDE and command-line
    // consumers.
    const auto path = project_path(graph) / "xcshareddata" / "xcschemes" /
                      (target_name(target, index) + ".xcscheme");
    write_if_changed(path,
                     scheme(graph,
                            target,
                            index,
                            ids.get(target_key(index)),
                            configuration));
  }
}

/// Select native targets once each and request the active profile's build
/// action.
std::vector<std::string>
xcode_build_command(const Graph &graph,
                    const Options &options,
                    const std::vector<size_t> &target_indices) {
  if (target_indices.empty())
    throw std::runtime_error("no Xcode build targets selected");

  std::vector<std::string> arguments{graph.backend.executable,
                                     "-project",
                                     project_path(graph).string(),
                                     "-configuration",
                                     options.release ? "Release" : "Debug"};
  std::set<size_t> selected;
  for (const auto index : target_indices) {
    if (index >= graph.targets.size())
      throw std::runtime_error("invalid Xcode build target index");

    if (selected.insert(index).second)
      arguments.insert(arguments.end(),
                       {"-target", target_name(graph.targets[index], index)});
  }

  if (options.jobs > 0)
    arguments.insert(arguments.end(), {"-jobs", std::to_string(options.jobs)});

  arguments.emplace_back("build");
  return arguments;
}
}  // namespace clutch
