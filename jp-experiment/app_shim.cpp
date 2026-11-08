#include "fukami_app.h"
#include <cstdlib>
namespace fukami::app {
static std::filesystem::path envPath(const char* n){const char* s=std::getenv(n);return s ? std::filesystem::path(s) : std::filesystem::path{};}
bool inApp(){return false;} bool canRestart(){return false;} void restart(){}
std::filesystem::path iniPath(){return envPath("RRV_FUKAMI_INI");}
std::filesystem::path defaultIniPath(){return envPath("RRV_FUKAMI_DEFAULT_INI");}
std::filesystem::path savesFolder(){return envPath("RRV_GATE3_MC_ROOT");}
std::filesystem::path logsFolder(){return envPath("RRV_JP_LOG_DIR");}
void reveal(const std::filesystem::path&){}
}
