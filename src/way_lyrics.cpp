#include "../include/way_lyrics.h"
#include "../include/utils.hpp"
#include "common.h"
#include "player_manager.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

void displayState(const PlayerState &state) {
    DEBUG("Current Player State:");
    DEBUG("  Player Name: %s", state.playerName.c_str());
    DEBUG("  Status: %s", state.status == PlaybackStatus::Playing ? "Playing": "Paused");
    DEBUG("  Position: %10ld ms", state.position);
    DEBUG("  Duration: %10ld ms", state.metadata.length);
    DEBUG("  Metadata:");
    DEBUG("    Title: %s", state.metadata.title.c_str());
    //   DEBUG("    Artist: %s", state.metadata.artist.c_str());
    //   DEBUG("    Lyrics: %s", state.metadata.lyrics.c_str());
    //   DEBUG("    Album: %s", state.metadata.album.c_str());
}

WayLyrics::WayLyrics(const std::string &cacheDir, unsigned int updateInterval,
                     const std::string &cssClass)
    : updateInterval_(updateInterval), cssClass_(cssClass),
      isRunning_(false) {
  // 初始化缓存目录
  cachePath = std::filesystem::path(cacheDir);
  // 初始化D-Bus连接和PlayerManager
  auto dbusUniqueConn = sdbus::createSessionBusConnection();
  dbusConn_ = std::shared_ptr<sdbus::IConnection>(dbusUniqueConn.release());
  playerManager_ = std::make_unique<PlayerManager>(dbusConn_, [this](const PlayerState &state) {
        DEBUG("  >> PlayerState updated: %s", state.playerName.c_str());
        currentState_ = state;
        // 如果歌词为空且状态为播放中，则尝试获取歌词
        if (currentState_.metadata.lyrics.empty() &&
            currentState_.status == PlaybackStatus::Playing) {
            DEBUG("  >> Fetching lyrics for: %s by %s",
                  currentState_.metadata.title.c_str(),
                  currentState_.metadata.artist.c_str());
            try {
              currentState_.metadata.lyrics = getLyrics(
                  currentState_.metadata.title, currentState_.metadata.artist);
              if (currentState_.metadata.lyrics.empty()) {
                currentState_.metadata.lyrics =
                    getLyrics(currentState_.metadata.title, "");
              }
            } catch (const std::exception &e) {
              WARN("  >> Failed to get lyrics: %s", e.what());
            }
        }
        currentState_.position += 200; // 微调预览歌词的时间
      });
  
  INFO("  >> WayLyrics initialized"
       " with cache path: %s, update interval: %u seconds, CSS class: %s",
       cachePath.c_str(), updateInterval_, cssClass_.c_str());
}
WayLyrics::~WayLyrics() {
  INFO("  >> WayLyrics destroyed");
  playerManager_.reset();
  stop();
}
std::string WayLyrics::getLyrics(const std::string &trackName, const std::string &artist="") {
  std::string trim_query = trackName + " " + artist;
  trim_query = trim(trim_query);
  if(trim_query.empty()) {
    return "";
  }
  std::string url =
      "https://lrclib.net/api/search?track_name=" + url_encode(trackName);

  // 如果提供了艺术家名称，添加到URL中
  if(!artist.empty())
      url += "&artist_name=" + url_encode(artist);

  std::filesystem::path lyricsCachePath =
      cachePath / std::string(replace_space(trim_query) + ".txt");
  std::string content;

  std::string syncedLyrics = "";
  if (std::filesystem::exists(lyricsCachePath)) {
    DEBUG("  >> Lyrics found in cache: %s", lyricsCachePath.c_str());
    std::ifstream file(lyricsCachePath, std::ios::binary);
    if (!file.is_open()) {
      ERROR("  >> Failed to open cache file: %s", lyricsCachePath.c_str());
      return "";
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
  } else {
    DEBUG("  >> Lyrics not found in cache[%s], fetching from: %s",
          lyricsCachePath.c_str() , url.c_str());
    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
      CURLcode res = curl_easy_perform(curl);

      if (res != CURLE_OK) {
        ERROR("  >> CURL error: %s", curl_easy_strerror(res));
        return "";
      }
      long http_code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code != 200) {
        ERROR("  >> HTTP error: %ld", http_code);
        return "";
      }
      if (content.empty()) {
        ERROR("  >> No content received");
        return "";
      }
      curl_easy_cleanup(curl);
    }
  }

  try {
    auto json = nlohmann::json::parse(content, nullptr, false);
    if (json.is_discarded())
      return "";

    auto currentLyrics = json.get<std::vector<nlohmann::json>>();
    if (currentLyrics.empty())
      return "";
    auto &first = currentLyrics[0];
    if (first.count("syncedLyrics")) {
      syncedLyrics = first["syncedLyrics"];

      if (!syncedLyrics.empty()) {
        std::thread([lyricsCachePath, syncedLyrics]() {
            std::ofstream file(lyricsCachePath,
                               std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
              ERROR("  >> Failed to open cache file for writing: %s",
                    lyricsCachePath.c_str());
              return;
            }
            file << syncedLyrics;
            if (file.fail()) {
              ERROR("  >> Failed to write lyrics to cache file: %s",
                    lyricsCachePath.c_str());
              return;
            }
            DEBUG("  >> Lyrics cached successfully to: %s", lyricsCachePath.c_str());
        }).detach();
      }
      return syncedLyrics;
    }else {
      WARN("  >> No syncedLyrics found in JSON");
      return "";
    } 
  }catch (const std::exception &e) {
    WARN("Error parsing JSON: %s", e.what());
    return "";
  }
  return "";
}
// 静态方法：提取指定时间戳的歌词行
static std::string getSyncedLine(uint64_t pos, const std::string &syncedLyrics) {
  auto strVec = split(syncedLyrics, "\n");
  auto len = strVec.size();
  DEBUG("  >> getSyncedLine: pos=%ld, len=%ld", pos, len);
  size_t index = 0;
  for (size_t i = 0; i < len; i++) {
    auto &cur = strVec[i];
    if (cur.empty()) {
      continue;
    }
    uint64_t ms = timestampToMs(cur);
    if (pos > ms) {
        index = i;
    } else {
      break;
    }
  }
  DEBUG("  >> getSyncedLine: index=%ld, len:%ld , line:[%s]", index, len, index >= len ? "out of size": strVec[index].c_str());
  if (index >= len) {
    return "";
  }
  auto str = strVec[index];
  const size_t time_len = 10; // 时间戳长度，例如 "[00:00.00]xxxx"
  if (str.size() <= time_len) {
    return "";
  }
  str = str.substr(time_len, str.size());
  return trim(str);
}
// 定义结构体包装三个参数
struct UpdateData {
  GtkLabel *label;
  std::string text;
  std::string status;
};
static void updateLabelText(GtkLabel *label, const std::string &text,
                            uint64_t position, std::string prefix = "",
                            const std::string &playerStatus = "playing") {
  static std::string lastText = ""; // 记录上一次的歌词行
  auto line = getSyncedLine(position, text);

  line = prefix + line;
  if (line.empty() || lastText == line) { // 减少不必要的更新
    DEBUG("  >> No lyrics or same line, skipping update: [%s]", line.c_str());
    return;
  }
  lastText = line;
  DEBUG("  >> Updating label: positon:%ld, text: %s", position, line.c_str());
  // 使用 gdk_threads_add_idle 提交到主线程执行
  gdk_threads_add_idle(
      [](gpointer data) -> gboolean {
    auto *updateData = static_cast<UpdateData *>(data); // 转换为结构体指针
    if (updateData == nullptr)
      return FALSE;

    // 检查标签是否存活
    if (GTK_IS_LABEL(updateData->label)) {
      // 设置标签文本
      gtk_label_set_text(updateData->label, updateData->text.c_str());
      // 添加播放状态对应的 CSS class（如 "playing" 或 "paused"）
      auto context =
          gtk_widget_get_style_context(GTK_WIDGET(updateData->label));
      // 清理现有 所有class
      for (const auto &class_name : {"playing", "paused", "stopped"}) {
        gtk_style_context_remove_class(context, class_name);
      }
      gtk_style_context_add_class(context, updateData->status.c_str());
    }

    delete updateData; // 释放动态分配的内存
    return FALSE;
    }, new UpdateData{label, line, playerStatus}  // 传递结构体实例
  );
}

