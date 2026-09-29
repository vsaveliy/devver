#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// COLORS
// ============================================================

namespace color {

constexpr std::string_view reset = "\033[0m";
constexpr std::string_view bold = "\033[1m";
constexpr std::string_view dim = "\033[2m";

constexpr std::string_view red = "\033[31m";
constexpr std::string_view green = "\033[32m";
constexpr std::string_view yellow = "\033[33m";
constexpr std::string_view cyan = "\033[36m";
constexpr std::string_view blue = "\033[34m";

} // namespace color

// ============================================================
// COMMAND RESULT
// ============================================================

struct CommandResult {
  bool success{false};
  std::string output;
};

// ============================================================
// COMMAND RUNNER
// ============================================================

class CommandRunner {
public:
  static constexpr int timeout_seconds = 3;

  [[nodiscard]]
  static bool exists(std::string_view command) {
    const char *path_environment = std::getenv("PATH");

    if (path_environment == nullptr) {
      return false;
    }

    const std::string path{path_environment};

    std::size_t begin = 0;

    while (begin <= path.size()) {
      const std::size_t end = path.find(':', begin);

      const std::string_view directory =
          end == std::string::npos
              ? std::string_view{path}.substr(begin)
              : std::string_view{path}.substr(begin, end - begin);

      const fs::path executable = directory.empty()
                                      ? fs::path{"."} / command
                                      : fs::path{directory} / command;

      if (::access(executable.c_str(), X_OK) == 0) {
        return true;
      }

      if (end == std::string::npos) {
        break;
      }

      begin = end + 1;
    }

    return false;
  }

  [[nodiscard]]
  static CommandResult run(std::string_view command,
                           const std::vector<std::string> &arguments = {}) {

    int pipe_fds[2]{};

    if (::pipe(pipe_fds) == -1) {
      return {};
    }

    const pid_t pid = ::fork();

    if (pid == -1) {
      ::close(pipe_fds[0]);
      ::close(pipe_fds[1]);

      return {};
    }

    // ----------------------------------------------------
    // CHILD
    // ----------------------------------------------------

    if (pid == 0) {
      ::close(pipe_fds[0]);

      if (::dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
        ::_exit(127);
      }

      if (::dup2(pipe_fds[1], STDERR_FILENO) == -1) {
        ::_exit(127);
      }

      ::close(pipe_fds[1]);

      std::string command_copy{command};

      std::vector<std::string> argument_copies;
      argument_copies.reserve(arguments.size());

      for (const std::string &argument : arguments) {
        argument_copies.push_back(argument);
      }

      std::vector<char *> argv;
      argv.reserve(argument_copies.size() + 2);

      argv.push_back(command_copy.data());

      for (std::string &argument : argument_copies) {
        argv.push_back(argument.data());
      }

      argv.push_back(nullptr);

      ::execvp(command_copy.c_str(), argv.data());

      ::_exit(127);
    }

    // ----------------------------------------------------
    // PARENT
    // ----------------------------------------------------

    ::close(pipe_fds[1]);

    std::string output;

    std::array<char, 4096> buffer{};

    const auto start = std::chrono::steady_clock::now();

    bool timed_out = false;

    while (true) {
      const auto now = std::chrono::steady_clock::now();

      const auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

      if (elapsed >= timeout_seconds) {
        timed_out = true;
        break;
      }

      fd_set read_fds;

      FD_ZERO(&read_fds);
      FD_SET(pipe_fds[0], &read_fds);

      timeval timeout{};

      timeout.tv_sec = 0;
      timeout.tv_usec = 100'000;

      const int result =
          ::select(pipe_fds[0] + 1, &read_fds, nullptr, nullptr, &timeout);

      if (result == -1) {
        if (errno == EINTR) {
          continue;
        }

        break;
      }

      if (result == 0) {
        continue;
      }

      const ssize_t bytes_read =
          ::read(pipe_fds[0], buffer.data(), buffer.size());

      if (bytes_read > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(bytes_read));

        continue;
      }

      if (bytes_read == 0) {
        break;
      }

      if (errno == EINTR) {
        continue;
      }

      break;
    }

