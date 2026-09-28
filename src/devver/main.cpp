#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace color {

constexpr const char *reset = "\033[0m";
constexpr const char *red = "\033[31m";
constexpr const char *green = "\033[32m";
constexpr const char *cyan = "\033[36m";
constexpr const char *blue = "\033[34m";
constexpr const char *dim = "\033[2m";

} // namespace color

// ============================================================
// Command result
// ============================================================

struct CommandResult {
  bool success;
  std::string output;
};

// ============================================================
// Tool configuration
// ============================================================

struct Tool {
  std::string name;
  std::string command;
  std::vector<std::string> args;
  bool show_version;
};

// ============================================================
// Check whether command exists
// ============================================================

bool command_exists(const std::string &command) {
  const char *path_env = std::getenv("PATH");

  if (path_env == nullptr) {
    return false;
  }

  std::stringstream path_stream(path_env);
  std::string directory;

  while (std::getline(path_stream, directory, ':')) {
    if (directory.empty()) {
      directory = ".";
    }

    std::string full_path = directory + "/" + command;

    if (access(full_path.c_str(), X_OK) == 0) {
      return true;
    }
  }

  return false;
}

// ============================================================
// Execute command
// ============================================================

CommandResult execute_command(const std::string &command,
                              const std::vector<std::string> &args) {
  std::string cmd = command;

  for (const auto &arg : args) {
    cmd += " ";
    cmd += arg;
  }

  cmd += " 2>&1";

  FILE *pipe = popen(cmd.c_str(), "r");

  if (pipe == nullptr) {
    return {false, "failed to execute command"};
  }

  std::array<char, 256> buffer{};
  std::string output;

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);

  if (status == -1) {
    return {false, output};
  }

  if (WIFEXITED(status)) {
    const int exit_code = WEXITSTATUS(status);

    return {exit_code == 0, output};
  }

  return {false, output};
}

// ============================================================
// Get first line
// ============================================================

std::string first_line(const std::string &text) {
  std::istringstream stream(text);

  std::string line;

  if (std::getline(stream, line)) {
    return line;
  }

  return {};
}

std::string extract_version(const std::string &output) {
  static const std::regex version_regex(
      R"((?:version\s+)?([0-9]+\.[0-9]+(?:\.[0-9]+)?(?:[-+][0-9A-Za-z.-]+)?))",
      std::regex::icase);

  std::smatch match;

  if (std::regex_search(output, match, version_regex)) {
    return match[1].str();
  }

  return first_line(output);
}

// ============================================================
// Tool constructors
// ============================================================

Tool version_tool(const std::string &name, const std::string &command,
                  std::vector<std::string> args = {"--version"}) {
  return Tool{name, command, std::move(args), true};
}

Tool exists_tool(const std::string &name, const std::string &command) {
  return Tool{name, command, {}, false};
}

// ============================================================
// Check tool
// ============================================================

void check_tool(const Tool &tool) {
  std::cout << std::left << std::setw(20) << tool.name;

  // --------------------------------------------------------
  // Not installed
  // --------------------------------------------------------

  if (!command_exists(tool.command)) {
    std::cout << color::red << "not installed" << color::reset << '\n';

    return;
  }

  // --------------------------------------------------------
  // Only existence check
  // --------------------------------------------------------

  if (!tool.show_version) {
    std::cout << color::green << "available" << color::reset << '\n';

    return;
  }

  // --------------------------------------------------------
  // Version check
  // --------------------------------------------------------

  const CommandResult result = execute_command(tool.command, tool.args);

  if (result.success) {
    std::cout << color::green << extract_version(result.output) << color::reset
              << '\n';
  } else {
    std::cout << color::red << "error" << color::reset << '\n';
  }
}

// ============================================================
// Section header
// ============================================================

void section(const std::string &title) {
  std::cout << '\n' << color::blue << title << color::reset << '\n';

  std::cout << color::dim << "----------------------------------------------"
            << color::reset << '\n';
}

// ============================================================
// JetBrains Toolbox
// ============================================================

namespace fs = std::filesystem;

struct JetBrainsIDE {
  std::string name;
  fs::path path;
  std::string version;
};

// ------------------------------------------------------------
// Read JetBrains build version
// ------------------------------------------------------------

std::string read_jetbrains_version(const fs::path &ide_path) {
  const fs::path build_file = ide_path / "build.txt";

  std::ifstream file(build_file);

  if (!file.is_open()) {
    return "unknown";
  }

  std::string line;

  if (std::getline(file, line)) {
    return line;
  }

  return "unknown";
}

// ------------------------------------------------------------
// Get human-readable IDE name
// ------------------------------------------------------------

