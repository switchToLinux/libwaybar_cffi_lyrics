#pragma once  // 防止头文件重复包含

// 包含GTK3和标准整数类型头文件
#include <gtk-3.0/gtk/gtk.h>
#include <stdint.h>

// 确保C++编译时使用C链接规范（兼容C语言调用）
#ifdef __cplusplus
extern "C" {
#endif

/// Waybar ABI版本号（应用二进制接口），当前最新版本为1
/// 用于模块与Waybar主程序之间的版本兼容性检查
extern const size_t wbcffi_version;

/// Waybar CFFI模块的私有结构体（内部实现细节，外部不可直接操作）
typedef struct wbcffi_module wbcffi_module;

/// Waybar模块初始化信息结构体（提供模块运行所需的核心接口）
typedef struct {
  /// Waybar CFFI模块对象指针（指向内部私有结构体实例）
  wbcffi_module* obj;

  /// Waybar主程序的版本字符串，用于模块适配不同版本
  const char* waybar_version;

  /// 获取Waybar为当前模块分配的GTK容器部件（用于布局模块UI）
  /// @param obj Waybar CFFI模块对象指针
  /// @return GTK容器部件指针（GtkContainer类型）
  GtkContainer* (*get_root_widget)(wbcffi_module* obj);

  /// 请求在GTK主事件循环的下一次迭代中调用wbcffi_update()
  /// 用于触发模块的UI更新（避免直接在非主线程调用UI操作）
  /// @param obj Waybar CFFI模块对象指针
  void (*queue_update)(wbcffi_module*);
} wbcffi_init_info;

/// 模块配置项的键值对结构体（表示扁平化的JSON配置）
typedef struct {
  /// 配置项的键名（如"font-size"）
  const char* key;
  /// 配置项的值（字符串形式）
  /// 注：JSON对象/数组会被序列化为字符串（如{"color":"red"}会变成"{'color':'red'}"）
  const char* value;
} wbcffi_config_entry;

/// 模块初始化函数（模块加载时必须实现的核心函数）
/// 在模块实例化时由Waybar主程序调用
/// @param init_info 包含模块运行所需的核心接口（如获取UI部件、触发更新等）
/// @param config_entries 模块配置的扁平化键值对数组（仅在初始化期间有效）
/// @param config_entries_len 配置项数量
/// @return 模块实例数据指针（由模块自定义类型，NULL表示初始化失败）
void* wbcffi_init(const wbcffi_init_info* init_info, const wbcffi_config_entry* config_entries,
                  size_t config_entries_len);

/// 模块反初始化函数（模块卸载时必须实现的核心函数）
/// 在Waybar关闭或模块被移除时调用，用于释放模块资源
/// @param instance 模块实例数据指针（由wbcffi_init返回）
void wbcffi_deinit(void* instance);

/// 模块UI更新函数（可选实现）
/// 由GTK主事件循环调用（通过queue_update触发），用于更新模块UI内容
/// @param instance 模块实例数据指针（由wbcffi_init返回）
void wbcffi_update(void* instance);

/// 信号响应函数（可选实现）
/// 当Waybar接收到POSIX信号（如SIGUSR1）时调用，用于模块自定义信号处理
/// @param instance 模块实例数据指针（由wbcffi_init返回）
/// @param signal 接收到的信号ID（如SIGINT=2）
void wbcffi_refresh(void* instance, int signal);

/// 动作处理函数（可选实现）
/// 当用户触发模块配置中的动作（如点击事件）时调用
/// （参考：https://github.com/Alexays/Waybar/wiki/Configuration#module-actions-config）
/// @param instance 模块实例数据指针（由wbcffi_init返回）
/// @param action_name 触发的动作名称（如"toggle"）
void wbcffi_doaction(void* instance, const char* action_name);

// 结束extern "C"块
#ifdef __cplusplus
}
#endif
