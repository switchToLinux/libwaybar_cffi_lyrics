#include <chrono>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <regex>
#include <sdbus-c++/TypeTraits.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <thread>

/*
 * @function listDBusNames
 * @brief 列出所有匹配 includeExpr字符串 并且 不匹配 excludeExpr字符串 的 D-Bus 名称
 * @param includeExpr 包含的名称字符串，默认为 "org.mpris.MediaPlayer2"
 * @param excludeExpr 排除的名称字符串，默认为 "playerctld"
 * @return std::vector<std::string> 包含所有名称的字符串向量
 */
std::vector<std::string> listDBusNames(
    std::string includeExpr = "org.mpris.MediaPlayer2",
    std::string excludeExpr = "playerctld") {
  // 连接到 session bus
  auto connection = sdbus::createSessionBusConnection();

  // 创建 org.freedesktop.DBus 的代理对象
  sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
  sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
  auto proxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                  org_freedesktop_DBus_path);
  // 调用 ListNames 方法
  std::vector<std::string> names;
  proxy->callMethod("ListNames")
      .onInterface("org.freedesktop.DBus")
      .storeResultsTo(names);

      std::vector<std::string> filteredNames;
  for (const auto &name : names) {
    if(includeExpr.empty() && excludeExpr.empty()) {
      filteredNames.push_back(name);
      continue;
    } else if(includeExpr.empty()) {
      if(name.find(excludeExpr) == std::string::npos) {
        filteredNames.push_back(name);
      }
    } else if(excludeExpr.empty()) {
      if(name.find(includeExpr) == 0) {
        filteredNames.push_back(name);
      }
    } else {
      if(name.find(includeExpr) == 0 &&
        name.find(excludeExpr) == std::string::npos) {
        filteredNames.push_back(name);
      }
    }
  }
  return filteredNames;
}
inline std::tuple<std::string, std::string, std::string, int, int, bool>
getNowPlaying(std::string serviceName) {
  // 连接到 session bus（保持单次调用独立连接，避免全局状态污染）
  auto connection = sdbus::createSessionBusConnection();

  // 创建目标播放器的代理对象（关键：使用传入的serviceName）
  sdbus::ServiceName destination{serviceName};
  sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"}; // 标准MPRIS对象路径
  auto proxy = sdbus::createProxy(*connection, destination, objectPath);
  if (!proxy) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus proxy creation failed for service: " << serviceName
              << std::endl;
    return {"", "", "", 0, 0, false};
  }

  int64_t position = 0;
  std::string title;
  std::vector<std::string> artists;
  std::string textLyrics; // 明确表示歌词字段
  int64_t length = 0;

  try {
    // 1. 获取播放状态（细化状态判断）
    sdbus::Variant playbackStatus;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
        .storeResultsTo(playbackStatus);

    const std::string status = playbackStatus.get<std::string>();
    if (status != "Playing") {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Player " << serviceName << " status: " << status
                << " (not Playing)" << std::endl;
      return {status, "", "", 0, 0, false}; // 返回实际状态值供上层处理
    }
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error (status query) for " << serviceName << ": "
              << e.getMessage() << std::endl;
    return {"Error", "", "", 0, 0, false}; // 明确错误类型
  }

  try {
    // 2. 获取媒体元数据（优化字段解析逻辑）
    sdbus::Variant metadata;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Metadata")
        .storeResultsTo(metadata);

    auto md = metadata.get<std::map<std::string, sdbus::Variant>>();

    // 解析标题（优先使用xesam:title）
    if (md.count("xesam:title")) {
      title = md["xesam:title"].get<std::string>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: xesam:title missing in " << serviceName
                << " metadata" << std::endl;
      title = "[Unknown Title]"; // 提供默认值避免空字符串
    }

    // 解析歌词（修正之前错误：将asText赋给textLyrics而不是title）
    if (md.count("xesam:asText")) {
      textLyrics = md["xesam:asText"].get<std::string>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: xesam:asText missing in " << serviceName
                << " metadata" << std::endl;
      textLyrics = ""; // 明确无歌词
    }

    // 解析艺术家（优先使用artist，降级使用albumArtist）
    if (md.count("xesam:artist")) {
      artists = md["xesam:artist"].get<std::vector<std::string>>();
    } else if (md.count("xesam:albumArtist")) {
      artists = md["xesam:albumArtist"].get<std::vector<std::string>>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: xesam:artist/albumArtist missing in "
                << serviceName << " metadata" << std::endl;
      artists = {"[Unknown Artist]"}; // 提供默认值
    }

    // 解析媒体长度（处理可能的类型转换问题）
    if (md.count("mpris:length")) {
      try {
        length = md["mpris:length"].get<int64_t>(); // 显式转换确保类型正确
      } catch (const std::exception &e) {
        std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                  << "Error parsing mpris:length in " << serviceName << ": "
                  << e.what() << std::endl;
        length = 0;
      }
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: mpris:length missing in " << serviceName
                << " metadata" << std::endl;
      length = 0;
    }
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error (metadata query) for " << serviceName << ": "
              << e.getMessage() << std::endl;
    return {title, artists.empty() ? "" : artists[0], textLyrics, 0, 0, false};
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "Metadata parsing error for " << serviceName << ": "
              << e.what() << std::endl;
    return {title, artists.empty() ? "" : artists[0], textLyrics, 0, 0, false};
  }

  try {
    // 3. 获取播放位置（添加单位转换注释）
    sdbus::Variant posVar;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Position")
        .storeResultsTo(posVar);
    position = posVar.get<int64_t>(); // 原单位是微秒（μs）
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error (position query) for " << serviceName << ": "
              << e.getMessage() << std::endl;
    return {title, artists.empty() ? "" : artists[0], textLyrics, 0, 0, false};
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "Position parsing error for " << serviceName << ": "
              << e.what() << std::endl;
    return {title, artists.empty() ? "" : artists[0], textLyrics, 0, 0, false};
  }

  // 转换微秒 → 毫秒（1ms=1000μs）
  return {
      title,
      artists.empty() ? "[Unknown Artist]" : artists[0], // 确保非空
      textLyrics,
      static_cast<int>(position / 1000), // 微秒转毫秒
      static_cast<int>(length / 1000),   // 微秒转毫秒
      true                               // 明确表示成功获取数据
  };
}

