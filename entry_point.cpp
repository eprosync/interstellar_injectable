#define NOMINMAX
#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <dlfcn.h>
    #include <sys/mman.h>
#endif

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>

#include "interstellar/interstellar.hpp"
#include "interstellar/interstellar_signal.hpp"

#include "interstellar/interstellar_bit.hpp"
#include "interstellar/interstellar_coroutine.hpp"
#include "interstellar/interstellar_debug.hpp"
#include "interstellar/interstellar_math.hpp"
#include "interstellar/interstellar_os.hpp"
#include "interstellar/interstellar_string.hpp"
#include "interstellar/interstellar_table.hpp"
#include "interstellar/interstellar_buffer.hpp"

#include "interstellar/interstellar_fs.hpp"
#include "interstellar/interstellar_memory.hpp"
#include "interstellar/interstellar_lxz.hpp"
#include "interstellar/interstellar_iot.hpp"
#include "interstellar/interstellar_sodium.hpp"

std::unique_ptr<std::mutex> mtx;
std::vector<std::string>& get_queue()
{
    static std::vector<std::string> queue = std::vector<std::string>();
    return queue;
}

static std::atomic<bool> deferred = false;
bool runtime(Interstellar::API::lua_State* L, std::chrono::milliseconds tickrate) {
    auto tick_start = std::chrono::steady_clock::now();

    Interstellar::runtime();

    #ifdef _WIN32
        std::unique_lock<std::mutex> guard(*mtx);
        auto& queue = get_queue();
        if (!queue.empty()) {
            for (auto input : queue) {
                if (input == "exit" || input == "quit") {
                    Interstellar::Reflection::close(L);
                    return false;
                }

                auto it = std::find(queue.begin(), queue.end(), input);
                if (it != queue.end()) queue.erase(it);

                guard.unlock();
                std::string err = Interstellar::Reflection::execute(L, input, "@internal");
                if (!err.empty()) {
                    std::cout << "ERROR - " << err << std::endl;
                }
                guard.lock();
            }
        }
        guard.unlock();
    #endif

    auto tick_end = std::chrono::steady_clock::now();
    auto tick_duration = std::chrono::duration_cast<std::chrono::milliseconds>(tick_end - tick_start);

    if (tick_duration < tickrate) {
        std::this_thread::sleep_for(tickrate - tick_duration);
    }

    return true;
}

