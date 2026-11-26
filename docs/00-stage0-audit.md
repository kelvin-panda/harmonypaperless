# 阶段 0 完成报告：工程审计与基线迁移

## 目标
把已验证的 HarmonyDemo 原生库、NAPI 类型和配置模板迁移到 HarmonyPaperless，建立 core_sdk 骨架，不实现业务页面。

## 迁移内容
- 从 `F:\harmonyos\HarmonyDemo` 复制到 `entry/libs/arm64-v8a/`：
  - `libmeet.so`
  - `libmeetcore.so`
  - `libcodec.so`
  - `libentry.so`
  - `libc++_shared.so`
- 复制 NAPI 类型：
  - `entry/src/main/cpp/types/libmeet/index.d.ts`
  - `entry/src/main/cpp/types/libentry/index.d.ts`
- 复制配置模板：
  - `entry/src/main/resources/rawfile/meet/client.ini`
  - `entry/src/main/resources/rawfile/meet/client.dev`
- `entry/oh-package.json5` 增加 `libmeet.so` 本地依赖。
- `entry/build-profile.json5` 移除 `externalNativeOptions`，改为使用预编译 `.so`，避免和 Demo 的预编译库冲突。
- `module.json5` 增加 INTERNET、KEEP_BACKGROUND_RUNNING、CAMERA、MICROPHONE 权限及 audioRecording 后台模式。

## core_sdk 骨架
已创建：
```text
entry/src/main/ets/core_sdk/
  native/MeetNative.ets
  config/ConfigManager.ets
  events/EventPoller.ets
  PaperlessSdk.ets
  model/Models.ets
```

阶段 0 只封装 Demo 已确认接口：
- `init / isReady / isLoggedIn / login`
- `queryMeetings / queryRooms / getEvents`
- `setCurrentMeeting / enterMeeting / signIn`
- 媒体和屏幕共享接口先保留，不在本阶段使用

## 验证
执行命令：
```text
hvigorw --mode module -p product=default -p buildMode=debug assembleHap --no-daemon
```

结果：
- `BUILD SUCCESSFUL`
- 产物：`entry/build/default/outputs/default/entry-default-unsigned.hap`
- HAP 内已确认包含：
  - `libs/arm64-v8a/libmeet.so`
  - `libs/arm64-v8a/libmeetcore.so`
  - `libs/arm64-v8a/libcodec.so`
  - `libs/arm64-v8a/libentry.so`
  - `libs/arm64-v8a/libc++_shared.so`
  - `resources/rawfile/meet/client.ini`
  - `resources/rawfile/meet/client.dev`

## 未验证/风险
1. 当前没有配置签名，只能构建 unsigned HAP，尚未在真机安装运行。
2. 目标工程 `compatibleSdkVersion = 6.0.1(21)`，Demo 真机验证环境为 API 26，需在目标设备验证 `.so` 兼容性。
3. 当前 `Index.ets` 只是 SDK 冒烟页面，不是正式 UI。
4. Demo NAPI 的文件/离线/绑定 PDF 接口缺口见 `02-sdk-gap.md`。

## 阶段结论
阶段 0 编译验证通过，原生库和配置已进入目标工程；阶段 1 可以继续做 core_sdk 的真实初始化、登录、会议查询和事件轮询。

## 补充：与 Demo 的初始化差异
对比 Demo 日志确认：
- Demo 在 `init` 后调用了 4 次 `initPlayRes(0..3)`；
- Demo 每秒轮询 `isReady()`，收到 READY 后才查询会议；
- Demo 日志确实收到 `T:DEVICEVALIDATE/M:NOTIFY` 和 `T:...READY` 事件；
- Demo 查询结果：`queryMeetings ret=1, count=1`。

目标工程已同步补齐：
- `PaperlessSdk.init()` 增加 `initPlayRes(0..3)`；
- `init()` 内轮询 `isReady()`，默认最多等 15 秒；
- `queryMeetings/queryRooms` 不再要求 `ret==0`：Demo/SDK 用 `ret=1` 表示成功；
- 从 Demo 原样恢复 `client.ini`、`client.dev`（目标原文件与 Demo 大小/编码/内容不一致）。

验证结果：
```text
init 成功, ready=true
事件: T:DEVICEVALIDATE/M:NOTIFY
事件: T:READY/M:NOTIFY
事件: T:TIME/M:NOTIFY
queryMeetings 成功: 1
会议 37 快捷会议 status=1, roomId=1
```

事件轮询已在 `init` 成功后立即启动（不再等登录），因此能收到 Demo 一样的 1 秒一次服务器时间通知。

未补齐：
- Demo 的 `getEvents()` 只打印事件字符串，没有解析 `DEVICEVALIDATE` 的 `valList/valflag`；
- 设备 ID、初始化状态码等仍需 SDK 补类型化接口。

`Index.ets` 增加了设备/会议信息栏：
- 设备ID：当前 `libmeet.so` 没有暴露 `getSelfDeviceId`，显示 `--`，不伪造；
- 会议室ID：取当前选中会议的 `roomId`；
- 会议ID：取当前选中会议 `id`；
- 参会人ID：`getSelfMemberId()`；
- 界面状态：当前 SDK 页面状态。

真机已安装启动，UI 树确认 5 个字段均显示。设备ID需要 SDK 团队补接口后再接。
