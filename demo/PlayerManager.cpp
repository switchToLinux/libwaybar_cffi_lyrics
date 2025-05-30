#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include <iostream>
#include <utility>
#include <vector>


// 前置声明回调类型（用于通知上层状态变化）
using PlayerStatusCallback = std::function<void(
    const std::string &serviceName, // 播放器服务名
    const std::string &status,      // 播放状态（Playing/Paused/Stopped）
    const std::string &title,       // 媒体标题
    const std::string &artist,      // 艺术家
    int positionMs,                 // 当前播放位置（毫秒）
    int durationMs                  // 总时长（毫秒）
    )>;

std::tuple<std::string, std::string, std::string, int, int, bool>
getNowPlaying(std::string serviceName) {
  // 连接到 session bus（保持单次调用独立连接，避免全局状态污染）
  auto connection = sdbus::createSessionBusConnection();

  // 创建目标播放器的代理对象（关键：使用传入的serviceName）
  sdbus::ServiceName destination{serviceName};
  sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"}; // 标准MPRIS对象路径
  auto proxy =
      sdbus::createProxy(*connection, std::move(destination), std::move(objectPath));
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

class PlayerManager {
public:
  explicit PlayerManager(const std::string &cacheDir = "")
      : cacheDir_(cacheDir) {
    initDBusConnection();
    watchNameOwnerChanges();
  }

  ~PlayerManager() {
    // 清理所有播放器代理
    players_.clear();
    if (connection_) {
      connection_->leaveEventLoop();
    }
  }

  // 获取当前所有活动的播放器服务名
  std::vector<std::string> getActivePlayers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto &[name, _] : players_) {
      names.push_back(name);
    }
    return names;
  }

  // 设置状态变化回调（由上层处理具体业务逻辑）
  void setStatusCallback(PlayerStatusCallback callback) {
    statusCallback_ = callback;
  }

