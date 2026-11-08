// Fukami (Japanese edition, SLPS-20001) for Linux: the normal-play launcher.
// Ported from src/app/fukami_app_linux.cpp (v1.1.0). Replaces the env-only
// app_shim.cpp of the experiment builds.
//
// A startup hook runs before main(). When this image is the app's bin/Fukami
// (next to share/fukami-default.ini) and is not yet the game process, it:
//   1. finds the user's Japanese disc image (CHD, or .cue/.bin), checks it and
//      unpacks the ten files once into the data folder,
//   2. loads the user's settings file (created from the bundled default),
//   3. keeps the memory card in a fixed folder (persistent across launches),
//   4. re-executes itself as the game with the environment built from the
//      settings (the same mapping as the North American app).
//
// Layout (any location, may contain spaces):
//   Fukami-JP/Fukami                     launcher script (LD_LIBRARY_PATH=lib, exec bin/Fukami)
//   Fukami-JP/bin/Fukami                 this image
//   Fukami-JP/lib/                       SDL2 + bridge closure
//   Fukami-JP/share/fukami-default.ini   Japanese-edition defaults
//   Fukami-JP/share/workload/bound-workload.txt  JP product workload (persistent card, no terminal)
//   Fukami-JP/share/game/                fixed resource package
// User data: ${XDG_DATA_HOME:-~/.local/share}/Fukami-JP (fukami.ini, mc/, disc/, sessions/).
//
// Developer/test launches are left alone: the hook does nothing when the
// environment already names a bound workload (RRV_GATE3_BOUND_WORKLOAD), which
// every run-*-test.sh script sets. restart() removes that variable so the hook
// runs again and re-reads the settings.
#include "fukami_app.h"
#include "fukami_app_common.h"
#include "fukami_settings.h"
#include "disc_extract.h"

#include <spawn.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace {

namespace fs = std::filesystem;
using namespace fukami::app::detail;

constexpr const char *kWorkloadVar = "RRV_GATE3_BOUND_WORKLOAD";
constexpr const char *kBootArgv0 = "cdrom0:\\SLPS_200.01;1";
constexpr const char *kElfName = "SLPS_200.01";

// ---- Japanese disc profile ---------------------------------------------------
// Names, sizes and SHA-256 of the ten root files of Ridge Racer V (Japan,
// SLPS-20001). Nothing game-derived beyond these fingerprints is embedded.
constexpr fukami::disc::detail::ExpectedFile kJpFiles[] = {
    {"IOPRP14E.IMG", 58013, "da3dc6d35e947a2526a236c801e074827ed144866a5ae73785c6c79159885168"},
    {"MCMAN.IRX", 63341, "9bf00bca0baf1cedda83eafa89994538959eace6ab11865c51c59cd3f8363539"},
    {"MCSERV.IRX", 5549, "d80617d0f466f1e96938f934ebce2a1897f0344b7e3401162d9fd0582dc71b46"},
    {"PADMAN.IRX", 39429, "26994f512c4d4cd5e458a4d15dd672191ccc5f4735ead5891542252a3de72eb3"},
    {"R5.ALL", 489742336, "6090afbacce1efa4907ec415e8a44f6ab74b258aaaa733f056f9058ec62d7f36"},
    {"R5.STR", 161521664, "6f905212dedc0fabaf98f59e59d26f0ff9b66f95d440f700d67a57376f57e644"},
    {"RSPU2DRV.IRX", 136157, "9e4063dc76d8c21eea7e06aca8ec8b132731f8323d7787851d979a6c6dfac133"},
    {"SIO2MAN.IRX", 8317, "20817a0c28152d0339eb1ba9e6fefc369c8a1933ec57dfae27bf8cdfaf86ea72"},
    {"SLPS_200.01", 1289332, "4532fb27202f6259c37d1e0d9f9d02f59f9479da29f0ac7d95e04bf5b5d759fa"},
    {"SYSTEM.CNF", 57, "87def77eed6432386bae1f2bbbe416d482b21b2cfa7cfbace0d163a4552c5abf"},
};
constexpr size_t kJpFileCount = sizeof kJpFiles / sizeof kJpFiles[0];

const fukami::disc::detail::Profile &jpProfile()
{
    static const fukami::disc::detail::Profile p{kJpFiles, "cdrom0:\\slps_200.01;1"};
    return p;
}

