#ifndef WAYLYRICS_COMMON_H
#define WAYLYRICS_COMMON_H

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

// 全局互斥锁，保护日志输出原子性
inline std::mutex log_mutex;

// 时间戳生成函数（线程安全，使用 localtime_r）
inline std::string getCurrentTimeStr() {
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) %
                1000;

  // 使用线程安全的 localtime_r（POSIX 标准，避免全局缓冲区竞争）
  std::tm local_time{};
  if (!localtime_r(&now_time_t, &local_time)) {
    return "";
  }

  std::stringstream ss;
  ss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3)
     << std::setfill('0') << now_ms.count();
  return ss.str();
}

// 日志级别定义（未修改）
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_DEBUG 3

// 根据编译参数设置日志级别（未修改）
#if defined(DEBUG_ENABLED)
#define LOG_LEVEL LOG_LEVEL_DEBUG
#elif defined(WARN_ENABLED)
#define LOG_LEVEL LOG_LEVEL_WARN
#elif defined(ERROR_ENABLED)
#define LOG_LEVEL LOG_LEVEL_ERROR
#else
#define LOG_LEVEL LOG_LEVEL_NONE
#endif

// 通用日志宏（添加互斥锁保护输出原子性）
#define LOG_PRINT(level, tag, format, ...)                                     \
  do {                                                                         \
    if (LOG_LEVEL >= level) {                                                  \
      std::string timeStr = getCurrentTimeStr();                               \
      std::lock_guard<std::mutex> lock(log_mutex);                             \
      fprintf(stderr, "[%s] [%s %s] [%s] %s:%d(%s): " format "\n",             \
              timeStr.c_str(), BUILD_VERSION, BUILD_TIME, tag, __FILE__,       \
              __LINE__, __func__, ##__VA_ARGS__);                              \
    }                                                                          \
  } while (0)

// 各级别日志宏（未修改）
#define DEBUG(format, ...)                                                     \
  LOG_PRINT(LOG_LEVEL_DEBUG, "DEBUG", format, ##__VA_ARGS__)
#define WARN(format, ...)                                                      \
  LOG_PRINT(LOG_LEVEL_WARN, "WARN", format, ##__VA_ARGS__)
#define ERROR(format, ...)                                                     \
  LOG_PRINT(LOG_LEVEL_ERROR, "ERROR", format, ##__VA_ARGS__)
#define INFO(format, ...)                                                      \
  LOG_PRINT(LOG_LEVEL_ERROR, "INFO", format, ##__VA_ARGS__)

#endif // WAYLYRICS_COMMON_H