    if (timed_out) {
      ::kill(pid, SIGKILL);
    }

    ::close(pipe_fds[0]);

    int status{};

    while (::waitpid(pid, &status, 0) == -1) {
      if (errno != EINTR) {
        return {false, std::move(output)};
      }
    }

    if (timed_out) {
      return {false, std::move(output)};
    }

    if (!WIFEXITED(status)) {
      return {false, std::move(output)};
    }

    return {WEXITSTATUS(status) == 0, std::move(output)};
  }
};

// ============================================================
// VERSION PARSER
// ============================================================

class VersionParser {
public:
  [[nodiscard]]
  static std::string extract(const std::string &text) {

    static const std::regex version_pattern{
        R"((\d+\.\d+(?:\.\d+)*(?:[-+~][A-Za-z0-9._+-]+)?))"};

    std::smatch match;

    if (std::regex_search(text, match, version_pattern)) {
      return match.str(1);
    }

    return first_line(text);
  }

private:
  [[nodiscard]]
  static std::string first_line(const std::string &text) {

    const std::size_t position = text.find('\n');

    if (position == std::string::npos) {
      return text;
    }

    return text.substr(0, position);
  }
};

// ============================================================
// TOOL
// ============================================================

class Tool {
public:
  Tool(std::string name, std::string command,
       std::vector<std::string> arguments = {}, bool show_version = true)
      : name_(std::move(name)), command_(std::move(command)),
        arguments_(std::move(arguments)), show_version_(show_version) {}

  [[nodiscard]]
  std::string_view name() const noexcept {
    return name_;
  }

  [[nodiscard]]
  bool installed() const {
    return CommandRunner::exists(command_);
  }

  [[nodiscard]]
  std::string version() const {

    if (!show_version_) {
      return {};
    }

    const CommandResult result = CommandRunner::run(command_, arguments_);

    if (result.output.empty()) {
      return {};
    }

    return VersionParser::extract(result.output);
  }

private:
  std::string name_;
  std::string command_;
  std::vector<std::string> arguments_;
  bool show_version_{true};
};

// ============================================================
// TOOL FACTORY
// ============================================================

class Tools {
public:
  [[nodiscard]]
  static Tool version(std::string name, std::string command,
                      std::vector<std::string> arguments = {}) {

    return Tool{std::move(name), std::move(command), std::move(arguments),
                true};
  }

  [[nodiscard]]
  static Tool exists(std::string name, std::string command) {

    return Tool{std::move(name), std::move(command), {}, false};
  }
};

// ============================================================
// STATISTICS
// ============================================================

struct Statistics {
  std::size_t installed{0};
  std::size_t missing{0};

  [[nodiscard]]
  std::size_t total() const noexcept {
    return installed + missing;
  }

  Statistics &operator+=(const Statistics &other) noexcept {

    installed += other.installed;
    missing += other.missing;

    return *this;
  }
};

// ============================================================
// CONSOLE
// ============================================================

class Console {
public:
  static void banner() {

    std::cout << '\n'
              << color::bold << color::cyan
              << "╭──────────────────────────────────────────────╮\n"
              << "│                    DEVVER                    │\n"
              << "│       Development Environment Checker        │\n"
              << "╰──────────────────────────────────────────────╯\n"
              << color::reset << '\n';
  }

  static void section(std::string_view title) {

    std::cout << '\n'
              << color::bold << color::blue << "┌─ " << title << color::reset
              << '\n';
  }

  static void section_end() {

    std::cout << color::blue
              << "└──────────────────────────────────────────────"
              << color::reset << '\n';
  }

