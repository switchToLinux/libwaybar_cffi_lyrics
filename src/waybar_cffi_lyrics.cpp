#include "../include/way_lyrics.h"
#include "../include/waybar_cffi_module.h"
#include "common.h"
#include <cstring>
#include <gtk/gtk.h>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>

const size_t wbcffi_version = 1;

int log_level = LOG_LEVEL_NONE;

// 默认配置参数
constexpr const char *defaultCssClass = "waylyrics-label";
constexpr const char *defaultLabelId = "waylyrics-label";
constexpr const char *defaultDestName = "org.mpris.MediaPlayer2.musicfox";
constexpr int defaultUpdateInterval = 1; // 秒
constexpr int defaultMaxLength = 30;    // 字符
constexpr int defaultLyricsMaxDuration = 300; // 秒
constexpr int defaultLyricsTitleMaxLength = 30; // 字符
constexpr const char *loadingText = "加载歌词...";
constexpr const char *defaultFormat = "{player}/{title} {lyrics}";

// 插件实例结构体（管理生命周期）
struct Mod {
  void *waybar_module;                  // waybar模块对象
  GtkBox *container;                    // GTK容器
  std::unique_ptr<WayLyrics> wayLyrics; // 歌词显示管理器
};

// 全局实例计数（用于调试）
static int instance_count = 0;

// 配置解析辅助函数（从waybar配置中提取参数）
static ConfigParams parseConfig(const wbcffi_config_entry *config_entries,
            size_t config_entries_len) {

  ConfigParams params = {
      .cssClass = defaultCssClass,
      .labelId = defaultLabelId,
      .destName = defaultDestName,
      .cacheDir = std::string(getenv("HOME")) + "/.cache/waylyrics",
      .format = defaultFormat,
      .tooltipFormat = "",
      .toggleTooltip = 0, // 默认禁用工具提示
      .updateInterval = defaultUpdateInterval,
     .maxLength = defaultMaxLength,
     .lyricsTitleMaxLength = defaultLyricsTitleMaxLength,
    .lyricsMaxDuration = defaultLyricsMaxDuration,
  };

  for (size_t i = 0; i < config_entries_len; ++i) {
    const auto &entry = config_entries[i];
    DEBUG("配置项 '%s' 值: %s", entry.key, entry.value);
    if (strncmp(entry.key, "class", 5) == 0) {
      params.cssClass = entry.value;
    } else if (strncmp(entry.key, "id", 2) == 0) {
      params.labelId = entry.value;
    } else if (strncmp(entry.key, "dest", 4) == 0) {
      params.destName = entry.value;
    } else if (strncmp(entry.key, "interval", 8) == 0) {
      params.updateInterval = std::max(1, atoi(entry.value)); // 最小间隔1秒
    } else if (strncmp(entry.key, "cache_dir", 10) == 0) {
      params.cacheDir = entry.value;
    } else if(strncmp(entry.key, "format", 6) == 0) {
      params.format = entry.value;
    } else if (strncmp(entry.key, "max-length", 10) == 0) {
      params.maxLength = std::max(10, atoi(entry.value));
    } else if(strncmp(entry.key, "lyrics-max-duration", 19) == 0) {
      params.lyricsMaxDuration = std::max(10, atoi(entry.value));
    } else if(strncmp(entry.key, "lyrics-title-max-length", 23) == 0) {
      params.lyricsTitleMaxLength = std::max(10, atoi(entry.value));
    } else if (strncmp(entry.key, "tooltip-format", 14) == 0) {
      params.tooltipFormat = entry.value;
    } else if (strncmp(entry.key, "tooltip", 7) == 0) {
      // value取值： true: 启用工具提示, false: 禁用工具提示
      if (strncmp(entry.value, "true", 4) == 0) {
        params.toggleTooltip = 1;
      }
    } else if (strncmp(entry.key, "log_level", 9) == 0) {
      // 启用调试模式 0-3
      log_level = atoi(entry.value);
      if(log_level > 3) {
        log_level = 3; // 限制最大日志级别为3
      } else if (log_level < 0) {
        log_level = 0; // 限制最小日志级别为0
      }
      INFO("启用调试模式: log_level: %d", log_level);
    } else if (strncmp(entry.key, "actions", 7) == 0 ||
               strncmp(entry.key, "module_path", 11) == 0) {
      // 忽略 actions 和 module_path 配置项
      INFO("(waybar使用的参数)忽略配置项 '%s'", entry.key);
    } else {
      INFO("未知配置项 '%s'", entry.key);
    }
  }
  if (params.cssClass.empty()) {
    params.cssClass = defaultCssClass;
  }
  if (params.labelId.empty()) {
    params.labelId = defaultLabelId;
  }
  if (params.destName.empty()) {
    params.destName = defaultDestName;
  }
  if (params.updateInterval <= 0) {
    params.updateInterval = defaultUpdateInterval;
  }
  if (params.cacheDir.empty()) {
    params.cacheDir = std::string(getenv("HOME")) + "/.cache/waylyrics";
  }
  displayConfig(params);
  return params;
}

