#include <iostream>
#include <memory>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/TypeTraits.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>
#include "../include/common.h"

// 播放器状态枚举（播放/暂停/停止）
enum class PlaybackStatus { Playing, Paused, Stopped, Unknown };

// 播放器元数据（包含歌曲名、艺术家、歌词等）
struct PlayerMetadata {
  std::string trackId; // 歌曲唯一ID（用于缓存）
  std::string title;   // 歌曲名
  std::string artist;  // 艺术家
  std::string album;   // 专辑
  std::string lyrics;  // 歌词内容（仅musicfox直接从dbus获取，其他查询网络获取）
  std::int64_t length; // 歌曲时长（毫秒）
};

enum class LoopStatus {
  None,    // 不循环
  Track,   // 单曲循环
  Playlist // 列表循环
};

// 播放器状态信息（整合D-Bus属性）
struct PlayerState {
  PlaybackStatus status;   // 播放状态
  PlayerMetadata metadata; // 元数据
  uint64_t position;       // 当前播放位置（毫秒）
  std::string playerName;  // 播放器名称（用于区分）
};

// 播放器状态变化的回调函数类型
using PlayerChangeCallback =
    std::function<void(const std::string &playerName, bool added)>;

std::vector<std::string> listDBusNames() {
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

  return names;
}

void onConcatenated(sdbus::Signal signal) {
  std::string concatenatedString;
  signal >> concatenatedString;

  std::cout << "Received signal with concatenated string " << concatenatedString
            << std::endl;
}

// 播放器管理器类
class MprisPlayerManager {
public:
  MprisPlayerManager() {
    // 初始化时获取现有播放器
    refreshPlayers();

    try {
      // 创建D-Bus连接
      auto connection = sdbus::createSessionBusConnection();

      // 创建用于监听信号的代理
      sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
      sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
      m_dbusSignalProxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                             org_freedesktop_DBus_path);

      // 注册NameOwnerChanged信号监听
      sdbus::InterfaceName org_freedesktop_DBus_interface{"org.freedesktop.DBus"};
      sdbus::SignalName org_freedesktop_DBus_signal{"NameOwnerChanged"};

      m_dbusSignalProxy
          ->registerSignalHandler(org_freedesktop_DBus_interface,
                                  org_freedesktop_DBus_signal, &onConcatenated);
      // 完成信号注册
      // m_dbusSignalProxy->finishRegistration();
    } catch (const sdbus::Error &e) {
      std::cerr << "Error setting up D-Bus signal listener: " << e.what()
                << std::endl;
    }
  }
  const std::vector<std::string> &getPlayers() const { return m_players; }

  // 虚函数，可被子类重写
  virtual void onPlayerAdded(const std::string &playerName) {
    std::cout << "新播放器: " << playerName << std::endl;
  }

  virtual void onPlayerRemoved(const std::string &playerName) {
    std::cout << "播放器已移除: " << playerName << std::endl;
  }

  PlayerState getPlayerState() const {

    PlayerState state = {PlaybackStatus::Stopped, {}, 0, currentPlayer_};
    if (currentPlayer_.empty()) {
      return state;
    }
    auto dbusConn_ = sdbus::createSessionBusConnection();
    // 等待300毫秒以确保播放器已经准备好
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    sdbus::ServiceName destination{std::move(currentPlayer_)};
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    auto playerProxy = sdbus::createProxy(*dbusConn_, std::move(destination),
                                          std::move(objectPath));

    // 1. 获取播放状态
    try {
      sdbus::Variant playbackStatus;
      playerProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
          .storeResultsTo(playbackStatus);

      const std::string status = playbackStatus.get<std::string>();

      if (status != "Playing" && status != "Paused") {
        state.status = PlaybackStatus::Stopped;
        WARN("Not in playing state: %s", status.c_str());
        return state;
      } else {
        state.status = status == "Playing" ? PlaybackStatus::Playing
                                           : PlaybackStatus::Paused;
      }
    } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
      WARN("D-Bus error: %s", e.getMessage().c_str());
      state.status = PlaybackStatus::Unknown;
      return state;
    }
    // 2. 获取媒体元数据
    try {
      sdbus::Variant metadata;
      playerProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.mpris.MediaPlayer2.Player", "Metadata")
          .storeResultsTo(metadata);

      auto md = metadata.get<std::map<std::string, sdbus::Variant>>();

      // 解析标题
      if (md.count("xesam:title")) {
        state.metadata.title = md["xesam:title"].get<std::string>();
      } else {
        WARN("xesam:title not found in metadata");
      }

      // 歌词解析
      if (md.count("xesam:asText")) {
        state.metadata.lyrics = md["xesam:asText"].get<std::string>();
      } else {
        WARN("xesam:asText not found in metadata");
      }

      if (md.count("xesam:artist")) {
        state.metadata.artist =
            md["xesam:artist"].get<std::vector<std::string>>()[0];
      } else {
        // 如果没有找到 xesam:artist，尝试使用 xesam:albumArtist （array)
        if (md.count("xesam:albumArtist")) {
          state.metadata.artist =
              md["xesam:albumArtist"].get<std::vector<std::string>>()[0];
        } else {
          WARN("xesam:albumArtist not found in metadata");
        }
      }
      // 解析媒体长度
      if (md.count("mpris:length")) {
        // 数据类型是 int64_t
        state.metadata.length = md["mpris:length"].get<int64_t>() / 1000;
      } else {
        WARN("mpris:length not found in metadata");
      }
    } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
      WARN("D-Bus error: %s", e.getMessage().c_str());
      return state;
    } catch (const std::exception &e) { // 捕获其他标准异常
      WARN("General error: %s", e.what());
      return state;
    }

    // 3. 获取播放位置
    try {
      sdbus::Variant posVar;
      playerProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.mpris.MediaPlayer2.Player", "Position")
          .storeResultsTo(posVar);
      state.position = posVar.get<int64_t>() / 1000;
    } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
      WARN("D-Bus error: %s", e.getMessage().c_str());
      return state;
    } catch (const std::exception &e) { // 捕获其他标准异常
      WARN("General error: %s", e.what());
      return state;
    }
    return state;
  }

  std::string currentPlayer_; // 当前活跃的播放器名称
