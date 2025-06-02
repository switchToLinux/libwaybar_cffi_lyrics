#include "../include/player_manager.h"
#include "common.h"
#include <cstddef>
#include <mutex>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <utility>


PlayerManager::PlayerManager(
    std::shared_ptr<sdbus::IConnection> dbusConn,
    std::function<void(const PlayerState &)> stateCallback)
    : dbusConn_(std::move(dbusConn)), stateCallback_(std::move(stateCallback)) {
  if (!dbusConn_) {
    ERROR("Failed to initialize D-Bus connection");
    return;
  }
  sdbus::ServiceName destination{"org.freedesktop.DBus"};
  sdbus::ObjectPath objectPath{"/org/freedesktop/DBus"};
  dbusProxy_ = sdbus::createProxy(*dbusConn_, destination, objectPath);
  if (!dbusProxy_) {
    ERROR("Failed to create D-Bus proxy");
    return;
  }
  INFO("D-Bus connection initialized");
  startMonitoring(); // 初始化时自动启动监听
}

PlayerManager::~PlayerManager() {
  stopMonitoring(); // 析构时停止监听
}

// 切换到下一个播放器，如果没有播放器可用，则返回空字符串
std::string PlayerManager::switchNewPlayer() const {
  auto allPlayer = getAllPlayers();
  if (allPlayer.empty()) {
    return "";
  }
  // 找到当前播放器在列表中的索引
  auto currentIndex = std::find(allPlayer.begin(), allPlayer.end(), currentPlayer_);
  if (currentIndex == allPlayer.end()) {
    DEBUG("Current player not found in player list, using first player: %s", allPlayer[0].c_str());
    return allPlayer[0];
  }
  // 计算下一个播放器的索引
  size_t nextIndex = (currentIndex - allPlayer.begin() + 1) % allPlayer.size();
  DEBUG("Switching to next player: %s", allPlayer[nextIndex].c_str());
  return allPlayer[nextIndex];
}

std::vector<std::string> PlayerManager::listPlayerNames() {
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

    std::vector<std::string> allNames;
    dbusProxy->callMethod("ListNames")
        .onInterface("org.freedesktop.DBus")
        .storeResultsTo(allNames);

    for (const auto &name : allNames) {
      if (name.find("org.mpris.MediaPlayer2.") == 0 && name.find("playerctld") == std::string::npos) {
        playerNames.push_back(name);
      }
    }
  } catch (const sdbus::Error &e) {
    WARN("Error getting player names: %s", e.what());
  }
  return playerNames;
}

PlayerState PlayerManager::getPlayerState() const {

  PlayerState state = {PlaybackStatus::Stopped, {}, 0, currentPlayer_};
  if(currentPlayer_.empty()) {
    return state;
  }
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
      state.status = status == "Playing" ? PlaybackStatus::Playing : PlaybackStatus::Paused;
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
// 启动D-Bus信号监听（NameOwnerChanged）
// 初始化当前活跃的播放器列表
// 启动事件循环
void PlayerManager::startMonitoring() {
  if (!dbusProxy_)
    return;

  // 初始化当前活跃的播放器列表
  for (const auto &name : listPlayerNames()) {
    INFO("Found player: %s", name.c_str());
    addNewPlayer(name); // 新播放器启动
  }
  updatePlayerState(); // 更新当前播放器状态
  DEBUG("Current player: [%s]", currentPlayer_.c_str());
  INFO("Starting D-Bus signal monitoring");
  // 注册NameOwnerChanged信号监听器
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
          if (name == currentPlayer_) {
            currentPlayer_ = switchNewPlayer();
            updatePlayerState();
          }
          INFO("Player exited: %s", name.c_str());
        } else if (oldOwner.empty()) {
          INFO("New player detected: %s", name.c_str());
          addNewPlayer(name);
        }
      });
  // 启动事件循环
  INFO("Starting D-Bus event loop");
  eventLoopThread_ = std::thread([this]() { dbusConn_->enterEventLoop(); });
}

