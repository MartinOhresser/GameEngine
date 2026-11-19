// The Android target of `gectl export`: finding the tools, the values taken from a manifest,
// and the commands that make an APK (a fake SDK and a fake runner stand for the real ones, so
// nothing is compiled here).
#include "ge/game/summary.hpp"

#include "../support/mini_test.hpp"
#include "export.hpp"
#include "export_android.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ge::tools::Environment;

namespace {

#ifdef _WIN32
constexpr const char* exe = ".exe";
#else
constexpr const char* exe = "";
#endif

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

void touch(const fs::path& path, const std::string& text = "x") {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream(path, std::ios::binary) << text;
}

std::string text_of(const fs::path& path) {
    const std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

class Scratch {
public:
    explicit Scratch(const std::string& name)
        : path_(fs::temp_directory_path() / ("ge_android_test_" + name)) {
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

/// What a machine set up for Android development has, in `root`.
void make_sdk(const fs::path& root, bool with_ndk = true) {
    fs::create_directories(root / "sdk" / "platforms" / "android-34");
    fs::create_directories(root / "sdk" / "platforms" / "android-36");
    fs::create_directories(root / "sdk" / "platforms" / "android-36.1-ext");
    fs::create_directories(root / "sdk" / "build-tools" / "35.0.0");
    touch(root / "sdk" / "cmake" / "3.22.1" / "bin" / (std::string("ninja") + exe));
    if (with_ndk) {
        touch(root / "sdk" / "ndk" / "26.1" / "build" / "cmake" / "android.toolchain.cmake");
        touch(root / "sdk" / "ndk" / "27.0" / "build" / "cmake" / "android.toolchain.cmake");
    }
    touch(root / "jdk" / "bin" / (std::string("java") + exe));
    touch(root / "sdk" / "platform-tools" / (std::string("adb") + exe));
}

Environment environment_of(std::map<std::string, std::string> variables) {
    return [variables = std::move(variables)](const std::string& name) {
        const auto found = variables.find(name);
        return found == variables.end() ? std::string() : found->second;
    };
}

Environment machine(const fs::path& root) {
    return environment_of({{"ANDROID_HOME", (root / "sdk").generic_string()},
                           {"JAVA_HOME", (root / "jdk").generic_string()}});
}

/// What the export asked the system to run, and the files the real programs would have made.
struct FakeSystem {
    std::vector<std::vector<std::string>> commands;
    int fail_at = -1;

    [[nodiscard]] ge::tools::CommandRunner runner() {
        return [this](const std::vector<std::string>& command) {
            const int index = static_cast<int>(commands.size());
            commands.push_back(command);
            if (index == fail_at) {
                return 1;
            }
            if (command.size() > 2 && command[1] == "--build") {
                const fs::path dir = command[2];
                touch(dir / "platform" / "sdl" / "libmain.so",
                      "library of " + dir.filename().string());
                const fs::path sdl = dir / "_deps" / "sdl3-src" / "android-project";
                touch(sdl / "gradlew", "#!/bin/sh");
                touch(sdl / "gradlew.bat", "rem");
                touch(sdl / "gradle.properties", "org.gradle.jvmargs=-Xmx2g");
                touch(sdl / "gradle" / "wrapper" / "gradle-wrapper.jar", "jar");
                touch(sdl / "app" / "src" / "main" / "java" / "org" / "libsdl" / "app" /
                      "SDLActivity.java");
                touch(sdl / "app" / "src" / "main" / "res" / "mipmap-mdpi" / "ic_launcher.png");
            }
            if (command.size() > 4 && command[1] == "-E" && command[2] == "chdir" &&
                command[4].find("gradlew") != std::string::npos) {
                touch(fs::path(command[3]) / "app" / "build" / "outputs" / "apk" / "debug" /
                          "app-debug.apk",
                      "apk");
            }
            return 0;
        };
    }
};

ge::tools::ExportRequest request_in(const Scratch& scratch, const std::string& game) {
    ge::tools::ExportRequest request;
    request.manifest = (fs::path(GE_SOURCE_DIR) / "games" / game / "game.json").generic_string();
    request.target = "android";
    request.out_dir = (scratch.path() / "dist").generic_string();
    request.source_dir = GE_SOURCE_DIR;
    request.build_dir = (scratch.path() / "build").generic_string();
    request.skip_tests = true;
    return request;
}

/// Runs the two steps of the target as `export_game` does, with this machine's tools.
bool run_target(const ge::tools::ExportTarget& target, const ge::tools::ExportRequest& request,
                FakeSystem& system, std::string& artifact, std::ostringstream& err) {
    ge::tools::ExportJob job;
    std::ostringstream out;
    if (!ge::tools::prepare_export_job(request, target, system.runner(), job, err)) {
        return false;
    }
    std::string program;
    return target.build(job, program, out, err) &&
           target.assemble(job, program, artifact, out, err);
}

} // namespace

TEST_CASE("android: the tools are found where Android Studio puts them") {
    const Scratch scratch("tools");
    make_sdk(scratch.path());
    const ge::tools::AndroidTools tools = ge::tools::find_android_tools(machine(scratch.path()));
    CHECK(tools.ok());
    CHECK(tools.platform == 36); // the numbered platforms, not "36.1-ext" nor the older 34
    CHECK(tools.ndk.filename() == "27.0");
    CHECK(tools.ninja.generic_string().find("3.22.1") != std::string::npos);
    CHECK(!tools.java.empty());
}

TEST_CASE("android: the SDK is also found under the home folder, ninja in the PATH") {
    const Scratch scratch("home");
    fs::create_directories(scratch.path() / "Android" / "Sdk" / "platforms" / "android-35");
    fs::create_directories(scratch.path() / "Android" / "Sdk" / "build-tools" / "35.0.0");
    touch(scratch.path() / "Android" / "Sdk" / "ndk" / "27.0" / "build" / "cmake" /
          "android.toolchain.cmake");
    touch(scratch.path() / "bin" / (std::string("ninja") + exe));
    touch(scratch.path() / "bin" / (std::string("java") + exe));
#ifdef _WIN32
    const std::string separator = ";";
#else
    const std::string separator = ":";
#endif
    const ge::tools::AndroidTools tools = ge::tools::find_android_tools(environment_of(
        {{"HOME", scratch.path().generic_string()},
         {"PATH", "/nowhere" + separator + (scratch.path() / "bin").generic_string()}}));
    CHECK(tools.ok());
    CHECK(tools.platform == 35);
    CHECK(tools.ninja.parent_path().filename() == "bin");
}

TEST_CASE("android: what is missing is said, with where to get it") {
    const Scratch scratch("missing");
    const ge::tools::AndroidTools none = ge::tools::find_android_tools(environment_of({}));
    CHECK(!none.ok());
    CHECK(none.missing.size() == 4U); // SDK, NDK, JDK, Ninja
    CHECK(has(none.missing.front(), "ANDROID_HOME"));

    make_sdk(scratch.path(), false);
    const ge::tools::AndroidTools no_ndk = ge::tools::find_android_tools(machine(scratch.path()));
    REQUIRE(no_ndk.missing.size() == 1U);
    CHECK(has(no_ndk.missing.front(), "NDK"));

    const ge::tools::ExportTarget* android = ge::tools::find_export_target("android");
    REQUIRE(android != nullptr);
    CHECK(!android->hidden());
    CHECK(android->label() == "Android (APK)");
    const std::unique_ptr<ge::tools::ExportTarget> without =
        ge::tools::make_android_target(machine(scratch.path()));
    CHECK(!without->buildable_here());
    CHECK(has(without->needs(), "NDK"));
}

TEST_CASE("android: the identifier, the version and the orientation come from the manifest") {
    CHECK(ge::tools::android_application_id("doodle_jump") == "ge.doodle_jump");
    CHECK(ge::tools::android_application_id("my-game") == "ge.my_game");
    CHECK(ge::tools::android_application_id("com.me.game") == "com.me.game");
    CHECK(ge::tools::android_application_id("2048") == "ge.g2048");
    CHECK(ge::tools::android_application_id("") == "ge.game");
    CHECK(ge::tools::android_version_code("1.2.3") == 10203U);
    CHECK(ge::tools::android_version_code("0.1.0") == 100U);
    CHECK(ge::tools::android_version_code("2") == 20000U);
    CHECK(ge::tools::android_version_code("beta") == 1U);
    CHECK(ge::tools::android_orientation(480, 720) == "portrait");
    CHECK(ge::tools::android_orientation(800, 600) == "sensorLandscape");
    CHECK(ge::tools::android_orientation(600, 600) == "unspecified");
    CHECK(ge::tools::xml_escape("Tom & \"Jerry\" <3 it's") ==
          "Tom &amp; &quot;Jerry&quot; &lt;3 it\\'s");
}

TEST_CASE("android: the commands that make an APK") {
    const Scratch scratch("apk");
    make_sdk(scratch.path());
    const std::unique_ptr<ge::tools::ExportTarget> target =
        ge::tools::make_android_target(machine(scratch.path()));
    ge::tools::ExportRequest request = request_in(scratch, "doodle_jump");
    request.abis = {"x86_64", "arm64-v8a"};
    FakeSystem system;
    std::string artifact;
    std::ostringstream err;
    REQUIRE(run_target(*target, request, system, artifact, err));
    CHECK(err.str().empty());

    // Configure and build once per processor, then Gradle.
    REQUIRE(system.commands.size() == 5U);
    const auto& configure = system.commands[0];
    CHECK(configure[0] == "cmake");
    CHECK(std::ranges::find(configure, "-DANDROID_ABI=x86_64") != configure.end());
    CHECK(std::ranges::find(configure, "-DANDROID_PLATFORM=android-21") != configure.end());
    CHECK(std::ranges::any_of(configure, [](const std::string& a) {
        return a.starts_with("-DCMAKE_TOOLCHAIN_FILE=") && a.ends_with("android.toolchain.cmake");
    }));
    CHECK(std::ranges::any_of(configure, [](const std::string& a) {
        return a.starts_with("-DGE_PLAYER_MODULES=") && a.find("sprite2d") != std::string::npos;
    }));
    CHECK(std::ranges::find(system.commands[2], "-DANDROID_ABI=arm64-v8a") !=
          system.commands[2].end());
    const auto& gradle = system.commands[4];
    CHECK(gradle[1] == "-E");
    CHECK(std::ranges::find(gradle, "-PcompileSdk=36") != gradle.end());
    CHECK(gradle.back() == "assembleDebug");

    // The project Gradle is run on.
    const fs::path project = scratch.path() / "build" / "project";
    const fs::path main = project / "app" / "src" / "main";
    CHECK(text_of(main / "jniLibs" / "x86_64" / "libmain.so") == "library of x86_64");
    CHECK(text_of(main / "jniLibs" / "arm64-v8a" / "libmain.so") == "library of arm64-v8a");
    CHECK(fs::exists(main / "assets" / "data" / "game.json"));
    CHECK(fs::exists(main / "java" / "org" / "libsdl" / "app" / "SDLActivity.java"));
    CHECK(fs::exists(main / "java" / "ge" / "player" / "GameActivity.java"));
    CHECK(fs::exists(project / "gradle" / "wrapper" / "gradle-wrapper.jar"));
    CHECK(has(text_of(project / "local.properties"), "sdk.dir="));

    const std::string gradle_file = text_of(project / "app" / "build.gradle");
    CHECK(has(gradle_file, "applicationId \"ge.games.doodle_jump\""));
    CHECK(!has(gradle_file, "@APPLICATION_ID@"));
    CHECK(!has(gradle_file, "@VERSION"));
    CHECK(has(gradle_file, "versionName \"0.1.0\""));
    const std::string manifest = text_of(main / "AndroidManifest.xml");
    CHECK(has(manifest, "portrait")); // doodle_jump's window is taller than wide
    CHECK(!has(manifest, "@ICON@"));
    CHECK(!has(manifest, "@ORIENTATION@"));
    CHECK(has(text_of(main / "res" / "values" / "strings.xml"), "Doodle Jump"));

    CHECK(fs::path(artifact).extension() == ".apk");
    CHECK(fs::path(artifact).parent_path() == fs::absolute(scratch.path() / "dist"));
    CHECK(text_of(artifact) == "apk");
}

TEST_CASE("android: one processor by default, and a failing step stops the export") {
    const Scratch scratch("fail");
    make_sdk(scratch.path());
    const std::unique_ptr<ge::tools::ExportTarget> target =
        ge::tools::make_android_target(machine(scratch.path()));
    const ge::tools::ExportRequest request = request_in(scratch, "move_sprite");
    FakeSystem system;
    std::string artifact;
    std::ostringstream err;
    REQUIRE(run_target(*target, request, system, artifact, err));
    REQUIRE(system.commands.size() == 3U); // configure, build, gradle
    CHECK(std::ranges::find(system.commands[0], "-DANDROID_ABI=arm64-v8a") !=
          system.commands[0].end());

    FakeSystem broken;
    broken.fail_at = 1; // the build of the program
    std::ostringstream err2;
    CHECK(!run_target(*target, request, broken, artifact, err2));
    CHECK(has(err2.str(), "could not be built"));
    CHECK(broken.commands.size() == 2U);

    FakeSystem no_gradle;
    no_gradle.fail_at = 2;
    std::ostringstream err3;
    CHECK(!run_target(*target, request, no_gradle, artifact, err3));
    CHECK(has(err3.str(), "Gradle"));
}

TEST_CASE("android: --install puts the package on the phone with adb") {
    const Scratch scratch("install");
    make_sdk(scratch.path());
    const std::unique_ptr<ge::tools::ExportTarget> target =
        ge::tools::make_android_target(machine(scratch.path()));
    ge::tools::ExportRequest request = request_in(scratch, "move_sprite");
    request.install = true;
    FakeSystem system;
    std::string artifact;
    std::ostringstream err;
    REQUIRE(run_target(*target, request, system, artifact, err));
    REQUIRE(system.commands.size() == 4U);
    const auto& install = system.commands[3];
    CHECK(fs::path(install[0]).stem() == "adb");
    CHECK(install[1] == "install");
    CHECK(install[2] == "-r");
    CHECK(install[3] == artifact);

    FakeSystem refused;
    refused.fail_at = 3;
    std::ostringstream err2;
    CHECK(!run_target(*target, request, refused, artifact, err2));
    CHECK(has(err2.str(), "adb could not install"));

    // Without adb, the export says so before building anything.
    fs::remove(scratch.path() / "sdk" / "platform-tools" / (std::string("adb") + exe));
    FakeSystem none;
    std::ostringstream err3;
    CHECK(!run_target(*target, request, none, artifact, err3));
    CHECK(has(err3.str(), "needs adb"));
    CHECK(none.commands.empty());
}

TEST_CASE("android: a package can be signed with the developer's own key") {
    const Scratch scratch("signing");
    make_sdk(scratch.path());
    touch(scratch.path() / "my.keystore");
    ge::tools::ExportRequest request = request_in(scratch, "move_sprite");
    request.keystore = (scratch.path() / "my.keystore").generic_string();
    request.key_alias = "release";
    const std::unique_ptr<ge::tools::ExportTarget> without_password =
        ge::tools::make_android_target(machine(scratch.path()));
    FakeSystem system;
    std::string artifact;
    std::ostringstream err;
    CHECK(!run_target(*without_password, request, system, artifact, err));
    CHECK(has(err.str(), "GE_KEYSTORE_PASSWORD"));
    CHECK(system.commands.empty());

    const std::unique_ptr<ge::tools::ExportTarget> target = ge::tools::make_android_target(
        environment_of({{"ANDROID_HOME", (scratch.path() / "sdk").generic_string()},
                        {"JAVA_HOME", (scratch.path() / "jdk").generic_string()},
                        {"GE_KEYSTORE_PASSWORD", "secret"}}));
    std::ostringstream err2;
    REQUIRE(run_target(*target, request, system, artifact, err2));
    const auto& gradle = system.commands.back();
    CHECK(std::ranges::any_of(gradle, [](const std::string& a) {
        return a.starts_with("-PgeKeystore=") && a.ends_with("my.keystore");
    }));
    CHECK(std::ranges::find(gradle, "-PgeKeyAlias=release") != gradle.end());
    // The password is never on a command line.
    CHECK(!std::ranges::any_of(
        gradle, [](const std::string& a) { return a.find("secret") != std::string::npos; }));
    CHECK(gradle.back() == "assembleDebug");

    request.key_alias.clear();
    std::ostringstream err3;
    CHECK(!run_target(*target, request, system, artifact, err3));
    CHECK(has(err3.str(), "--key-alias"));
    request.key_alias = "release";
    request.keystore = (scratch.path() / "nope.keystore").generic_string();
    std::ostringstream err4;
    CHECK(!run_target(*target, request, system, artifact, err4));
    CHECK(has(err4.str(), "does not exist"));
}