  static void tool(const Tool &tool, bool installed, std::string_view version) {

    constexpr int name_width = 25;

    std::cout << "│ ";

    if (installed) {
      std::cout << color::green << "✓" << color::reset;
    } else {
      std::cout << color::red << "✗" << color::reset;
    }

    std::cout << ' ';

    std::cout << std::left << std::setw(name_width) << tool.name();

    if (installed) {
      std::cout << color::green << (version.empty() ? "installed" : version)
                << color::reset;
    } else {
      std::cout << color::dim << "not installed" << color::reset;
    }

    std::cout << '\n';
  }

  static void jetbrains_tool(std::string_view name, std::string_view version) {

    constexpr int name_width = 25;

    std::cout << "│ " << color::green << "✓" << color::reset << ' ' << std::left
              << std::setw(name_width) << name << color::green << version
              << color::reset << '\n';
  }

  static void jetbrains_missing(std::string_view name) {

    constexpr int name_width = 25;

    std::cout << "│ " << color::red << "✗" << color::reset << ' ' << std::left
              << std::setw(name_width) << name << color::dim << "not installed"
              << color::reset << '\n';
  }

  static void no_jetbrains() {

    std::cout << "│ " << color::yellow << "No JetBrains IDEs found"
              << color::reset << '\n';
  }

  static void summary(const Statistics &statistics) {

    std::cout << '\n'
              << color::bold << "Summary" << color::reset << '\n'
              << '\n';

    std::cout << "  " << color::green << "✓ " << statistics.installed
              << " installed" << color::reset << '\n';

    std::cout << "  " << color::red << "✗ " << statistics.missing << " missing"
              << color::reset << '\n';

    std::cout << "  " << color::dim << "Total: " << statistics.total()
              << color::reset << '\n'
              << '\n';
  }
};

// ============================================================
// TOOL CHECKER
// ============================================================

class ToolChecker {
public:
  [[nodiscard]]
  static Statistics check(const std::vector<Tool> &tools) {

    Statistics statistics;

    for (const Tool &tool : tools) {

      const bool installed = tool.installed();

      if (installed) {

        ++statistics.installed;

        const std::string version = tool.version();

        Console::tool(tool, true, version);

      } else {

        ++statistics.missing;

        Console::tool(tool, false, {});
      }
    }

    return statistics;
  }
};

// ============================================================
// JETBRAINS IDE
// ============================================================

class JetBrainsIDE {
public:
  JetBrainsIDE(std::string name, fs::path path, std::string version)
      : name_(std::move(name)), path_(std::move(path)),
        version_(std::move(version)) {}

  [[nodiscard]]
  std::string_view name() const noexcept {
    return name_;
  }

  [[nodiscard]]
  std::string_view version() const noexcept {
    return version_;
  }

  [[nodiscard]]
  const fs::path &path() const noexcept {
    return path_;
  }

private:
  std::string name_;
  fs::path path_;
  std::string version_;
};

// ============================================================
// JETBRAINS SCANNER
// ============================================================

class JetBrainsScanner {
private:
  struct IdeInfo {
    std::string name;
    std::string version;
    fs::path path;
  };

  /*
   * JetBrains products checked by devver.
   *
   * identifier:
   *     used to detect the product in paths
   *
   * name:
   *     displayed to the user
   */

  static constexpr std::array<std::pair<std::string_view, std::string_view>, 12>
      ide_names{{{"clion", "CLion"},
                 {"idea", "IntelliJ IDEA"},
                 {"pycharm", "PyCharm"},
                 {"rustrover", "RustRover"},
                 {"webstorm", "WebStorm"},
                 {"datagrip", "DataGrip"},
                 {"goland", "GoLand"},
                 {"phpstorm", "PhpStorm"},
                 {"rubymine", "RubyMine"},
                 {"rider", "Rider"},
                 {"aqua", "Aqua"},
                 {"dataspell", "DataSpell"}}};

public:
  [[nodiscard]]
  std::vector<JetBrainsIDE> scan() const {

    std::vector<JetBrainsIDE> result;

    scan_toolbox(result);
    scan_path(result);

    remove_duplicates(result);

    return result;
  }