void WayLyrics::start(GtkLabel *label) {
  if (isRunning_)
    return;
  displayLabel_ = label;
  isRunning_ = true;

  INFO("  >> Starting update thread");
  updateThread_ = std::thread([this]() {
    while (isRunning_) {
      DEBUG("  >> Update thread started");
      std::string prefix = "";
      std::string lyricsLine = "";
      std::string playerStatus = "playing";
      try {
        if (!currentState_.metadata.title.empty()) {
          prefix = "《" + currentState_.metadata.title + "》" +
                    currentState_.metadata.artist + " - ";
        } else {
          prefix = "[no title]" + currentState_.metadata.artist + " - ";
        }
        if (currentState_.status == PlaybackStatus::Playing) {
          playerStatus = "playing";
          if (currentState_.metadata.lyrics.empty()) {
            lyricsLine = "no lyrics...";
          } else {
            lyricsLine = currentState_.metadata.lyrics;
          }
        } else if(currentState_.status == PlaybackStatus::Paused) {
          playerStatus = "paused";
          prefix = "paused...";
        } else {
          playerStatus = "stopped";
          prefix = "stopped...";
        }
        updateLabelText(displayLabel_, currentState_.metadata.lyrics,
                      currentState_.position, prefix, playerStatus);
        // 短间隔睡眠并检查 isRunning_，减少退出延迟
        for (unsigned int i = 0; i < updateInterval_ && isRunning_; ++i) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          if (isRunning_ && currentState_.status == PlaybackStatus::Playing) {
            currentState_.position += 1000;
          }
        }
      } catch (const std::exception &e) {
        WARN("  >> Update thread error: %s", e.what());
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 异常后短暂休眠避免高频重试
      } catch (...) {
        WARN("  >> Unknown error in update thread");
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 异常后短暂休眠避免高频重试
      }
    }
    INFO("  >> Update thread finished");
  });
}

