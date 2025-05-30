# dbus命令行帮助

### 查看所有信号
如果我们想查看所有的信号(当前会话)，可以使用以下命令：
```bash
dbus-monitor --session
```
通常情况下，我们会看到如下输出：
```bash
signal time=1747972127.273267 sender=:1.868 -> destination=(null destination) serial=1927 path=/org/mpris/MediaPlayer2; interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
...
```
解释输出信息:
- time: 信号发送的时间
- sender: 信号的发送者(可以通过下面的命令查看信号的来源)
- destination: 信号的目标(通常为null destination: 表示信号没有目标, 即信号是广播信号, 所有的订阅者都会收到该信号)
- serial: 信号的序列号, 用于标识信号的唯一性
- path: 信号的路径, 通常为播放器的路径, 比如/org/mpris/MediaPlayer2
- interface: 信号的接口, 通常为org.freedesktop.DBus.Properties（表示信号是一个属性变化信号）
- member: 信号的成员, 通常为PropertiesChanged, 表示信号的名称,还可以是其他信号名称, 比如NameOwnerChanged, 表示信号的名称(通常该信号的用途是用于监听播放器的名称变化, 比如播放器的启动或关闭)

通过上面的命令, 我们可以看到所有的信号, 但是我们通常只关心我们关心的信号, 比如我们关心播放器的状态变化信号, 我们可以使用以下命令:
```bash
dbus-monitor --session interface=org.freedesktop.DBus.Properties path=/org/mpris/MediaPlayer2/Player member=PropertiesChanged

# 输出结果为：
# signal time=1747972127.273267 sender=:1.868 -> destination=(null destination) serial=1927 path=/org/mpris/MediaPlayer2; interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
#    string "org.mpris.MediaPlayer2.Player"
#    array [
#       dict entry(
#          string "PlaybackStatus"
#          variant             string "Paused"
#       )
#    ]

```

解释命令信息:
- interface: 信号的接口, 通常为org.freedesktop.DBus.Properties（表示信号是一个属性变化信号）
- path: 信号的路径, 通常为播放器的路径, 比如/org/mpris/MediaPlayer2/Player
- member: 信号的成员, 通常为PropertiesChanged, 表示信号的名称,还可以是其他信号名称, 比如NameOwnerChanged, 表示信号的名称(通常该信号的用途是用于监听播放器的名称变化, 比如播放器的启动或关闭)



### 查看指定信号
如果我们想查看某个信号，可以使用以下命令：
```bash
dbus-monitor --session interface=org.freedesktop.DBus.Properties path=/org/mpris/MediaPlayer2/Player member=PropertiesChanged
```


### 查看信号来源

如果我们想查看某个信号的来源(比如通知信号标注的'sender=:1.1042' )，可以使用以下命令：

```bash

send="$1"
spid=$(dbus-send --session \
  --dest=org.freedesktop.DBus \
  --type=method_call \
  --print-reply \
  /org/freedesktop/DBus \
  org.freedesktop.DBus.GetConnectionUnixProcessID \
  string::"$send" | awk '/int32/{ print $2 }')
if [ -z "$spid" ]; then
  echo "Error: Failed to get process ID for connection $send"
  exit 1
fi
ps -ef |grep "$spid"


# 输出结果为：
# message type=method_return sender=:1.1042 -> destination=:1.1042 serial=2
#   int32 1042

ps -ef |grep 1042
# 输出结果为：
# root      1042  1  0 15:59 ?        00:00:00 /usr/bin/musicfox
```
返回结果为进程PID, 可以通过PID找到进程名称。