  [[nodiscard]]
  static constexpr std::size_t total_products() noexcept {
    return ide_names.size();
  }

  [[nodiscard]]
  static constexpr auto products() noexcept {
    return ide_names;
  }

private:
  // ========================================================
  // Detect IDE name from path
  // ========================================================

  [[nodiscard]]
  static std::string detect_name(const fs::path &path) {

    std::string complete_path = path.string();

    std::transform(complete_path.begin(), complete_path.end(),
                   complete_path.begin(), [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });

    for (const auto &[identifier, name] : ide_names) {

      if (complete_path.find(identifier) != std::string::npos) {
        return std::string{name};
      }
    }

    return {};
  }

  // ========================================================
  // Toolbox
  // ========================================================

  static void scan_toolbox(std::vector<JetBrainsIDE> &result) {

    const char *home = std::getenv("HOME");

    if (home == nullptr) {
      return;
    }

    const fs::path toolbox =
        fs::path{home} / ".local/share/JetBrains/Toolbox/apps";

    std::error_code error;

    if (!fs::exists(toolbox, error) || !fs::is_directory(toolbox, error)) {
      return;
    }

    /*
     * We don't recursively scan the whole
     * home directory.
     *
     * We scan ONLY:
     *
     * ~/.local/share/JetBrains/Toolbox/apps
     */

    scan_toolbox_directory(toolbox, result, 0);
  }

  static void scan_toolbox_directory(const fs::path &directory,
                                     std::vector<JetBrainsIDE> &result,
                                     int depth) {

    if (depth > 8) {
      return;
    }

    std::error_code error;

    for (fs::directory_iterator iterator{
             directory, fs::directory_options::skip_permission_denied, error};

         iterator != fs::directory_iterator{};

         iterator.increment(error)) {

      if (error) {
        error.clear();
        continue;
      }

      const fs::path current = iterator->path();

      if (!fs::is_directory(current, error)) {
        continue;
      }

      /*
       * JetBrains installation markers.
       */

      const fs::path build_txt = current / "build.txt";

      const fs::path product_info = current / "product-info.json";

      const fs::path lib_build_txt = current / "lib" / "build.txt";

      const fs::path lib_product_info = current / "lib" / "product-info.json";

      const bool is_ide = fs::exists(build_txt, error) ||
                          fs::exists(product_info, error) ||
                          fs::exists(lib_build_txt, error) ||
                          fs::exists(lib_product_info, error);

      if (is_ide) {

        const std::string name = detect_name(current);

        if (!name.empty()) {

          const std::string version = read_version(current);

          result.emplace_back(name, current,
                              version.empty() ? "unknown" : version);

          /*
           * IDE found.
           *
           * Don't scan inside it.
           */

          continue;
        }
      }

      /*
       * Manual recursion.
       */

      scan_toolbox_directory(current, result, depth + 1);
    }
  }

  // ========================================================
  // Read version
  // ========================================================

  [[nodiscard]]
  static std::string read_version(const fs::path &directory) {

    const std::array<fs::path, 4> files{
        {directory / "build.txt", directory / "lib" / "build.txt",
         directory / "product-info.json",
         directory / "lib" / "product-info.json"}};

    for (const fs::path &file : files) {

      std::error_code error;

      if (!fs::exists(file, error) || !fs::is_regular_file(file, error)) {
        continue;
      }

      std::ifstream stream{file};

      if (!stream) {
        continue;
      }

      std::string content{std::istreambuf_iterator<char>{stream},
                          std::istreambuf_iterator<char>{}};

      if (content.empty()) {
        continue;
      }

      const std::string version = VersionParser::extract(content);

      if (!version.empty()) {
        return version;
      }
    }

    return {};
  }