std::vector<std::string> split(std::string s, std::string delimiter) {
  size_t pos_start = 0, pos_end, delim_len = delimiter.length();
  std::string token;
  std::vector<std::string> res;

  while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
    token = s.substr(pos_start, pos_end - pos_start);
    pos_start = pos_end + delim_len;
    res.push_back(token);
  }

  res.push_back(s.substr(pos_start));
  return res;
}
static constexpr char *ws = (char *)" \t\n\r\f\v";
// trim from end of string (right)
inline std::string &rtrim(std::string &s, const char *t = ws) {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

// trim from beginning of string (left)
inline std::string &ltrim(std::string &s, const char *t = ws) {
  s.erase(0, s.find_first_not_of(t));
  return s;
}

inline std::string &trim(std::string &s, const char *t = ws) {
  return ltrim(rtrim(s, t), t);
}

std::string getSyncedLine(uint64_t pos,
                                 const std::string &syncedLyrics) {
  auto strVec = split(syncedLyrics, "\n");
  auto len = strVec.size();

  size_t index = 0;
  for (size_t i = 0; i < len; i++) {
    auto &cur = strVec[i];
    auto start = cur.find("[");
    auto mins_s = std::string_view(cur.data() + start + 1, 2);
    auto secs_s = std::string_view(cur.data() + start + 4, 5);

    auto secs = (atoi(mins_s.data()) * 60) + atof(secs_s.data());
    auto ms = secs * 1000;
    if (pos > ms) {
      index = i;
    }
  }

  auto str = strVec[index];
  str = str.substr(10, str.size());
  return trim(str);
}

inline std::optional<std::tuple<std::string, int64_t, int64_t>>
getCurrentLine(std::string serviceName) {
  std::string line = "";
  auto [title, artists, ret, pos, dur, ok] = getNowPlaying(serviceName);
  if (!ok) {                             // 非 playing 状态
    return std::make_tuple(title, 0, 0); // 此时 title 表示的状态信息
  }
  if (ret.size()) {
    try {
      line = getSyncedLine(pos, ret);
    } catch (const std::exception &e) {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Error processing lyrics json: " << e.what() << std::endl;
    }
  } else {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "No lyrics for [" << title << "|" << artists << "|" << pos
              << "|" << dur << "|" << ok << "]" << std::endl;
  }
  std::string lineWithTitle = "《" + title + "》-" + artists;
  if (!line.empty()) {
    lineWithTitle += ":" + line;
  }
  return std::make_tuple(lineWithTitle, pos, dur);
}
/*
 * @function handlePlayerPropertiesChanged
 * @brief 处理播放器属性变化的回调函数（核心状态监听逻辑）
 * @param serviceName 播放器的D-Bus服务名
 * @param changedProps 变化的属性集合
 */
void handlePlayerPropertiesChanged(
    const std::string &serviceName,
    const std::map<std::string, sdbus::Variant> &changedProps) {
  std::cout << "\n[Player Status Changed] " << serviceName << ":\n";

  // 遍历所有变化的属性
  for (const auto &[prop, value] : changedProps) {
    if (prop == "PlaybackStatus") { // 播放状态变化（Playing/Paused/Stopped）
      const std::string status = value.get<std::string>();
      std::cout << "  PlaybackStatus: " << status << "\n";
      // 这里可以触发UI更新或其他业务逻辑
    } else if (prop == "Metadata") { // 媒体元数据变化（标题/艺术家等）
      auto metadata = value.get<std::map<std::string, sdbus::Variant>>();
      std::cout << "  Metadata Updated:\n";
      if (metadata.count("xesam:title")) {
        const std::string title = metadata["xesam:title"].get<std::string>();
        std::cout << "    Title: " << title << "\n";
      }
      if (metadata.count("xesam:artist")) {
        const auto artists = metadata["xesam:artist"].get<std::vector<std::string>>();
        std::cout << "    Artist: " << (artists.empty() ? "Unknown" : artists[0]) << "\n";
      }
    } else if (prop == "Position") { // 播放位置变化（微秒级更新）
      const int64_t positionUs = value.get<int64_t>();
      const int positionMs = static_cast<int>(positionUs / 1000); // 转换为毫秒
      std::cout << "  Current Position: " << positionMs << "ms\n";
    }
  }
  std::cout << "--------------------------\n";
}

/*
 * @function onNameOwnerChanged
 * @brief 回调函数用于 NameOwnerChanged 信号： 快速识别音乐播放器的启动和退出
 * @param name 服务名称
 * @param oldOwner 旧的所有者名称
 * @param newOwner 新的所有者名称
 * @return void
 */
void onNameOwnerChanged(std::string name, std::string oldOwner,
                        std::string newOwner) {
  std::cout << "NameOwnerChanged signal received:\n"
            << "  Service: " << name << "\n"
            << "  Old owner: " << (oldOwner.empty() ? "none" : oldOwner) << "\n"
            << "  New owner: " << (newOwner.empty() ? "none" : newOwner)
            << "\n";

  if (newOwner.empty()) {
    std::cout << "Music player (" << name << ") has exited.\n";
  } else if (oldOwner.empty()) {
    std::cout << "Music player (" << name << ") has started.\n";
    
    // TODO: 为新启动的播放器创建独立代理，监听PropertiesChanged信号

  } else {
    std::cout << "Music player (" << name << ") ownership changed.\n";
  }
  std::cout << "Current music players:\n";
  for (const auto &name : listDBusNames()) {
    std::cout << "- " << name << "\n";
    auto [title, artist, textLyrics, pos, dur, ok] = getNowPlaying(name);
    if (ok) {
      std::cout << "  Title: " << title << "\n"
                << "  Artist: " << artist << "\n"
                << "  Position: " << pos << "ms\n"
                << "  Duration: " << dur << "ms\n";
      if (!textLyrics.empty()) {
        std::cout << "  Lyrics: " << textLyrics << "\n";
        if (!textLyrics.empty()) {
          std::cout << "  Lyrics: " << textLyrics << "\n";
          auto d = getCurrentLine(name);
          if (d.has_value()) {
            auto [line, pos, dur] = d.value();
            std::cout << "  Current line: " << line << "\n";
          } else {
            std::cout << "  Lyrics: No lyrics available\n";
          }
        }
      } else {
        std::cout << "  Lyrics: No lyrics available\n";
        // TODO: 通过网络下载歌词
      }
    }
  }
  std::cout << "\n--------------------------\n\n";
}

void watchPlayerStatus() {
  // Create a D-Bus connection to the session bus
  auto connection = sdbus::createSessionBusConnection();

  // Create a proxy object for the org.freedesktop.DBus service
  sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
  sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
  auto proxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                  org_freedesktop_DBus_path);

  // 注册 NameOwnerChanged 信号处理器
  proxy->uponSignal("NameOwnerChanged")
      .onInterface("org.freedesktop.DBus")
      .call([&](const std::string &name, const std::string &oldOwner,
                const std::string &newOwner) {
        // 直接传递解析后的参数, 只选择name匹配为 org.mpris.MediaPlayer2.*
        if (name.find("org.mpris.MediaPlayer2.") == 0) {
          onNameOwnerChanged(name, oldOwner, newOwner);
        }
      });
  // TODO: 注册 PropertiesChanged 信号监听器
  // 监听所有 org.mpris.MediaPlayer2.* 服务的 PropertiesChanged 信号
  // proxy->uponSignal("PropertiesChanged")
  //     .onInterface("org.freedesktop.DBus")
  //     .call([](const std::string &interfaceName,
  //                    const std::map<std::string, sdbus::Variant> &changedProps,
  //                    const std::vector<std::string> &invalidatedProps) {
  //         if (interfaceName ==
  //             "org.mpris.MediaPlayer2.Player") { // 只处理播放器相关属性
  //           handlePlayerPropertiesChanged(name, changedProps);
  //         }
  //       });

  // 启动事件循环线程
  std::thread eventLoop([&connection]() { connection->enterEventLoop(); });

  // Keep the main thread running
  std::cout << "Monitoring music player D-Bus name changes. Press Ctrl+C to "
               "exit.\n";
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Clean up (unreachable in this example due to infinite loop)
  eventLoop.join();
}
int main() {
  try {
    std::cout << "Current music players:\n";
    for (const auto &name : listDBusNames()) {
      std::cout << "- " << name << "\n";
      auto [title, artist, textLyrics, pos, dur, ok] = getNowPlaying(name);
      if (ok) {
        std::cout << "  Title: " << title << "\n"
                  << "  Artist: " << artist << "\n"
                  << "  Position: " << pos << "ms\n"
                  << "  Duration: " << dur << "ms\n";
        if (!textLyrics.empty()) {
          std::cout << "  Lyrics: " << textLyrics << "\n";
          auto d = getCurrentLine(name);
          if (d.has_value()) {
            auto [line, pos, dur] = d.value();
            std::cout << "  Current line: " << line << "\n";
          } else {
            std::cout << "  Lyrics: No lyrics available\n";
          }
        }
      }
    }
    std::cout << "\n--------------------------\n\n";

    watchPlayerStatus();
  } catch (const sdbus::Error &e) {
    std::cerr << "sdbus error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Standard error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}