std::string jetbrains_name(const std::string &directory_name) {
  if (directory_name.find("clion") != std::string::npos) {
    return "CLion";
  }

  if (directory_name.find("idea") != std::string::npos) {
    return "IntelliJ IDEA";
  }

  if (directory_name.find("pycharm") != std::string::npos) {
    return "PyCharm";
  }

  if (directory_name.find("rustrover") != std::string::npos) {
    return "RustRover";
  }

  if (directory_name.find("webstorm") != std::string::npos) {
    return "WebStorm";
  }

  if (directory_name.find("datagrip") != std::string::npos) {
    return "DataGrip";
  }

  if (directory_name.find("goland") != std::string::npos) {
    return "GoLand";
  }

  if (directory_name.find("phpstorm") != std::string::npos) {
    return "PhpStorm";
  }

  if (directory_name.find("rubymine") != std::string::npos) {
    return "RubyMine";
  }

  if (directory_name.find("rider") != std::string::npos) {
    return "Rider";
  }

  if (directory_name.find("appcode") != std::string::npos) {
    return "AppCode";
  }

  if (directory_name.find("aqua") != std::string::npos) {
    return "Aqua";
  }

  return {};
}

// ------------------------------------------------------------
// Find JetBrains IDEs installed by Toolbox
// ------------------------------------------------------------

std::vector<JetBrainsIDE> find_jetbrains_ides() {
  std::vector<JetBrainsIDE> result;

  const char *home_env = std::getenv("HOME");

  if (home_env == nullptr) {
    return result;
  }

  const fs::path home(home_env);

  const std::vector<fs::path> toolbox_paths{
      home / ".local/share/JetBrains/Toolbox/apps",
      home / ".local/share/JetBrains/Toolbox/apps"};

  fs::path toolbox_apps;

  for (const auto &path : toolbox_paths) {
    if (fs::exists(path) && fs::is_directory(path)) {
      toolbox_apps = path;
      break;
    }
  }

  if (toolbox_apps.empty()) {
    return result;
  }

  try {
    for (const auto &ide_directory : fs::directory_iterator(toolbox_apps)) {

      if (!ide_directory.is_directory()) {
        continue;
      }

      const std::string directory_name =
          ide_directory.path().filename().string();

      const std::string ide_name = jetbrains_name(directory_name);

      if (ide_name.empty()) {
        continue;
      }

      // Toolbox normally stores versions inside
      // the IDE directory.
      for (const auto &version_directory :
           fs::directory_iterator(ide_directory.path())) {

        if (!version_directory.is_directory()) {
          continue;
        }

        const fs::path ide_path = version_directory.path();

        const fs::path build_file = ide_path / "build.txt";

        if (!fs::exists(build_file)) {
          continue;
        }

        const std::string version = read_jetbrains_version(ide_path);

        result.push_back({ide_name, ide_path, version});
      }
    }
  } catch (const fs::filesystem_error &) {
    return result;
  }

  return result;
}

// ------------------------------------------------------------
// Print JetBrains IDEs
// ------------------------------------------------------------

void check_jetbrains_ides() {
  section("JETBRAINS IDE");

  const auto ides = find_jetbrains_ides();

  if (ides.empty()) {
    std::cout << color::red << "No JetBrains IDEs found" << color::reset
              << '\n';

    return;
  }

  for (const auto &ide : ides) {

    std::cout << std::left << std::setw(20) << ide.name

              << color::green << ide.version << color::reset

              << '\n';
  }
}

// ============================================================
// Main
// ============================================================