  // ========================================================
  // PATH
  // ========================================================

  static void scan_path(std::vector<JetBrainsIDE> &result) {

    /*
     * Executable names used by JetBrains products.
     */

    static constexpr std::array<std::pair<std::string_view, std::string_view>,
                                11>
        commands{{{"clion", "CLion"},
                  {"idea", "IntelliJ IDEA"},
                  {"pycharm", "PyCharm"},
                  {"rustrover", "RustRover"},
                  {"webstorm", "WebStorm"},
                  {"datagrip", "DataGrip"},
                  {"goland", "GoLand"},
                  {"phpstorm", "PhpStorm"},
                  {"rubymine", "RubyMine"},
                  {"rider", "Rider"},
                  {"aqua", "Aqua"}}};

    for (const auto &[command, name] : commands) {

      if (!CommandRunner::exists(command)) {
        continue;
      }

      const CommandResult result_command =
          CommandRunner::run(command, {"--version"});

      std::string version{"unknown"};

      if (!result_command.output.empty()) {

        const std::string parsed =
            VersionParser::extract(result_command.output);

        if (!parsed.empty()) {
          version = parsed;
        }
      }

      result.emplace_back(std::string{name}, fs::path{command},
                          std::move(version));
    }
  }

  // ========================================================
  // Remove duplicates
  // ========================================================

  static void remove_duplicates(std::vector<JetBrainsIDE> &result) {

    std::vector<JetBrainsIDE> unique_ides;

    unique_ides.reserve(result.size());

    for (const JetBrainsIDE &ide : result) {

      const bool exists = std::any_of(unique_ides.begin(), unique_ides.end(),
                                      [&ide](const JetBrainsIDE &existing) {
                                        return existing.name() == ide.name();
                                      });

      if (!exists) {
        unique_ides.push_back(ide);
      }
    }

    result = std::move(unique_ides);
  }
};

// ============================================================
// JETBRAINS SECTION
// ============================================================

class JetBrainsSection {
public:
  [[nodiscard]]
  static Statistics print() {

    Console::section("JETBRAINS IDE");

    const JetBrainsScanner scanner;

    /*
     * Only actually installed IDEs are returned
     * by scan().
     */
    const std::vector<JetBrainsIDE> installed_ides = scanner.scan();

    Statistics statistics;

    /*
     * Iterate over the COMPLETE list of JetBrains
     * products.
     *
     * This allows us to print both:
     *
     * ✓ installed
     * ✗ not installed
     */

    for (const auto &[identifier, name] : JetBrainsScanner::products()) {

      (void)identifier;

      const auto iterator = std::find_if(
          installed_ides.begin(), installed_ides.end(),
          [name](const JetBrainsIDE &ide) { return ide.name() == name; });

      if (iterator != installed_ides.end()) {

        ++statistics.installed;

        Console::jetbrains_tool(iterator->name(), iterator->version());

      } else {

        ++statistics.missing;

        Console::jetbrains_missing(name);
      }
    }

    Console::section_end();

    return statistics;
  }
};

// ============================================================
// APPLICATION
// ============================================================

class Application {
public:
  int run() const {

    Console::banner();

    Statistics total;

    total += print_cpp();
    total += print_build_systems();
    total += print_cpp_package_managers();
    total += print_rust();
    total += print_python();
    total += print_javascript();
    total += print_go();
    total += print_java();
    total += print_version_control();
    total += print_containers();
    total += print_databases();
    total += print_debugging();
    total += print_editors();
    total += print_documentation();
    total += print_virtualization();

    /*
     * IMPORTANT:
     *
     * JetBrains now participates in the
     * global statistics.
     */

    total += JetBrainsSection::print();

    Console::summary(total);

    return 0;
  }

private:
  // ========================================================
  // Generic section
  // ========================================================