void WayLyrics::stop() {
  if(!isRunning_) return;
  isRunning_ = false;
  // 主动等待线程退出
  try {
    DEBUG("  >> Waiting for update thread to finish");
    if(updateThread_.joinable()) {
      updateThread_.join();
    }
    DEBUG("  >> Update thread stopped");
    // gdk_threads_add_idle([](gpointer data) { return FALSE; }, nullptr);
    displayLabel_ = nullptr;
    INFO("  >> WayLyrics stopped");
  } catch (const std::exception &e) {
    WARN("  >> Error joining update thread: %s", e.what());
  } catch (...) {
    WARN("  >> Unknown error joining update thread");
  }
}

void WayLyrics::toggle() { isRunning_ ? stop() : start(displayLabel_); }

bool WayLyrics::isRunning() const { return isRunning_; }

void WayLyrics::nextPlayer() {
  // 切换到下一个播放器（简化实现）
  auto players = playerManager_->getAllPlayers();
  auto current = playerManager_->getCurrentPlayerName();
  auto it = std::find(players.begin(), players.end(), current);
  if (it != players.end()) {
    playerManager_->setCurrentPlayer((it + 1) == players.end() ? players[0]
                                                               : *(it + 1));
  }
}

void WayLyrics::prevPlayer() {
  // 切换到上一个播放器（简化实现）
  auto players = playerManager_->getAllPlayers();
  auto current = playerManager_->getCurrentPlayerName();
  auto it = std::find(players.begin(), players.end(), current);
  if (it != players.end()) {
    playerManager_->setCurrentPlayer(it == players.begin() ? players.back()
                                                           : *(it - 1));
  }
}

std::string WayLyrics::getCurrentPlayer() const {
  return playerManager_->getCurrentPlayerName();
}