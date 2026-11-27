# 阶段 1：core_sdk 完善与联调

## 目标
把阶段 0 的冒烟代码整理成可复用的 `core_sdk` 状态机，并验证初始化、会议、会场、签到、设备和事件。

## 已完成
- `PaperlessSdk` 增加 `SdkState`：
  - `UNINITIALIZED`
  - `INITIALIZING`
  - `READY`
  - `LOGGED_IN`
  - `DISPOSED`
- `init()`：
  - 防止重复初始化；
  - 校验 `prepareConfig` 返回值；
  - `initPlayRes(0..3)`；
  - 等待 `isReady()==1`，最多 15 秒。
- `login()`：
  - 按 Demo 约定以 `isLoggedIn()` 判断成功，不再假设 `ret==0`。
- `queryMeetings() / queryRooms()`：
  - 不再要求 `ret==0`（Demo 中 `ret=1` 表示成功）；
  - 打印真实 ret/count。
- `queryMeetingDevices()`：
  - 返回入会设备列表；
  - `ret` 表示设备数量。
- `ensureReady()`：
  - 查询/进入/签到前校验状态。
- 事件轮询：
  - `init` 成功后立即启动；
  - 每秒收到服务器 `T:TIME/M:NOTIFY`；
  - 退出时 `stopEvents()`。

## 真机验证
初始化：

```text
init 成功, ready=true
事件: T:DEVICEVALIDATE/M:NOTIFY
事件: T:READY/M:NOTIFY
事件: T:TIME/M:NOTIFY
```

会议：

```text
queryMeetings 成功: 1
会议 37 快捷会议 status=1, roomId=1
```

会场：

```text
查询会场: 4
会场 id=1, name=会议室
会场 id=2, name=会议室11
会场 id=3, name=会议室2
会场 id=4, name=测试会议室
```

签到/入会：

```text
进入会议成功
签到成功, memberId=26
```

入会设备：

```text
入会设备: 2 台
设备 id=17858609, name=Pad-31
设备 id=17858575, name=Pad-f
```

## 登录验证
使用测试账号 `root` / `123456`，真机验证通过：

```text
login 成功
参会人 id=26, signedIn=false
状态：已登录 ready=true login=true
```

登录后继续验证：

```text
queryMeetings 成功: 1
会议 37 快捷会议 status=1, roomId=1
进入会议成功
签到成功, memberId=26
```

## 入会设备
入会设备列表在未登录时实测可返回：

```text
入会设备: 2 台
设备 id=17858609, name=Pad-31
设备 id=17858575, name=Pad-f
```

登录为管理员后再次查询返回 0 台，说明该接口受当前会议/登录角色/设备在线状态影响，后续需要继续确认语义；不影响登录、会议、会场、签到链路。

## 未验证/限制
1. `queryMeetingDevices` 在管理员登录后返回 0 台，需确认服务端当前会议是否有其他在线设备。
2. 设备 ID、DEVICEVALIDATE 的 `valList/valflag` 仍没有结构化 NAPI。
3. `client.dev` 未提交仓库（设备授权文件，已 ignore），新环境需从 Demo/厂商提供。
4. 提交仓库的 `build-profile.json5` 清空签名配置；本地构建前需配置自己的签名。