  [[nodiscard]]
  static Statistics print(std::string_view title,
                          const std::vector<Tool> &tools) {

    Console::section(title);

    const Statistics statistics = ToolChecker::check(tools);

    Console::section_end();

    return statistics;
  }

  // ========================================================
  // C / C++
  // ========================================================

  [[nodiscard]]
  static Statistics print_cpp() {

    return print("C / C++",
                 {Tools::version("GCC", "gcc", {"--version"}),

                  Tools::version("G++", "g++", {"--version"}),

                  Tools::version("Clang", "clang", {"--version"}),

                  Tools::version("Clang++", "clang++", {"--version"}),

                  Tools::version("clang-format", "clang-format", {"--version"}),

                  Tools::version("clang-tidy", "clang-tidy", {"--version"}),

                  Tools::version("cppcheck", "cppcheck", {"--version"}),

                  Tools::version("CMake", "cmake", {"--version"}),

                  Tools::version("Ninja", "ninja", {"--version"}),

                  Tools::version("Make", "make", {"--version"}),

                  Tools::version("pkg-config", "pkg-config", {"--version"})});
  }

  // ========================================================
  // Build systems
  // ========================================================

  [[nodiscard]]
  static Statistics print_build_systems() {

    return print("BUILD SYSTEMS",
                 {Tools::version("Meson", "meson", {"--version"}),

                  Tools::version("Autoconf", "autoconf", {"--version"}),

                  Tools::version("Automake", "automake", {"--version"}),

                  Tools::version("Libtoolize", "libtoolize", {"--version"}),

                  Tools::version("Bison", "bison", {"--version"}),

                  Tools::version("Flex", "flex", {"--version"})});
  }

  // ========================================================
  // C++ package managers
  // ========================================================

  [[nodiscard]]
  static Statistics print_cpp_package_managers() {

    return print("C++ PACKAGE MANAGERS",
                 {Tools::version("Conan", "conan", {"--version"}),

                  Tools::version("Vcpkg", "vcpkg", {"--version"})});
  }

  // ========================================================
  // Rust
  // ========================================================

  [[nodiscard]]
  static Statistics print_rust() {

    return print(
        "RUST",
        {Tools::version("Rustc", "rustc", {"--version"}),

         Tools::version("Cargo", "cargo", {"--version"}),

         Tools::version("Rustfmt", "rustfmt", {"--version"}),

         Tools::version("Cargo Clippy", "cargo", {"clippy", "--version"}),

         Tools::version("Rust Analyzer", "rust-analyzer", {"--version"})});
  }

  // ========================================================
  // Python
  // ========================================================

  [[nodiscard]]
  static Statistics print_python() {

    return print("PYTHON", {Tools::version("Python", "python", {"--version"}),

                            Tools::version("Python3", "python3", {"--version"}),

                            Tools::version("Pip", "pip", {"--version"}),

                            Tools::version("Pip3", "pip3", {"--version"}),

                            Tools::version("UV", "uv", {"--version"}),

                            Tools::version("Poetry", "poetry", {"--version"}),

                            Tools::version("Pipenv", "pipenv", {"--version"})});
  }

  // ========================================================
  // JavaScript / TypeScript
  // ========================================================

  [[nodiscard]]
  static Statistics print_javascript() {

    return print("JAVASCRIPT / TYPESCRIPT",
                 {Tools::version("Node", "node", {"--version"}),

                  Tools::version("NPM", "npm", {"--version"}),

                  Tools::version("NPX", "npx", {"--version"}),

                  Tools::version("Yarn", "yarn", {"--version"}),

                  Tools::version("PNPM", "pnpm", {"--version"}),

                  Tools::version("Bun", "bun", {"--version"}),

                  Tools::version("Deno", "deno", {"--version"}),

                  Tools::version("TypeScript", "tsc", {"--version"})});
  }

  // ========================================================
  // Go
  // ========================================================

  [[nodiscard]]
  static Statistics print_go() {

    return print("GO", {Tools::version("Go", "go", {"version"}),

                        Tools::exists("Gofmt", "gofmt")});
  }

