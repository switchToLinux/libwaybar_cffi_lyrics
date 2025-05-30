# waylyrics
> a lyrics plugin for waybar
一个基于sdbus-cpp开发的waybar歌词插件。

优先支持musicfox播放器，其他播放器通过网络获取歌词。


waylyrics是一个CFFI动态库，基于sdbus-cpp开发实现。

当将waylyrics配置到waybar后，会在状态栏中显示歌词。默认情况下，会每隔2秒刷新一次歌词（对应配置中的 `interval` 参数）。

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
编译后会生成动态库 `libwaylyrics.so`，可以直接使用。



## waybar使用

模块配置示例:

```json
{
    "cffi/lyrics": {
        "module_path": "/apps/libs/waylyrics.so",
        "cache_dir": "~/.cache/waylyrics",
        "id": "waylyrics",
        "class": "lyrics-mpv",
        "interval": 3,
        "dest": "mpv"
    },
}
```

参数说明:
- module_path: 插件路径
- id: css样式id ,默认值为 cffi-lyrics-label
- class: css样式class，默认值为 cffi-lyrics-label
- interval: 歌词刷新时间间隔，单位秒，默认为 3
- dest: 播放器实例名称,暂时没有实现此功能, mpris表示所有支持mpris协议的播放器，应用于dbus的 **org.mpris.MediaPlayer2.{dest}**，比如 mpv, vlc, mpris 等.
- cache_dir: 歌词缓存目录, 用于缓存歌词, 避免每次都请求歌词, 默认为 ~/.cache/waylyrics
-


## 已知问题

- waybar偶尔会core,暂时分析什么原因，但多启动几次还是可以启动的，猜测是跟线程有关问题。
- 歌词不更新： 触发了某些为止的异常导致更新歌词线程退出，需要重启waybar,但遇到这种情况最好反馈给我，描述在什么情况下触发bug了，这样我才能快速修复问题，避免以后一直被这类问题困扰。


## 学习参考资料

- [使用sdbus-c++文档](https://kistler-group.github.io/sdbus-cpp/docs/using-sdbus-c++.html)
- [cffi-example代码示例](https://github.com/Alexays/Waybar/tree/master/resources/custom_modules/cffi_example/)


