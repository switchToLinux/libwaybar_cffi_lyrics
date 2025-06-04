#ifndef WAYLYRICS_COMMON_H
#define WAYLYRICS_COMMON_H
// Filename: common.h
// Description: 通用头文件，包含版本信息、日志宏定义等
// Author: awkee
// Date: 2025-06-04
///////////////////////////////////////////////////////

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

/*
 * 日志输出宏定义
 *   输出模式: 
 *     方式一: 通过全局变量 log_level 控制 DEBUG 日志输出(用于调试分析)
 *     方式二: 通过编译选项 DEBUG_ENABLED, WARN_ENABLED, ERROR_ENABLED 控制输出级别
 *
 *   输出格式:
 *     [时间戳] [版本号 编译时间] [日志级别] [文件名:行号(函数名)]: 日志内容
 *
 *   日志级别：
 *     打印内容从多到少: DEBUG > WARN > ERROR > NONE
 */

// 全局互斥锁，保护日志输出原子性
static std::mutex log_mutex;

// 时间戳生成函数（线程安全，使用 localtime_r）
inline std::string getCurrentTimeStr() {
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

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

// 日志级别定义
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_DEBUG 3

// 全局变量控制DEBUG日志开关（默认关闭）
extern int log_level;

#if defined(DEBUG_ENABLED)
#define LOG_LEVEL LOG_LEVEL_DEBUG
#elif defined(WARN_ENABLED)
#define LOG_LEVEL LOG_LEVEL_WARN
#elif defined(ERROR_ENABLED)
#define LOG_LEVEL LOG_LEVEL_ERROR
#else
#define LOG_LEVEL LOG_LEVEL_NONE
#endif


// 通用日志宏（DEBUG日志由 enable_debug 控制，其他级别始终输出）
#define LOG_PRINT(level, tag, format, ...)                                     \
  do {                                                                         \
    if (log_level >= level) {                            \
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