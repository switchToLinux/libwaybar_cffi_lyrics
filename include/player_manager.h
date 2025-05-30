#ifndef WAYLYRICS_PLAYER_MANAGER_H
#define WAYLYRICS_PLAYER_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <sdbus-c++/ConvenienceApiClasses.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

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

class PlayerManager {
public:
  // 构造函数：传入D-Bus连接和状态变更回调（用于通知WayLyrics）
  PlayerManager(std::shared_ptr<sdbus::IConnection> dbusConn,
                std::function<void(const PlayerState &)> stateCallback);
  ~PlayerManager();

  // 启动D-Bus信号监听（NameOwnerChanged/PropertiesChanged）
  void startMonitoring();
  // 停止监听并清理资源
  void stopMonitoring();
  // 获取当前活跃的播放器名称（优先musicfox）
  std::string getCurrentPlayerName() const;
  std::vector<std::string> getAllPlayers() const;
  std::string switchNewPlayer() const; //切换到下一个播放器
  void setCurrentPlayer(const std::string &playerName); //切换当前播放器

  // 新增控制方法声明
  void togglePlayPause();                // 播放/暂停切换
  void nextSong();                       // 下一首
  void prevSong();                       // 上一首
  void stopPlayer();                     // 停止播放
  void setLoopStatus(LoopStatus status); // 设置循环模式
  void setShuffle(bool enable);          // 设置随机播放
  bool isShuffle() const;                // 获取随机播放状态

private :
  // D-Bus信号处理函数
  void handleNameOwnerChanged(const std::string &name, const std::string &oldOwner,
                           const std::string &newOwner);
  void handlePropertiesChanged(const sdbus::Signal &signal);
  void addNewPlayer(const std::string &playerName);
  std::vector<std::string> listPlayerNames();
  PlayerState getPlayerState() const; // 根据 currentPlayer_ 获取状态信息
  void updatePlayerState(); // 更新 currentPlayer_ 的状态信息

  void parseMetadata(const std::map<std::string, sdbus::Variant> &metadata,
                     PlayerMetadata &out) const;

  // 成员变量
  std::shared_ptr<sdbus::IConnection> dbusConn_; // D-Bus连接对象
  std::shared_ptr<sdbus::IProxy> dbusProxy_; // D-Bus代理对象（用于NameOwnerChanged）
  std::mutex mutex_;                         // 保护players_的线程安全
  std::thread eventLoopThread_;
  std::map<std::string, std::unique_ptr<sdbus::IProxy>> players_; // 播放器代理
  std::string currentPlayer_; // 当前活跃的播放器名称
  bool isShuffle_ = false; // 随机播放标记
  std::function<void(const PlayerState &)> stateCallback_; // 状态变更回调（通知WayLyrics）
};

#endif // WAYLYRICS_PLAYER_MANAGER_H