uint64_t jpRequiredBytes()
{
    uint64_t total = 0;
    for (const auto &f : kJpFiles)
        total += f.size;
    return total;
}

std::string statusText(const fukami::disc::Result &r)
{
    using fukami::disc::Status;
    switch (r.status)
    {
    case Status::ok: return "OK";
    case Status::cannotOpen: return "ファイルを開けません。";
    case Status::notChd: return "CHD形式として読めません。";
    case Status::notIso9660: return "PS2のディスクイメージとして読めません。";
    case Status::wrongDisc: return "リッジレーサーV（日本版 SLPS-20001）のディスクではありません。";
    case Status::damaged: return "ディスクイメージが壊れているか、内容が一致しません。(" + r.message + ")";
    case Status::noSpace: return "保存先の空き容量が足りません。";
    case Status::writeFailed: return "ファイルを書き込めません。(" + r.message + ")";
    case Status::cancelled: return "中止しました。";
    }
    return r.message;
}

fukami::disc::Result probeJp(const fs::path &image)
{
    try
    {
        fukami::disc::Result err{fukami::disc::Status::ok, {}};
        auto src = fukami::disc::detail::openImage(image, err);
        if (!src)
            return err;
        return fukami::disc::detail::probeSource(*src, jpProfile());
    }
    catch (...)
    {
        return {fukami::disc::Status::damaged, "unexpected error"};
    }
}

fukami::disc::Result extractJp(const fs::path &image, const fs::path &dest,
                               const std::function<bool(const fukami::disc::Progress &)> &progress)
{
    try
    {
        fukami::disc::Result err{fukami::disc::Status::ok, {}};
        auto src = fukami::disc::detail::openImage(image, err);
        if (!src)
            return err;
        return fukami::disc::detail::extractSource(*src, jpProfile(), dest, progress);
    }
    catch (...)
    {
        return {fukami::disc::Status::damaged, "unexpected error"};
    }
}

// The unpacked folder is ready when its marker lists exactly the ten expected
// files with their expected sizes (hashes were checked when unpacking).
bool jpDiscReady(const fs::path &disc)
{
    std::ifstream marker(disc / kVerifiedMarker);
    if (!marker)
        return false;
    std::string name;
    uintmax_t size = 0;
    size_t files = 0;
    while (marker >> name >> size)
    {
        bool expected = false;
        for (const auto &f : kJpFiles)
            if (name == f.name && size == f.size)
                expected = true;
        std::error_code ec;
        if (!expected || fs::file_size(disc / name, ec) != size || ec)
            return false;
        ++files;
    }
    return files == kJpFileCount;
}

// ---- paths -------------------------------------------------------------------

fs::path executablePath()
{
    char resolved[PATH_MAX];
    if (!realpath("/proc/self/exe", resolved))
        return {};
    return resolved;
}

fs::path appRoot(const fs::path &exe)
{
    const fs::path bin = exe.parent_path();
    if (exe.filename() != "Fukami" || bin.filename() != "bin")
        return {};
    std::error_code ec;
    if (!fs::is_regular_file(bin.parent_path() / "share" / "fukami-default.ini", ec))
        return {};
    return bin.parent_path();
}

std::string envOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

fs::path supportDirectory()
{
    if (const char *dir = std::getenv("FUKAMI_JP_DATA"); dir && dir[0] == '/')
        return dir;
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] == '/')
        return fs::path(xdg) / "Fukami-JP";
    return fs::path(envOr("HOME", "/tmp")) / ".local/share/Fukami-JP";
}

// ---- dialogs (zenity or kdialog when present; always stderr) ---------------