// waybar插件初始化入口（waybar要求的固定接口）
void *wbcffi_init(const wbcffi_init_info *init_info,
                  const wbcffi_config_entry *config_entries,
                  size_t config_entries_len) {
  try {
    INFO("waylyrics: 初始化插件，配置项数量: %ld", config_entries_len);

    // 解析配置参数
    auto configParams = parseConfig(config_entries, config_entries_len);

    // 创建插件实例结构体
    Mod *inst = new (std::nothrow) Mod;
    if (!inst) {
      ERROR("waylyrics: 内存分配失败");
      return nullptr;
    }
    // 初始化
    inst->container = nullptr;
    inst->waybar_module = init_info->obj;
    try{
      inst->wayLyrics = std::make_unique<WayLyrics>(configParams);
    } catch (const std::exception &e) {
      ERROR("waylyrics: 初始化失败，std::exception: %s", e.what());
    } catch (...) {
      ERROR("waylyrics: 初始化失败，未知异常");
    }
    if (!inst->wayLyrics) {
      ERROR("waylyrics: 初始化失败，无法创建WayLyrics实例");
    }
    if(!inst->wayLyrics) {
      DEBUG("waylyrics: 初始化失败，无法创建WayLyrics实例");
      delete inst;
      return nullptr;
    }
    // 创建GTK容器和标签
    GtkContainer *root = init_info->get_root_widget(init_info->obj);
    inst->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5));
    gtk_container_add(GTK_CONTAINER(root), GTK_WIDGET(inst->container));

    GtkLabel *label = GTK_LABEL(gtk_label_new(loadingText));
    GtkStyleContext *label_context =
        gtk_widget_get_style_context(GTK_WIDGET(label));
    gtk_style_context_add_class(label_context,
                                configParams.cssClass.c_str());    // 应用CSS类
    gtk_style_context_add_class(label_context, "flat module"); // 应用CSS类
    gtk_widget_set_name(GTK_WIDGET(label),
                        configParams.labelId.c_str()); // 设置标签ID
    gtk_container_add(GTK_CONTAINER(inst->container), GTK_WIDGET(label));
    // 设置标签最大长度(超过部分用...显示)
    gtk_label_set_max_width_chars(label, configParams.maxLength);
    gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_END);

    // 如果启用了工具提示，设置标签工具提示
    if (configParams.toggleTooltip) {
      gtk_widget_set_tooltip_text(GTK_WIDGET(label), configParams.tooltipFormat.c_str());
    }

    inst->wayLyrics->start(label); // 启动歌词显示

    INFO("waylyrics: 实例 %p 初始化完成（总实例数: %d）", inst, ++instance_count);
    return inst;
  } catch (const std::exception &e) {
    ERROR("waylyrics: wbcffi_init std::exception: %s", e.what());
    return nullptr;
  } catch (...) {
    ERROR("waylyrics: wbcffi_init unknown exception");
    return nullptr;
  }
}

// waybar插件动作处理接口（可选，根据需要实现）
void wbcffi_doaction(void *instance, const char *action_name) {
  DEBUG("waylyrics: 处理动作: %s", action_name);
  if (!instance || !action_name)
    return;
  Mod *inst = static_cast<Mod *>(instance);
  if (!inst->wayLyrics || !inst->wayLyrics->playerManager_)
    return;
  const std::string action = action_name;
  DEBUG("currentPlayer: %s", inst->wayLyrics->playerManager_->getCurrentPlayerName().c_str());
  if (action == "toggle") {
    inst->wayLyrics->playerManager_->togglePlayPause();
  } else if (action == "loop") {
    // 循环模式切换：None→Track→Playlist→None
    int nextStatus =
        (static_cast<int>(inst->wayLyrics->currentLoopStatus_) + 1) % 3;
    inst->wayLyrics->currentLoopStatus_ = static_cast<LoopStatus>(nextStatus);
    inst->wayLyrics->playerManager_->setLoopStatus(
        inst->wayLyrics->currentLoopStatus_);
  } else if (action == "next") {
    inst->wayLyrics->playerManager_->nextSong();
  } else if (action == "prev") {
    inst->wayLyrics->playerManager_->prevSong();
  /*} else if(action == "stop"){
    inst->wayLyrics->playerManager_->stopPlayer();
  */
  } else if (action == "shuffle") {
    inst->wayLyrics->playerManager_->setShuffle(!inst->wayLyrics->playerManager_->isShuffle());
  // }else if(action == "toggleLabel") {
  //   inst->wayLyrics->toggle(); // 切换显示/隐藏状态
  } else {
    DEBUG("waylyrics: 未处理的动作: %s", action_name);
  }
}
// waybar插件销毁接口（可选，根据需要实现）
void wbcffi_finish(void *data) {
  if (!data)
    return;
  Mod *inst = (Mod *)data;
  if (inst->wayLyrics) {
    inst->wayLyrics->stop(); // 停止歌词刷新
  }
  gtk_widget_destroy(GTK_WIDGET(inst->container)); // 销毁GTK部件
  delete inst;
  INFO("waylyrics: 实例销毁完成（剩余实例数: %d）", --instance_count);
}