void PlayerManager::stopMonitoring() {
  // 遍历所有播放器代理，移除信号监听器
  for (auto &[serviceName, playerProxy] : players_) {
    try{
      playerProxy->unregister();
      INFO("Unregistered player proxy for %s", serviceName.c_str());
    } catch (const std::exception &e) {
      WARN("Failed to unregister player proxy for %s: %s", serviceName.c_str(), e.what());
    } catch (...) {
      WARN("Failed to unregister player proxy for %s: Unknown error", serviceName.c_str());
    }
  }
  players_.clear();
  dbusConn_->leaveEventLoop();
}
// Metadata 解析函数（实现）
void PlayerManager::parseMetadata(
    const std::map<std::string, sdbus::Variant> &metadata,
    PlayerMetadata &out) const {
  // 解析标题
  if (metadata.count("xesam:title")) {
    out.title = metadata.at("xesam:title").get<std::string>();
  } else {
    out.title = "[Unknown Title]"; // 默认值避免空字符串
    DEBUG("Metadata missing xesam:title");
  }

  // 解析艺术家（优先 xesam:artist，降级使用 albumArtist）
  if (metadata.count("xesam:artist")) {
    const auto &artists =
        metadata.at("xesam:artist").get<std::vector<std::string>>();
    out.artist = artists.empty() ? "[Unknown Artist]" : artists[0];
  } else if (metadata.count("xesam:albumArtist")) {
    const auto &albumArtists =
        metadata.at("xesam:albumArtist").get<std::vector<std::string>>();
    out.artist = albumArtists.empty() ? "[Unknown Artist]" : albumArtists[0];
  } else {
    out.artist = "[Unknown Artist]"; // 默认值
    DEBUG("Metadata missing xesam:artist/albumArtist");
  }

  // 解析歌词（musicfox 专有字段）
  if (metadata.count("xesam:asText")) {
    out.lyrics = metadata.at("xesam:asText").get<std::string>();
  } else {
    out.lyrics = ""; // 无歌词时置空
  }
}
void PlayerManager::addNewPlayer(const std::string &serviceName) {
  // 优先使用musicfox播放器
  if (serviceName.contains("musicfox")) {
    currentPlayer_ = serviceName;
    INFO("Using musicfox player: %s", serviceName.c_str());
  }
  try {
    // 创建播放器实例代理
    sdbus::ServiceName destination{std::move(serviceName)};
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    auto playerProxy = sdbus::createProxy(*dbusConn_, std::move(destination),
                                          std::move(objectPath));
    // 注册PropertiesChanged信号监听器
    playerProxy->uponSignal("PropertiesChanged")
        .onInterface("org.freedesktop.DBus.Properties")
        .call([this,
               serviceName](const std::string &interfaceName,
                            std::map<std::string, sdbus::Variant> &changedProps,
                            std::vector<std::string> &invalidatedProps) {
          DEBUG("PropertiesChanged: %s , interfaceName: [%s] , currentPlayer: %s",
                serviceName.c_str(), interfaceName.c_str(), currentPlayer_.c_str());
          if (interfaceName != "org.mpris.MediaPlayer2.Player") {
            WARN("Ignoring non-player interface: %s", interfaceName.c_str());
            return;
          }
          bool needCallback = false;
          if(changedProps.count("Metadata")) {
            needCallback = true;
            auto metadata = changedProps["Metadata"].get<std::map<std::string, sdbus::Variant>>();
            PlayerMetadata md;
            parseMetadata(metadata, md);
            DEBUG("Metadata changed: title=[%s], artist=[%s], lyrics=[%s]",
                  md.title.c_str(), md.artist.c_str(), md.lyrics.c_str());
          }
          if(changedProps.count("PlaybackStatus")) {
            auto status = changedProps["PlaybackStatus"].get<std::string>();
            needCallback = true;
            INFO("PlaybackStatus changed: %s", status.c_str());
          }
          // 触发上层回调（线程安全：D-Bus事件循环可能在独立线程，需确保回调线程安全）
          if (stateCallback_ && needCallback) {
            updatePlayerState();
          }
        });

    // 完成信号注册并存储代理
    players_[serviceName] = std::move(playerProxy);
    INFO("New player added: %s", serviceName.c_str());
  } catch (const sdbus::Error &e) {
    WARN("Player proxy init error: %s", e.what());
  }
}