fs::path findTool(const char *name)
{
    const std::string path = envOr("PATH", "/usr/bin:/bin");
    size_t begin = 0;
    while (begin <= path.size())
    {
        const size_t end = path.find(':', begin);
        const std::string dir = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!dir.empty())
        {
            const fs::path candidate = fs::path(dir) / name;
            if (access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return {};
}

bool haveDisplay()
{
    const char *x = std::getenv("DISPLAY");
    const char *w = std::getenv("WAYLAND_DISPLAY");
    return ((x && *x) || (w && *w)) && !std::getenv("FUKAMI_JP_NO_DIALOGS");
}

int runTool(const std::vector<std::string> &args, std::string *output = nullptr)
{
    int pipeFds[2] = {-1, -1};
    if (output && pipe(pipeFds) != 0)
        return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (output)
    {
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    }
    std::vector<std::string> copy = args;
    std::vector<char *> argv;
    for (auto &arg : copy)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (output)
        close(pipeFds[1]);
    if (spawned != 0)
    {
        if (output)
            close(pipeFds[0]);
        return -1;
    }
    if (output)
    {
        char buffer[4096];
        ssize_t n;
        while ((n = read(pipeFds[0], buffer, sizeof buffer)) > 0)
            output->append(buffer, static_cast<size_t>(n));
        close(pipeFds[0]);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void message(const std::string &title, const std::string &text, bool error)
{
    std::fprintf(stderr, "[fukami-jp] %s: %s\n", title.c_str(), text.c_str());
    if (!haveDisplay())
        return;
    if (const fs::path zenity = findTool("zenity"); !zenity.empty())
        runTool({zenity.string(), error ? "--error" : "--info", "--title=" + title, "--no-markup", "--text=" + text});
    else if (const fs::path kdialog = findTool("kdialog"); !kdialog.empty())
        runTool({kdialog.string(), "--title", title, error ? "--error" : "--msgbox", text});
}

fs::path pickImage()
{
    if (!haveDisplay())
        return {};
    std::string out;
    int status = -1;
    const std::string title = "リッジレーサーV（日本版）のディスクイメージを選択";
    if (const fs::path zenity = findTool("zenity"); !zenity.empty())
        status = runTool({zenity.string(), "--file-selection", "--title=" + title,
                          "--file-filter=Disc images | *.chd *.CHD *.cue *.CUE *.bin *.BIN"},
                         &out);
    else if (const fs::path kdialog = findTool("kdialog"); !kdialog.empty())
        status = runTool({kdialog.string(), "--title", title, "--getopenfilename", envOr("HOME", "/"),
                          "*.chd *.CHD *.cue *.CUE *.bin *.BIN"},
                         &out);
    if (status != 0)
        return {};
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

// ---- disc setup ------------------------------------------------------------

fukami::disc::Result extractWithProgress(const fs::path &image, const fs::path &disc)
{
    FILE *zenity = nullptr;
    if (haveDisplay())
        if (const fs::path tool = findTool("zenity"); !tool.empty())
        {
            const std::string command = "'" + tool.string() +
                                        "' --progress --title=Fukami --text='ゲームデータを展開しています...' "
                                        "--percentage=0 --auto-close 2>/dev/null";
            zenity = popen(command.c_str(), "w");
        }
    const auto previousPipe = std::signal(SIGPIPE, SIG_IGN);
    int lastPercent = -1;
    bool cancelled = false;
    auto result = extractJp(image, disc, [&](const fukami::disc::Progress &p) {
        const uint64_t total = p.bytesTotal ? p.bytesTotal : 1;
        const int percent = static_cast<int>(p.bytesDone * 100u / total);
        if (percent != lastPercent)
        {
            lastPercent = percent;
            std::fprintf(stderr, "\r[fukami-jp] unpacking game files: %3d%%", percent);
            if (zenity && !cancelled)
                if (std::fprintf(zenity, "%d\n", percent < 100 ? percent : 99) < 0 || std::fflush(zenity) != 0)
                    cancelled = true;
        }
        return !cancelled;
    });
    std::fprintf(stderr, "\n");
    if (zenity)
    {
        std::fprintf(zenity, "100\n");
        pclose(zenity);
    }
    std::signal(SIGPIPE, previousPipe);
    return result;
}

bool setUpDisc(const fs::path &support, const fs::path &disc, const std::vector<fs::path> &argumentImages)
{
    std::vector<fs::path> candidates = argumentImages;
    if (const char *env = std::getenv("FUKAMI_JP_CHD"); env && *env)
        candidates.emplace_back(env);
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(support, ec))
        if (isChd(entry.path()))
            candidates.push_back(entry.path());
    const std::string intro =
        "Fukami（日本語版）には、お持ちのリッジレーサーV（日本版 SLPS-20001）のディスクイメージ"
        "（CHD、または .cue と .bin）が必要です。初回だけゲームデータ（約" +
        std::to_string((jpRequiredBytes() + 999999) / 1000000) + " MB）を " + support.string() +
        " に展開します。展開後はディスクイメージを削除しても構いません。";
    bool picked = false;
    for (size_t index = 0;; ++index)
    {
        fs::path image;
        if (index < candidates.size())
            image = candidates[index];
        else
        {
            if (!picked)
                message("Fukami へようこそ", intro, false);
            picked = true;
            image = pickImage();
            if (image.empty())
            {
                message("Fukami",
                        "ディスクイメージが選択されませんでした。CHDまたは.cueのパスを引数に指定するか、FUKAMI_JP_CHD を設定するか、" +
                            support.string() + " にファイルを置いてから起動してください。",
                        true);
                return false;
            }
        }
        std::fprintf(stderr, "[fukami-jp] trying disc image %s\n", image.c_str());
        const auto probe = probeJp(image);
        if (probe.status != fukami::disc::Status::ok)
        {
            // Candidates found automatically (arguments, data folder) may be other discs; only report picked ones.
            if (index >= candidates.size())
                message("このディスクイメージは使用できません", image.string() + ": " + statusText(probe), true);
            else
                std::fprintf(stderr, "[fukami-jp] skipped %s: %s\n", image.c_str(), probe.message.c_str());
            continue;
        }
        const auto result = extractWithProgress(image, disc);
        if (result.status == fukami::disc::Status::ok)
        {
            writeMarker(disc);
            if (jpDiscReady(disc))
                return true;
            message("ゲームデータを確認できません", "展開後の確認に失敗しました: " + disc.string(), true);
            return false;
        }
        if (result.status == fukami::disc::Status::cancelled)
            return false;
        message("ゲームデータを展開できません", statusText(result), true);
    }
}

// ---- launch ------------------------------------------------------------------

[[noreturn]] void quit(int code)
{
    std::fflush(nullptr);
    std::_Exit(code);
}

std::optional<fukami::settings::IniDocument> loadSettings(const fs::path &ini, const fs::path &defaults)
{
    std::error_code ec;
    if (!fs::exists(ini, ec))
    {
        fs::create_directories(ini.parent_path(), ec);
        fs::copy_file(defaults, ini, ec);
    }
    auto doc = fukami::settings::IniDocument::load(ini);
    if (!doc)
    {
        message("Fukami", "設定ファイルを読めません: " + ini.string(), true);
        return std::nullopt;
    }
    const auto issues = fukami::settings::check(*doc, false);
    if (issues.empty())
        return effectiveSettings(std::move(*doc), defaults);
    const auto &issue = issues.front();
    const std::string where = issue.line > 0 ? std::to_string(issue.line) + "行目: " : "";
    fs::path bad = ini;
    bad += ".bad";
    fs::remove(bad, ec);
    fs::rename(ini, bad, ec);
    fs::copy_file(defaults, ini, ec);
    message("設定ファイルに問題がありました",
            ini.filename().string() + " " + where + issue.message + "。初期設定に戻しました。元のファイル: " +
                bad.string(),
            true);
    doc = fukami::settings::IniDocument::load(ini);
    if (!doc || !fukami::settings::check(*doc, false).empty())
        return std::nullopt;
    return effectiveSettings(std::move(*doc), defaults);
}

// All display/scene enhancements are verified on the Japanese edition (2026-11-08); this hook stays
// for the next unverified one. FUKAMI_JP_EXPERIMENTAL=1 bypasses it.
void applyJpSafety(fukami::settings::LaunchConfig &c)
{
    if (const char *e = std::getenv("FUKAMI_JP_EXPERIMENTAL"); e && std::strcmp(e, "1") == 0)
    {
        std::fprintf(stderr, "[fukami-jp] FUKAMI_JP_EXPERIMENTAL=1: unverified enhancements follow the settings file\n");
        return;
    }
    (void)c;
    // display.ratio (real widescreen) verified on the Japanese edition 2026-11-08: JP data addresses
    // checked against both executables (codex audit), intro without letterbox, menu, GP race and VS race.
    // game.car_lod, game.draw_distance and game.car_depth_fix verified on the Japanese edition
    // 2026-11-08: addresses in the codex audit, the user's recorded 4-round GP replayed at 16:9 with
    // car_lod 4, draw_distance 2 and the depth fix: no crash, display-list guard engaged, save identical.
    // native_code (29 native hot functions + car vertex loop) and split_gs were verified on the
    // Japanese edition on 2026-11-08 (recorded GP race replay: identical save; identical captures
    // for native code; split GS 1-LSB colour in <0.06 % of pixels, as deterministic as without it).
}

void launchFromApp(const fs::path &exe, const fs::path &root, const std::vector<fs::path> &argumentImages)
{
    const fs::path resources = root / "share";
    const fs::path support = supportDirectory();
    const fs::path disc = support / "disc";
    const fs::path ini = support / "fukami.ini";
    const fs::path mc = support / "mc";
    std::error_code ec;
    fs::create_directories(support, ec);
    if (const char *config = std::getenv("XDG_CONFIG_HOME"); config && config[0] == '/')
        fs::create_directories(config, ec);
    else
        fs::create_directories(fs::path(envOr("HOME", "/tmp")) / ".config", ec);

    if (!jpDiscReady(disc) && !setUpDisc(support, disc, argumentImages))
        quit(1);

    auto doc = loadSettings(ini, resources / "fukami-default.ini");
    if (!doc)
        quit(1);

    auto config = fukami::settings::LaunchConfig::fromIni(*doc, false);
    applyJpSafety(config);
    const bool logging = config.logging || std::getenv("FUKAMI_JP_LOG");
    fs::path session;
    if (logging)
    {
        session = support / "sessions" / (timestamp() + "-" + std::to_string(getpid()));
        fs::create_directories(session, ec);
    }
    fs::create_directories(mc, ec);
    if (logging)
        fs::copy(mc, session / "mc-start", fs::copy_options::recursive, ec);

    fukami::settings::LaunchPaths paths;
    paths.boundWorkload = resources / "workload" / "bound-workload.txt";
    paths.memoryCard = mc;
    paths.session = session;
    paths.libraries = root / "lib";
    paths.home = envOr("HOME", "/");
    paths.path = envOr("PATH", "/usr/local/bin:/usr/bin:/bin");
    paths.tmpdir = envOr("TMPDIR", "/tmp");
    std::vector<std::string> env;
    for (auto &entry : fukami::settings::environment(config, paths))
        if (entry.rfind("DYLD_LIBRARY_PATH=", 0) != 0)
            env.push_back(std::move(entry));
    std::string libraries = paths.libraries.string();
    if (const char *inherited = std::getenv("LD_LIBRARY_PATH"); inherited && *inherited &&
                                                              std::string(inherited) != libraries)
        libraries += std::string(":") + inherited;
    env.push_back("LD_LIBRARY_PATH=" + libraries);
    for (const char *name : {"DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE",
                             "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "DBUS_SESSION_BUS_ADDRESS",
                             "PULSE_SERVER", "PIPEWIRE_RUNTIME_DIR", "SDL_VIDEODRIVER", "SDL_AUDIODRIVER",
                             "SDL_GAMECONTROLLERCONFIG", "SDL_GAMECONTROLLER_IGNORE_DEVICES",
                             "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "SteamDeck", "SteamAppId",
                             "SteamGameId", "STEAM_COMPAT_CLIENT_INSTALL_PATH", "GAMESCOPE_WAYLAND_DISPLAY",
                             "ENABLE_GAMESCOPE_WSI", "LANG", "LC_ALL", "VK_ICD_FILENAMES", "VK_DRIVER_FILES",
                             "VK_INSTANCE_LAYERS", "MESA_VK_WSI_PRESENT_MODE", "FUKAMI_JP_DATA",
                             "FUKAMI_JP_EXPERIMENTAL", "FUKAMI_JP_LOG", "FUKAMI_JP_NO_DIALOGS", "FUKAMI_JP_DISPLAY_SYNC",
                             "RRV_TEST_PRESENT_LIMIT"})
        if (const char *value = std::getenv(name); value && *value)
            env.push_back(std::string(name) + "=" + value);
    // Fast-unpack read-ahead: verified on the Japanese edition 2026-11-08 (46/46 jobs used, identical captures).
    // Display sync: Mailbox by default on Linux (2026-11-08, GNOME Wayland 59.91 Hz, fullscreen, scale 2).
    // FIFO with PCSX2's two swap-chain images let the GS thread wait 10-13 ms per field for the next image:
    // 2760 of 3000 paced fields late and frequent audio re-primes. Mailbox: 28 late, 3 re-primes.
    // FUKAMI_JP_DISPLAY_SYNC=fifo restores FIFO.
    {
        const char *sync = std::getenv("FUKAMI_JP_DISPLAY_SYNC");
        env.push_back(std::string("RRV_PCSX2_GS_DISPLAY_SYNC=") + (sync && std::strcmp(sync, "fifo") == 0 ? "fifo" : "mailbox"));
    }
    env.push_back(std::string("RRV_BOOT_ARGV0=") + kBootArgv0);
    env.push_back(std::string(kChild) + "=1");
    env.push_back(std::string(kIni) + "=" + ini.string());
    env.push_back(std::string(kSupport) + "=" + support.string());

    std::vector<std::string> finalEnv = lastWins(env);
    std::vector<char *> envp;
    for (auto &entry : finalEnv)
        envp.push_back(entry.data());
    envp.push_back(nullptr);

    std::string exePath = exe.string();
    std::string elf = (disc / kElfName).string();
    char *argv[] = {exePath.data(), elf.data(), nullptr};

    if (logging)
        std::fprintf(stderr, "[fukami-jp] starting the game; logs in %s\n", session.c_str());
    std::fflush(nullptr);
    const int out = logging ? open((session / "stdout.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)
                            : open("/dev/null", O_WRONLY | O_CLOEXEC);
    const int err = logging ? open((session / "stderr.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)
                            : open("/dev/null", O_WRONLY | O_CLOEXEC);
    const int savedErr = dup(STDERR_FILENO);
    if (out >= 0)
        dup2(out, STDOUT_FILENO);
    if (err >= 0)
        dup2(err, STDERR_FILENO);
    execve(exePath.c_str(), argv, envp.data());
    const std::string reason = std::strerror(errno);
    if (savedErr >= 0)
        dup2(savedErr, STDERR_FILENO);
    message("ゲームを起動できません", reason, true);
    quit(1);
}

__attribute__((constructor)) void fukamiJpAppEntry(int argc, char **argv, char **)
{
    if (std::getenv(kChild) || std::getenv(kWorkloadVar))
        return;
    if (argv && argc >= 2 && argv[1] && std::strncmp(argv[1], "--print-launch-", 15) == 0)
        return;
    const fs::path exe = executablePath();
    const fs::path root = exe.empty() ? fs::path{} : appRoot(exe);
    if (root.empty())
        return;
    std::vector<fs::path> images;
    for (int i = 1; argv && i < argc; ++i)
        if (argv[i] && isChd(argv[i]))
            images.emplace_back(argv[i]);
    launchFromApp(exe, root, images);
}

} // namespace

namespace fukami::app {

std::filesystem::path defaultIniPath()
{
    if (const char *value = std::getenv("RRV_FUKAMI_DEFAULT_INI"); value && *value)
        return value;
    const fs::path exe = executablePath();
    const fs::path root = exe.empty() ? fs::path{} : appRoot(exe);
    return root.empty() ? fs::path{} : root / "share" / "fukami-default.ini";
}

void restart()
{
    if (!inApp())
        return;
    const fs::path exe = executablePath();
    if (exe.empty())
        return;
    // Drop the child marker and the bound workload so the startup hook runs
    // again and rebuilds the environment from the settings file.
    auto named = [](const char *entry, const char *name) {
        const size_t n = std::strlen(name);
        return std::strncmp(entry, name, n) == 0 && entry[n] == '=';
    };
    std::vector<std::string> env;
    for (char **entry = environ; *entry; ++entry)
        if (!named(*entry, kChild) && !named(*entry, kWorkloadVar))
            env.emplace_back(*entry);
    std::vector<char *> envp;
    for (auto &entry : env)
        envp.push_back(entry.data());
    envp.push_back(nullptr);
    std::string exePath = exe.string();
    char *argv[] = {exePath.data(), nullptr};
    std::fflush(nullptr);
    execve(exePath.c_str(), argv, envp.data());
}

void reveal(const std::filesystem::path &path)
{
    if (path.empty())
        return;
    std::error_code ec;
    std::string target = (fs::is_directory(path, ec) ? path : path.parent_path()).string();
    char tool[] = "xdg-open";
    char *argv[] = {tool, target.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) == 0)
        std::thread([pid] { int status = 0; waitpid(pid, &status, 0); }).detach();
}

} // namespace fukami::app