private:
  // 初始化D-Bus连接
  void initDBusConnection() {
    try {
      connection_ = sdbus::createSessionBusConnection();
      sdbus::ServiceName destination{"org.freedesktop.DBus"};
      sdbus::ObjectPath objectPath{"/org/freedesktop/DBus"};
      dbusProxy_ = sdbus::createProxy(*connection_, destination, objectPath);
      std::cout << "D-Bus connection initialized" << std::endl;
    } catch (const sdbus::Error &e) {
      std::cerr << "D-Bus init error: " << e.what() << std::endl;
    }
  }

  // 监听NameOwnerChanged信号（全局播放器生命周期变化）
  void watchNameOwnerChanges() {
    if (!dbusProxy_)
      return;

    dbusProxy_->uponSignal("NameOwnerChanged")
        .onInterface("org.freedesktop.DBus")
        .call([this](const std::string &name, const std::string &oldOwner,
                     const std::string &newOwner) {
          if (name.find("org.mpris.MediaPlayer2.") != 0)
            return;

          std::lock_guard<std::mutex> lock(mutex_);
          if (newOwner.empty()) {
            // 播放器退出：从管理列表移除
            players_.erase(name);
            std::cout << "Player exited: " << name << std::endl;
          } else if (oldOwner.empty()) {
            // 新播放器启动：创建代理并监听属性变化
            addNewPlayer(name);
          }
        });

    // 启动事件循环
    eventLoopThread_ = std::thread([this]() { connection_->enterEventLoop(); });
  }

  // 添加新播放器并注册属性监听
  void addNewPlayer(const std::string &serviceName) {
    try {
      // 创建播放器实例代理
      sdbus::ServiceName destination{std::move(serviceName)};
      sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
      auto playerProxy =
          sdbus::createProxy(*connection_, std::move(destination), std::move(objectPath));

      // 注册PropertiesChanged信号监听（修正参数顺序）
      playerProxy->uponSignal("PropertiesChanged")
          .onInterface("org.freedesktop.DBus.Properties")
          .call([this, serviceName](
                    const std::string &interfaceName,
                    const std::map<std::string, sdbus::Variant> &changedProps,
                    const std::vector<std::string> &invalidatedProps) {
            std::cout << "Properties changed for " << serviceName << ": " << std::endl;
            if (interfaceName != "org.mpris.MediaPlayer2.Player") {
              return;
            }
            handlePlayerPropertiesChange(serviceName, changedProps);
          });

      // 完成信号注册并存储代理
      players_[serviceName] = std::move(playerProxy);
      std::cout << "New player added: " << serviceName << std::endl;
      // 初始状态获取
      updatePlayerStatus(serviceName);
    } catch (const sdbus::Error &e) {
      std::cerr << "Player proxy init error: " << e.what() << std::endl;
    }
  }

  // 处理单个播放器的属性变化
  void handlePlayerPropertiesChange(
      const std::string &serviceName,
      const std::map<std::string, sdbus::Variant> &changedProps) {
    // 解析变化的属性并更新状态
    std::string status;
    std::string title;
    std::string artist;
    int positionMs = 0;
    int durationMs = 0;

    for (const auto &[prop, value] : changedProps) {
      if (prop == "PlaybackStatus") {
        status = value.get<std::string>();
        std::cout << "Player status changed: " << serviceName << " -> " << status << std::endl;
      } else if (prop == "Metadata") {
        auto metadata = value.get<std::map<std::string, sdbus::Variant>>();
        std::cout << "Metadata updated for " << serviceName << ": " << std::endl;
        if (metadata.count("xesam:title")) {
          title = metadata["xesam:title"].get<std::string>();
        }
        if (metadata.count("xesam:artist")) {
          auto artists =
              metadata["xesam:artist"].get<std::vector<std::string>>();
          artist = artists.empty() ? "Unknown" : artists[0];
        }
        if (metadata.count("mpris:length")) {
          durationMs =
              static_cast<int>(metadata["mpris:length"].get<int64_t>() / 1000);
        }
      } else if (prop == "Position") {
        std::cout << "Position updated for " << serviceName << ": " << std::endl;
        positionMs = static_cast<int>(value.get<int64_t>() / 1000);
      } else {
        std::cout << "Unknown property changed: " << serviceName << "." << prop << std::endl;
        return;
      }
    }

    // 触发上层回调
    if (statusCallback_) {
      statusCallback_(serviceName, status, title, artist, positionMs,
                      durationMs);
    }
  }

  // 初始状态获取（补充首次连接时的状态）
  void updatePlayerStatus(const std::string &serviceName) {
    auto [title, artist, textLyrics, pos, dur, ok] = getNowPlaying(serviceName);
    if (ok) {
      handlePlayerPropertiesChange(
          serviceName,
          {{"PlaybackStatus", sdbus::Variant("Playing")},
           {"Metadata",
            sdbus::Variant(getMetadataMap(title, artist, dur * 1000))},
           {"Position", sdbus::Variant(static_cast<int64_t>(pos * 1000))}});
    }
  }

  // 辅助函数：构造元数据map（用于初始状态同步）
  std::map<std::string, sdbus::Variant>
  getMetadataMap(const std::string &title, const std::string &artist,
                 int64_t lengthUs) {
    std::map<std::string, sdbus::Variant> metadata;
    metadata["xesam:title"] = sdbus::Variant(title);
    metadata["xesam:artist"] = sdbus::Variant(std::vector<std::string>{artist});
    metadata["mpris:length"] = sdbus::Variant(lengthUs);
    return metadata;
  }

private:
  std::unique_ptr<sdbus::IConnection> connection_;
  std::unique_ptr<sdbus::IProxy> dbusProxy_; // 全局D-Bus代理
  std::map<std::string, std::unique_ptr<sdbus::IProxy>>
      players_; // 管理中的播放器代理
  std::thread eventLoopThread_;
  PlayerStatusCallback statusCallback_;
  std::mutex mutex_; // 保护players_的线程安全
  std::string cacheDir_;
};

int main() {
  PlayerManager playerManager;

  // 设置状态变化回调（上层业务逻辑）
  playerManager.setStatusCallback(
      [](const std::string &serviceName, const std::string &status,
         const std::string &title, const std::string &artist, int positionMs,
         int durationMs) {
        std::cout << "\n[Player Update] " << serviceName << ":\n"
                  << "  Status: " << status << "\n"
                  << "  Title: " << title << "\n"
                  << "  Artist: " << artist << "\n"
                  << "  Position: " << positionMs << "ms/" << durationMs << "ms\n" << std::endl;
      });

  // 主线程保持运行
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}