  // ========================================================
  // Java / JVM
  // ========================================================

  [[nodiscard]]
  static Statistics print_java() {

    return print("JAVA / JVM",
                 {Tools::version("Java", "java", {"--version"}),

                  Tools::version("Javac", "javac", {"--version"}),

                  Tools::version("Maven", "mvn", {"--version"}),

                  Tools::version("Gradle", "gradle", {"--version"})});
  }

  // ========================================================
  // Version control
  // ========================================================

  [[nodiscard]]
  static Statistics print_version_control() {

    return print("VERSION CONTROL",
                 {Tools::version("Git", "git", {"--version"}),

                  Tools::version("Git LFS", "git-lfs", {"--version"}),

                  Tools::version("SVN", "svn", {"--version"}),

                  Tools::version("Mercurial", "hg", {"--version"})});
  }

  // ========================================================
  // Containers
  // ========================================================

  [[nodiscard]]
  static Statistics print_containers() {

    return print(
        "CONTAINERS",
        {Tools::version("Docker", "docker", {"--version"}),

         Tools::version("Docker Compose", "docker", {"compose", "version"}),

         Tools::version("Podman", "podman", {"--version"}),

         Tools::version("Buildah", "buildah", {"--version"}),

         Tools::version("Skopeo", "skopeo", {"--version"})});
  }

  // ========================================================
  // Databases
  // ========================================================

  [[nodiscard]]
  static Statistics print_databases() {

    return print("DATABASES",
                 {Tools::version("PostgreSQL", "psql", {"--version"}),

                  Tools::version("MySQL", "mysql", {"--version"}),

                  Tools::version("MariaDB", "mariadb", {"--version"}),

                  Tools::version("SQLite", "sqlite3", {"--version"}),

                  Tools::version("Redis", "redis-cli", {"--version"}),

                  Tools::version("MongoDB", "mongosh", {"--version"})});
  }

  // ========================================================
  // Debugging / Profiling
  // ========================================================

  [[nodiscard]]
  static Statistics print_debugging() {

    return print("DEBUG / PROFILING",
                 {Tools::version("GDB", "gdb", {"--version"}),

                  Tools::version("LLDB", "lldb", {"--version"}),

                  Tools::version("Valgrind", "valgrind", {"--version"}),

                  Tools::version("Perf", "perf", {"--version"}),

                  Tools::version("Strace", "strace", {"--version"}),

                  Tools::version("Ltrace", "ltrace", {"--version"})});
  }

  // ========================================================
  // Editors / IDE
  // ========================================================

  [[nodiscard]]
  static Statistics print_editors() {

    return print("EDITORS / IDE",
                 {Tools::version("VS Code", "code", {"--version"}),

                  Tools::version("Zed", "zeditor", {"--version"}),

                  Tools::version("Neovim", "nvim", {"--version"}),

                  Tools::version("Vim", "vim", {"--version"}),

                  Tools::version("Emacs", "emacs", {"--version"})});
  }

  // ========================================================
  // Documentation
  // ========================================================

  [[nodiscard]]
  static Statistics print_documentation() {

    return print("DOCUMENTATION / TOOLS",
                 {Tools::version("Doxygen", "doxygen", {"--version"}),

                  Tools::version("Graphviz", "dot", {"-V"}),

                  Tools::version("Sphinx", "sphinx-build", {"--version"})});
  }

  // ========================================================
  // Virtualization
  // ========================================================

  [[nodiscard]]
  static Statistics print_virtualization() {

    return print("VIRTUALIZATION / TOOLS",
                 {Tools::version("VirtualBox", "virtualbox", {"--help"}),

                  Tools::version("QEMU", "qemu-system-x86_64", {"--version"})});
  }
};

// ============================================================
// MAIN
// ============================================================

int main() {

  const Application application;

  return application.run();
}