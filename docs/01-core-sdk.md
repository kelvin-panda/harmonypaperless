# core_sdk 阶段 0 说明

## 模块位置
当前采用“entry 内 core_sdk 目录”的过渡方式，原因是 `libmeet.so`/`libentry.so` 是 entry 的预编译 NAPI 库，先保证加载和编译通过；后续如果需要拆成 HAR/HSP，必须在不破坏 NAPI 加载的前提下迁移。

```text
entry/src/main/ets/core_sdk/
  native/MeetNative.ets    # libmeet.so 类型镜像与调用入口
  config/ConfigManager.ets # rawfile 配置复制、prepareConfig
  events/EventPoller.ets   # getEvents() 轮询与分发
  PaperlessSdk.ets         # 门面 API
  model/Models.ets         # 会议/房间/事件模型
```

## 已封装接口
```text
init(serverIp, serverPort)
isReady()
isLoggedIn()
login(user, password)
queryMeetings()
queryRooms()
selectMeeting(meetingId)
enterMeeting()
signIn(signinType)
startEvents(callback)
stopEvents()
exit()
```

## 调用顺序
1. `ConfigManager.prepare()` 复制 `rawfile/meet/client.ini`、`client.dev` 到 `filesDir/meet`；
2. 调用 `prepareConfig(template, out, serverIp, serverPort)`；
3. `meetNative.init(configPath)`；
4. 轮询 `isReady/isLoggedIn/getEvents`；
5. `login`；
6. `queryMeetings`；
7. `setCurrentMeeting` → `enterMeeting` → `signIn`。

## 约束
- 页面只能依赖 `PaperlessSdk`，不得直接 import `libmeet.so`。
- 事件轮询只在 core_sdk 中，不放到页面。
- 新增接口前先更新 `MeetNativeApi` 和 NAPI 类型声明。
- 未确认的目录/文件/离线/绑定 PDF 接口不得写入 `PaperlessSdk` 的真实实现。