int main() {
  std::cout << color::cyan << "Development Environment Version Checker"
            << color::reset << '\n';

  // ========================================================
  // C / C++
  // ========================================================

  section("C / C++");

  const std::vector<Tool> c_cpp_tools{

      version_tool("GCC", "gcc"),
      version_tool("G++", "g++"),

      version_tool("Clang", "clang"),
      version_tool("Clang++", "clang++"),

      version_tool("clang-format", "clang-format"),
      version_tool("clang-tidy", "clang-tidy"),

      version_tool("cppcheck", "cppcheck"),

      version_tool("CMake", "cmake"),
      version_tool("Ninja", "ninja"),
      version_tool("Make", "make"),

      version_tool("pkg-config", "pkg-config")};

  for (const auto &tool : c_cpp_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Build systems
  // ========================================================

  section("BUILD SYSTEMS");

  const std::vector<Tool> build_tools{

      version_tool("Meson", "meson"),
      version_tool("Autoconf", "autoconf"),
      version_tool("Automake", "automake"),
      version_tool("Libtoolize", "libtoolize"),

      version_tool("Bison", "bison"),
      version_tool("Flex", "flex")};

  for (const auto &tool : build_tools) {
    check_tool(tool);
  }

  // ========================================================
  // C++ package managers
  // ========================================================

  section("C++ PACKAGE MANAGERS");

  const std::vector<Tool> cpp_package_tools{

      version_tool("Conan", "conan"), version_tool("Vcpkg", "vcpkg")};

  for (const auto &tool : cpp_package_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Rust
  // ========================================================

  section("RUST");

  const std::vector<Tool> rust_tools{

      version_tool("Rustc", "rustc"), version_tool("Cargo", "cargo"),
      version_tool("Rustfmt", "rustfmt"),

      version_tool("Cargo Clippy", "cargo", {"clippy", "--version"}),

      version_tool("Rust Analyzer", "rust-analyzer")};

  for (const auto &tool : rust_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Python
  // ========================================================

  section("PYTHON");

  const std::vector<Tool> python_tools{

      version_tool("Python", "python"), version_tool("Python3", "python3"),

      version_tool("Pip", "pip"),       version_tool("Pip3", "pip3"),

      version_tool("UV", "uv"),         version_tool("Poetry", "poetry"),
      version_tool("Pipenv", "pipenv")};

  for (const auto &tool : python_tools) {
    check_tool(tool);
  }

  // ========================================================
  // JavaScript / TypeScript
  // ========================================================

  section("JAVASCRIPT / TYPESCRIPT");

  const std::vector<Tool> javascript_tools{

      version_tool("Node", "node"),
      version_tool("NPM", "npm"),
      version_tool("NPX", "npx"),

      version_tool("Yarn", "yarn"),
      version_tool("PNPM", "pnpm"),
      version_tool("Bun", "bun"),
      version_tool("Deno", "deno"),

      version_tool("TypeScript", "tsc", {"--version"})};

  for (const auto &tool : javascript_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Go
  // ========================================================

  section("GO");

  const std::vector<Tool> go_tools{

      // Go does NOT use --version
      version_tool("Go", "go", {"version"}),

      // gofmt does not provide a useful --version flag
      exists_tool("Gofmt", "gofmt")};

  for (const auto &tool : go_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Java / JVM
  // ========================================================

  section("JAVA / JVM");

  const std::vector<Tool> java_tools{

      version_tool("Java", "java"), version_tool("Javac", "javac"),

      version_tool("Maven", "mvn"), version_tool("Gradle", "gradle")};

  for (const auto &tool : java_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Version control
  // ========================================================

  section("VERSION CONTROL");

  const std::vector<Tool> vcs_tools{

      version_tool("Git", "git"), version_tool("Git LFS", "git-lfs"),

      version_tool("SVN", "svn"), version_tool("Mercurial", "hg")};

  for (const auto &tool : vcs_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Containers
  // ========================================================

  section("CONTAINERS");

  const std::vector<Tool> container_tools{

      version_tool("Docker", "docker"),
      version_tool("Docker Compose", "docker-compose"),

      version_tool("Podman", "podman"), version_tool("Buildah", "buildah"),
      version_tool("Skopeo", "skopeo")};

  for (const auto &tool : container_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Databases
  // ========================================================

  section("DATABASES");

  const std::vector<Tool> database_tools{

      version_tool("PostgreSQL", "psql"), version_tool("MySQL", "mysql"),
      version_tool("MariaDB", "mariadb"),

      version_tool("SQLite", "sqlite3"),

      version_tool("Redis", "redis-cli"), version_tool("MongoDB", "mongosh")};

  for (const auto &tool : database_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Debug / Profiling
  // ========================================================

  section("DEBUG / PROFILING");

  const std::vector<Tool> debug_tools{

      version_tool("GDB", "gdb"),           version_tool("LLDB", "lldb"),
      version_tool("Valgrind", "valgrind"),

      version_tool("Perf", "perf"),         version_tool("Strace", "strace"),
      version_tool("Ltrace", "ltrace")};

  for (const auto &tool : debug_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Editors / IDE
  // ========================================================

  section("EDITORS / IDE");

  const std::vector<Tool> editor_tools{

      version_tool("VS Code", "code"), version_tool("Zed", "zeditor"),

      version_tool("Neovim", "nvim"), version_tool("Vim", "vim"),
      version_tool("Emacs", "emacs")};

  for (const auto &tool : editor_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Documentation / Tools
  // ========================================================

  section("DOCUMENTATION / TOOLS");

  const std::vector<Tool> documentation_tools{

      version_tool("Doxygen", "doxygen"), version_tool("Graphviz", "dot"),
      version_tool("Sphinx", "sphinx-build")};

  for (const auto &tool : documentation_tools) {
    check_tool(tool);
  }

  // ========================================================
  // Finish
  // ========================================================

  std::cout << '\n';

  return 0;
}