private:
  std::vector<std::string> m_players;
  std::unique_ptr<sdbus::IProxy> m_dbusSignalProxy; // 新增：持有信号监听代理
  std::shared_ptr<sdbus::IProxy> dbusProxy_; // D-Bus代理对象（用于NameOwnerChanged）

  void refreshPlayers() { m_players = getMprisPlayerNames(); }

  // 前面定义的 getMprisPlayerNames 函数
  std::vector<std::string> getMprisPlayerNames() {
    std::vector<std::string> playerNames;
    try {
      // 连接到会话总线
      auto connection = sdbus::createSessionBusConnection();

      // 创建 D-Bus 代理以访问 org.freedesktop.DBus 服务

      sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
      sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
      auto dbusProxy = sdbus::createProxy(*connection, 
        org_freedesktop_DBus,
        org_freedesktop_DBus_path);

        
      // 调用 ListNames 方法
      std::vector<std::string> allNames;
      dbusProxy->callMethod("ListNames")
          .onInterface("org.freedesktop.DBus")
          .storeResultsTo(allNames);

      // 解析结果
      for (const auto &name : allNames) {
        if (name.find("org.mpris.MediaPlayer2.") == 0) {
          playerNames.push_back(name);
        }
      }
    } catch (const sdbus::Error &e) {
      std::cerr << "Error getting player names: " << e.what() << std::endl;
    }
    return playerNames;
  }
  
};

// 示例用法
int main() {
  // 创建播放器管理器
  MprisPlayerManager playerManager;
  // 保持程序运行以监听变化
  std::cout << "按 Ctrl+C 退出..." << std::endl;
  while (true) {
    // 打印当前所有播放器
    std::cout << "当前活动的播放器:" << std::endl;
    for (const auto &player : playerManager.getPlayers()) {
      std::cout << "- " << player << std::endl;
      playerManager.currentPlayer_ = player;
      auto state = playerManager.getPlayerState();
      std::cout << "  状态: "
                << (state.status == PlaybackStatus::Playing ? "播放中"
                                                           : "暂停")
                << std::endl;
      std::cout << "  标题: " << state.metadata.title << std::endl;
      std::cout << "  艺术家: " << state.metadata.artist << std::endl;
      std::cout << "  歌词: " << state.metadata.lyrics << std::endl;
      std::cout << "  长度: " << state.metadata.length << " 秒" << std::endl;
      std::cout << "  位置: " << state.position << " 秒" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
