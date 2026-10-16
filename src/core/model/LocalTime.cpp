#include "core/model/LocalTime.hpp"

#include <cstdlib>
#include <ctime>
#include <mutex>

namespace mydb::core::model {

namespace {

std::chrono::minutes minutesSinceEpoch(const std::tm& time) {
    using namespace std::chrono;
    const year_month_day date{year{time.tm_year + 1900}, month{static_cast<unsigned>(time.tm_mon + 1)},
                              day{static_cast<unsigned>(time.tm_mday)}};
    return duration_cast<minutes>(sys_days{date}.time_since_epoch()) + hours{time.tm_hour} + minutes{time.tm_min};
}

} // namespace

std::chrono::minutes localUtcOffset(std::chrono::system_clock::time_point at) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(at);
    std::tm local{};
    std::tm utc{};
    {
        // std::localtime e std::gmtime podem devolver o mesmo buffer estático: cada resultado é copiado
        // antes da próxima chamada, sob uma trava. (O C++20 do GCC 12 ainda não tem std::chrono::current_zone.)
        static std::mutex mutex;
        const std::scoped_lock lock(mutex);
        const std::tm* converted = std::localtime(&seconds);
        if (converted == nullptr) {
            return std::chrono::minutes{0};
        }
        local = *converted;
        converted = std::gmtime(&seconds);
        if (converted == nullptr) {
            return std::chrono::minutes{0};
        }
        utc = *converted;
    }
    return minutesSinceEpoch(local) - minutesSinceEpoch(utc);
}

std::string formatUtcOffset(std::chrono::minutes offset) {
    constexpr int kMinutesPerHour = 60;
    constexpr int kTen = 10;
    const auto total = static_cast<int>(offset.count());
    const int magnitude = std::abs(total);
    const int hours = magnitude / kMinutesPerHour;
    const int minutes = magnitude % kMinutesPerHour;
    std::string text(1, total < 0 ? '-' : '+');
    const auto twoDigits = [&text](int value) {
        text += static_cast<char>('0' + (value / kTen));
        text += static_cast<char>('0' + (value % kTen));
    };
    twoDigits(hours);
    text += ':';
    twoDigits(minutes);
    return text;
}

} // namespace mydb::core::model
