# libwaybar_cffi_lyrics
> a lyrics plugin for waybar
一个基于sdbus-cpp开发的waybar歌词插件。

优先支持musicfox播放器，其他播放器通过网络获取歌词。


libwaybar_cffi_lyrics是一个CFFI动态库，基于sdbus-cpp开发实现。

当将libwaybar_cffi_lyrics配置到waybar后，会在状态栏中显示歌词。默认情况下，会每隔2秒刷新一次歌词（对应配置中的 `interval` 参数）。

效果示例：

![preview](preview/waybar_preview.gif)


## Build
编译需要一些依赖库，具体如下：

- Build dependencies:
  - g++ (C++ compiler)
  - meson & ninja (build system)
  - pkg-config

- Libraries:
  - gtk-3
  - sdbus-c++
  - curl
  - nlohmann-json

编译步骤：
```bash

make purge
make

# 编译安装到系统默认目录 ~/.config/cffi/
make install

# 编译安装到指定目录
make install DESTDIR=/path/to/libs/
```
编译后会生成动态库 `libwaybar_cffi_lyrics.so`，可以直接使用。



## waybar使用

模块配置示例:

```json
{
    "cffi/lyrics": {
        "module_path": "/apps/libs/libwaybar_cffi_lyrics.so",
        "cache_dir": "~/.cache/libwaybar_cffi_lyrics",
        "id": "waybar_cffi_lyrics",
        "class": "lyrics-mpv",
        "format": "{player} - {title} {elapsed}/{duration} {lyrics}",
        "actions": {
            "on-click": "toggle",
            "on-click-right": "loop",
            "on-click-middle": "shuffle",
            "on-scroll-up": "prev",
            "on-scroll-down": "next"
        },
        "max_length": 30,
        "lyrics-title-max-length": 30,
        "lyrics-max-duration": 300,
        "interval": 3,
        "dest": "mpv"
    },
}
```

参数说明:
- module_path: 插件路径
- id: css样式id ,默认值为 waybar_cffi_lyrics
- class: css样式class，默认不设置
- interval: 歌词刷新时间间隔，单位秒，默认为 3
- max_length: 歌词最大长度，默认为 30
- lyrics-title-max-length: 歌词标题最大长度，默认为 30
- lyrics-max-duration: 歌词最大显示时间，单位秒，默认为 300
- dest: 播放器实例名称,暂时没有实现此功能, mpris表示所有支持mpris协议的播放器，应用于dbus的 **org.mpris.MediaPlayer2.{dest}**，比如 mpv, vlc, mpris 等.
- cache_dir: 歌词缓存目录, 用于缓存歌词, 避免每次都请求歌词, 默认为 ~/.cache/libwaybar_cffi_lyrics
- actions: 动作配置, 目前支持的动作有:
  - toggle: 播放器播放/暂停
  - loop: 循环播放
  - shuffle: 随机播放
  - prev: 上一首
  - next: 下一首
- format: 歌词格式, 支持的变量有:
  - player: 播放器名称: musicfox/mpv/vlc/firefox/chromium
  - status: 播放状态, playing/paused/stopped
  - title: 歌曲标题
  - artist: 歌手
  - album: 专辑
  - elapsed: 已播放时间
  - duration: 歌曲总时长
  - lyrics: 歌词




## 问题提醒

- waybar偶尔会core，但多启动几次还是可以启动的。
- 多屏幕显示歌词，歌词更新只会在一个屏幕上显示（两个屏幕不会同时更新歌词）。

## 学习参考资料

- [使用sdbus-c++文档](https://kistler-group.github.io/sdbus-cpp/docs/using-sdbus-c++.html)
- [cffi-example代码示例](https://github.com/Alexays/Waybar/tree/master/resources/custom_modules/cffi_example/)

