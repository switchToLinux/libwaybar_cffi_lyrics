#include "dbus/dbus-shared.h"
#include <dbus/dbus.h>
#include <stdio.h>

int main() {
  DBusError error;
  DBusConnection *connection;
  DBusMessage *message, *reply;
  DBusMessageIter iter;
  const char *name;

  dbus_error_init(&error);

  // 连接到系统总线
  connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (dbus_error_is_set(&error)) {
    fprintf(stderr, "Error connecting to the system bus: %s\n", error.message);
    dbus_error_free(&error);
    return 1;
  }

  // 创建一个方法调用消息
  message = dbus_message_new_method_call("org.freedesktop.DBus",
                                         "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus", "ListNames");
  if (!message) {
    fprintf(stderr, "Error creating the method call message.\n");
    dbus_connection_close(connection);
    return 1;
  }

  // 发送消息并获取回复
  reply = dbus_connection_send_with_reply_and_block(connection, message, -1,
                                                    &error);
  dbus_message_unref(message);
  if (dbus_error_is_set(&error)) {
    fprintf(stderr, "Error sending the message: %s\n", error.message);
    dbus_error_free(&error);
    dbus_connection_close(connection);
    return 1;
  }

  // 解析回复消息
  if (!dbus_message_iter_init(reply, &iter)) {
    fprintf(stderr, "Error initializing the message iter.\n");
    dbus_message_unref(reply);
    dbus_connection_close(connection);
    return 1;
  }
  while (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INVALID) {
    dbus_message_iter_get_basic(&iter, &name);
    printf("%s\n", name);
    dbus_message_iter_next(&iter);
  }

  // 释放资源
  dbus_message_unref(reply);
  dbus_connection_close(connection);

  return 0;
}
