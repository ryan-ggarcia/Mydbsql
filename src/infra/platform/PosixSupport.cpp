#include "infra/platform/PosixSupport.hpp"

#include <pwd.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

extern char** environ; // NOLINT(readability-redundant-declaration): exigido por posix_spawnp

namespace mydb::infra::platform::posix {

std::filesystem::path homeDirectory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') { // NOLINT(concurrency-mt-unsafe)
        return home;
    }
    long size = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(static_cast<std::size_t>(size > 0 ? size : 16384));
    passwd entry{};
    passwd* found = nullptr;
    if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 && found != nullptr &&
        found->pw_dir != nullptr) {
        return found->pw_dir;
    }
    throw std::runtime_error("Não foi possível descobrir o diretório do usuário");
}

void spawnDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw std::invalid_argument("spawnDetached: argv vazio");
    }
    std::vector<std::string> copies = argv; // posix_spawnp recebe char* não constante
    std::vector<char*> pointers;
    pointers.reserve(copies.size() + 1);
    for (std::string& arg : copies) {
        pointers.push_back(arg.data());
    }
    pointers.push_back(nullptr);

    pid_t pid = 0;
    const int error = ::posix_spawnp(&pid, pointers[0], nullptr, nullptr, pointers.data(), environ);
    if (error != 0) {
        throw std::runtime_error("Não foi possível iniciar " + argv[0] + ": " +
                                 std::strerror(error)); // NOLINT(concurrency-mt-unsafe)
    }
    std::thread([pid] {
        int status = 0;
        while (::waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
    }).detach();
}

} // namespace mydb::infra::platform::posix
