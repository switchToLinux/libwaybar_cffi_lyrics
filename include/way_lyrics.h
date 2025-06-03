#ifndef WAYLYRICS_WAY_LYRICS_H
#define WAYLYRICS_WAY_LYRICS_H

#include "common.h"
#include "player_manager.h"
#include <atomic>
#include <filesystem>
#include <gtk/gtk.h>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <pthread.h>
#include <string>
#include <thread>

const std::string NOPLAYER = "...";

// 配置参数结构体
struct ConfigParams {
  std::string cssClass; // 默认CSS类名
  std::string labelId;  // 默认标签ID
  std::string destName; // 默认播放器名称
  std::string cacheDir; // 缓存目录（无默认值，需显式设置）
  std::string format;   // 歌词格式
  int updateInterval;   // 默认更新间隔（秒）
  int maxLength;        // 默认最大歌词长度（字符）
  int lyricsTitleMaxLength; // 限制音频的标题长度（字符），超过长度的标题不会查询歌词
  int lyricsMaxDuration; // 限制音频的最大时长（秒），超过时长的歌词不会查询歌词
};

inline void displayConfig(const ConfigParams &params) {
  INFO("config params:");
  INFO("  cssClass: %s", params.cssClass.c_str());
  INFO("  labelId: %s", params.labelId.c_str());
  INFO("  destName: %s", params.destName.c_str());
  INFO("  cacheDir: %s", params.cacheDir.c_str());
  INFO("  format: %s", params.format.c_str());
  INFO("  updateInterval: %d", params.updateInterval);
  INFO("  maxLength: %d", params.maxLength);
  INFO("  lyricsTitleMaxLength: %d", params.lyricsTitleMaxLength);
  INFO("  lyricsMaxDuration: %d", params.lyricsMaxDuration);
}


class WayLyrics {
public:
  // 构造函数：传入配置参数（缓存目录、更新间隔、CSS类名等）
  WayLyrics(const ConfigParams &params);
  ~WayLyrics();

  // 核心控制方法
  void start(GtkLabel *label); // 启动歌词显示（绑定GTK标签）
  void stop();                 // 停止显示并清理资源
  void toggle();               // 切换启动/停止状态
  bool isRunning() const;      // 检查是否正在运行

  // 播放器切换
  void nextPlayer();                    // 切换到下一个播放器
  void prevPlayer();                    // 切换到上一个播放器
  std::string getCurrentPlayer() const; // 获取当前播放器名称
  LoopStatus currentLoopStatus_ = LoopStatus::None; // 跟踪当前循环模式
  std::unique_ptr<PlayerManager> playerManager_;    // 播放器管理实例

private:
  void updateLyricsLoop(); // 歌词刷新循环（后台线程）
  std::string
  getLyrics(const PlayerState &state); // 获取歌词（优先缓存/网络请求）
  void onPlayerStateChanged(const PlayerState &state); // 播放器状态变更回调
  std::string getLyrics(const std::string &trackName, const std::string &artist);


  // 成员变量
  ConfigParams params_;                // 配置参数
  std::filesystem::path cachePath;     // 歌词缓存目录
  GtkLabel *displayLabel_{nullptr};    // 绑定的GTK标签（用于显示歌词）
  std::atomic<bool> isRunning_{false}; // 运行状态标记（原子操作保证线程安全）
  std::thread updateThread_{};         // 歌词刷新后台线程
  PlayerState currentState_;           // 当前播放器状态（线程安全需加锁）
  std::shared_ptr<sdbus::IConnection> dbusConn_;
};

#endif // WAYLYRICS_WAY_LYRICS_H