int module_open()
{
    #ifdef _WIN32
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (!AllocConsole()) {
                return 0;
            }
        }

        TCHAR path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        std::wstring full(path);
        size_t pos = full.find_last_of(L"\\/");
        std::wstring name = (pos != std::wstring::npos) ? full.substr(pos + 1) : full;
        std::wstring title = L"Interstellar Injectable - " + name;
        SetConsoleTitle(title.c_str());
        
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        std::ios::sync_with_stdio();
    #endif

    int errs = Interstellar::init("");
    if (errs > 0) {
        std::cout << "Failed to initialize program: " << errs << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return 0;
    }

    // Core
    Interstellar::Reflection::api();
    Interstellar::Signal::api();
    Interstellar::Coroutine::api();
    Interstellar::Buffer::api();
    Interstellar::String::api();
    Interstellar::Debug::api();
    Interstellar::Table::api();
    Interstellar::Math::api();
    Interstellar::OS::api();

    // Extensions
    Interstellar::FS::api(Interstellar::FS::pwd());
    Interstellar::Memory::api();
    Interstellar::LXZ::api();
    Interstellar::IOT::api();
    Interstellar::Sodium::api();

    {
        using namespace Interstellar;

        Signal::add_error("entry_point", [](API::lua_State* L, std::string name, std::string identity, std::string error) {
            std::string state_name = Tracker::get_name(L);
            std::cout << "[" << state_name << "] [signal." << name << "." << identity << "] " << error << std::endl;
            });

        Reflection::Task::add_error("entry_point", [](API::lua_State* L, std::string error) {
            std::string state_name = Tracker::get_name(L);
            std::cout << "[" << state_name << "] [task] " << error << std::endl;
            });

        FS::add_error("entry_point", [](API::lua_State* L, std::string error) {
            std::string state_name = Tracker::get_name(L);
            std::cout << "[" << state_name << "] [fs] " << error << std::endl;
            });

        LXZ::add_error("entry_point", [](API::lua_State* L, std::string error) {
            std::string state_name = Tracker::get_name(L);
            std::cout << "[" << state_name << "] [lxz] " << error << std::endl;
            });

        IOT::add_error("entry_point", [](API::lua_State* L, std::string type, std::string error) {
            std::string state_name = Tracker::get_name(L);
            std::cout << "[" << state_name << "] [iot." << type << "] " << error << std::endl;
            });
    }

    std::cout << "Interstellar - ";

    #if defined(_WIN32)
            std::cout << "Windows ";
    #elif defined(__linux__)
            std::cout << "Linux ";
    #endif

    #if defined(__x86_64__) || defined(_M_X64)
            std::cout << "x64 ";
    #elif defined(__i386__) || defined(_M_IX86)
            std::cout << "x86 ";
    #elif defined(__arm__)
            std::cout << "ARM ";
    #elif defined(__aarch64__)
            std::cout << "AARCH64 ";
    #endif

    std::cout << "- " __TIME__ " " __DATE__ << " - " << LUAJIT_VERSION << std::endl;
    std::cout << "Ctrl+C or 'exit' or 'quit' to escape this program." << std::endl;

    #ifdef _WIN32
        deferred = false;
        mtx = std::make_unique<std::mutex>();

        std::thread([]() {
            std::string input;

            while (true) {
                std::getline(std::cin, input);
                std::cout << "> " << input << std::endl;
                std::lock_guard<std::mutex> guard(*mtx);
                auto& queue = get_queue();
                queue.push_back(input);
                if (input == "exit" || input == "quit") {
                    break;
                }
            }
        }).detach();
    #endif

    std::thread([]() {
        auto L = Interstellar::Reflection::open("main", true, false);
        static std::chrono::milliseconds tickrate(16);

        {
            using namespace Interstellar::API;
            lua::pushvalue(L, indexer::global);
            lua::getfield(L, -1, "task");
            lua::remove(L, -2);

            lua::pushcfunction(L, [](lua_State* L) {
                deferred = luaL::checkboolean(L, 1);
                return 0;
            });
            lua::setfield(L, -2, "deferred");

            lua::pushcfunction(L, [](lua_State* L) {
                if (deferred) runtime(L, tickrate);
                return 0;
            });
            lua::setfield(L, -2, "invoke");

            lua::pushcfunction(L, [](lua_State* L) {
                Interstellar::Memory::push_address(L, &runtime);
                return 1;
            });
            lua::setfield(L, -2, "subroutine");

            lua::pop(L);
        }

        {
            std::string filename = "init.lua";
            std::ifstream file(filename);
            if (file) {
                std::string line;
                std::stringstream data;
                while (std::getline(file, line)) {
                    data << line << std::endl;
                }

                std::string err = Interstellar::Reflection::execute(L, data.str(), "@" + filename);
                if (err.size() > 0) {
                    std::cout << "ERROR - " << err << std::endl;
                }
            }
        }

        while (true) {
            if (!deferred) runtime(L, tickrate);
        }
    }).detach();

    return 0;
}

int module_close()
{
    using namespace Interstellar;

    auto list = Tracker::get_states();
    for (auto& state : list) {
        bool is_created = !Tracker::is_internal(state.second);
        Tracker::pre_remove(state.second);
        if (is_created) Reflection::close(state.second);
        Tracker::post_remove(state.second);
    }

    return 0;
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    DisableThreadLibraryCalls(module);

    if (reason == DLL_PROCESS_ATTACH) {
        module_open();
        return 1;
    }
    else if (reason == DLL_PROCESS_DETACH) {
        module_close();
        return 1;
    }

    return 0;
}
#else
__attribute__((constructor))
void onLibraryLoad() {
    module_open();
}

__attribute__((destructor))
void onLibraryUnload() {
    module_close();
}
#endif