std::string PlayerManager::getCurrentPlayerName() const {
  // 优先返回musicfox播放器
  return currentPlayer_;
}

std::vector<std::string> PlayerManager::getAllPlayers() const {
  std::vector<std::string> playerNames;
  for (const auto &[name, _] : players_) {
    playerNames.push_back(name);
  }
  return playerNames;
}

// 实现切换当前播放器的方法（切换显示信息)
void PlayerManager::setCurrentPlayer(const std::string &playerName) {
  currentPlayer_ = playerName;
  updatePlayerState();
}

// 更新播放器状态信息（调用回调）
void PlayerManager::updatePlayerState() {
  DEBUG("updatePlayerState: %s", currentPlayer_.c_str());
  auto state = getPlayerState();
  if (stateCallback_) {
    stateCallback_(state);
  }
}

// 播放/暂停切换
void PlayerManager::togglePlayPause() {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for play/pause toggle");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  try {
    it->second->callMethod("PlayPause")
        .onInterface("org.mpris.MediaPlayer2.Player");
    INFO("Toggled play/pause for player: %s", currentPlayer_.c_str());
  } catch (const sdbus::Error &e) {
    WARN("PlayPause failed: %s", e.what());
  }
}

// 下一首歌曲
void PlayerManager::nextSong() {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for next song");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  try {
    it->second->callMethod("Next").onInterface("org.mpris.MediaPlayer2.Player");
    INFO("Next song triggered for player: %s", currentPlayer_.c_str());
  } catch (const sdbus::Error &e) {
    WARN("Next song failed: %s", e.what());
  }
}

// 上一首歌曲
void PlayerManager::prevSong() {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for previous song");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  try {
    it->second->callMethod("Previous")
        .onInterface("org.mpris.MediaPlayer2.Player");
    INFO("Previous song triggered for player: %s", currentPlayer_.c_str());
  } catch (const sdbus::Error &e) {
    WARN("Previous song failed: %s", e.what());
  }
}

// 停止播放
void PlayerManager::stopPlayer() {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for stop");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  try {
    it->second->callMethod("Stop").onInterface("org.mpris.MediaPlayer2.Player");
    INFO("Stopped player: %s", currentPlayer_.c_str());
  } catch (const sdbus::Error &e) {
    WARN("Stop failed: %s", e.what());
  }
}

// 设置循环模式（需要在头文件中定义LoopStatus枚举）
void PlayerManager::setLoopStatus(LoopStatus status) {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for loop status");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  const char *statusStr;
  switch (status) {
  case LoopStatus::None:
    statusStr = "None";
    break;
  case LoopStatus::Track:
    statusStr = "Track";
    break;
  case LoopStatus::Playlist:
    statusStr = "Playlist";
    break;
  default: {
    WARN("Invalid loop status");
    return;
  }
  }
  try {
    it->second->callMethod("Set")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "LoopStatus",
                       sdbus::Variant(statusStr));
    INFO("Set loop status to %s for player: %s", statusStr,
         currentPlayer_.c_str());
  } catch (const sdbus::Error &e) {
    WARN("Set loop status failed: %s", e.what());
  }
}

// 设置随机播放
void PlayerManager::setShuffle(bool enable) {
  if (currentPlayer_.empty()) {
    WARN("No current player selected for shuffle");
    return;
  }
  auto it = players_.find(currentPlayer_);
  if (it == players_.end()) {
    WARN("Current player proxy not found: %s", currentPlayer_.c_str());
    return;
  }
  try {
    it->second->callMethod("Set")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Shuffle",
                       sdbus::Variant(enable));
    INFO("Set shuffle %s for player: %s", enable ? "on" : "off",
         currentPlayer_.c_str());
    isShuffle_ = enable;
  } catch (const sdbus::Error &e) {
    WARN("Set shuffle failed: %s", e.what());
  }
}
bool PlayerManager::isShuffle() const {
  return isShuffle_;
}