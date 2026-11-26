# 阶段 0 SDK 缺口清单

## 已确认可用
来自 HarmonyDemo 的 `libmeet/index.d.ts` 和真机验证：
- 配置：`prepareConfig`
- 生命周期：`init / exit / isReady / isLoggedIn`
- 登录：`login`
- 会议：`queryMeetings / queryRooms / addMeeting / deleteMeeting`
- 当前会议：`setCurrentMeeting / enterMeeting`
- 签到：`signIn / isSignedIn / getSelfMemberId`
- 事件：`getEvents`
- 媒体：`queryMedia / playMedia / closePlayRes / getPlayProgress`
- 屏幕：`setSurfaceId / initPlayRes / playStream / stopPlayStream / startScreenShare / stopScreenShare`
- 设备：`queryMeetingDevices`

### 已实测
`queryMeetingDevices()` 能查询当前会议的入会设备列表，`ret` 表示设备数量，`items[]` 是 `{id,name}`：
```text
入会设备: 2 台
设备 id=17858609, name=Pad-31
设备 id=17858575, name=Pad-f
```

但它只返回**入会设备列表**，不能直接返回“当前设备自己的会议状态”。当前设备信息仍缺：
- 当前设备 ID；
- 当前设备 meetId/roomId/memberId 的结构化状态；
- `DEVICEMEETSTATUS` 事件字段。

## 与 Android DEVICEVALIDATE 的差异
Android 初始化结果通过 `Pb_TYPE_MEET_INTERFACE_DEVICEVALIDATE_VALUE` 异步通知，`pbui_Type_DeviceValidate` 的 `valList/valflag` 中包含：
- 区域服务器 ID
- **设备 ID**
- 初始化状态码
- 到期时间
- 企业 ID
- 协议版本
- 在线/最大设备数

HarmonyDemo 当前只有：
- `init()` 返回初始化调用结果；
- `isReady() === 1` 表示 SDK ready；
- `getEvents()` 返回字符串事件列表，但 d.ts 没有定义设备校验事件字段。

因此：
- **只判断“初始化是否完成”**：Demo 的 `isReady` 可以满足；
- **需要 DEVICEVALIDATE 的完整数据（尤其设备 ID）**：Demo 不满足，需要 SDK 补 `getDeviceValidateInfo()` 或让 `getEvents` 返回带字段的类型化事件。

设备 ID 不能用 `getSelfMemberId()` 代替；二者是不同概念。

## 目标 App 需要、Demo 当前未声明
以下接口在 Android `JniHelper` 中使用，但 Demo 的 `libmeet/index.d.ts` 没有直接声明，阶段 5/6 前必须确认：

1. `queryDir` / `queryFile`：目录、目录文件；
2. 文件下载/上传、缓存文件路径；
3. `queryBindPdf(mediaId)`、绑定 PDF 文件名/大小；
4. `queryOfflineMeet()`：离线会议 JSON；
5. 服务端文件索引/对账；
6. 文件/目录/会议删除通知的完整字段；
7. 会议创建时间戳、保密会议状态、成员/权限；
8. 会议结束/设备会议清空事件。

## 确认方式
1. 检查 `libmeet.so` 导出符号与 C++ 头文件；
2. 向 SDK 团队索取 Harmony NAPI 声明或 HAR；
3. 若 SDK 确实没有该能力，需先补 NAPI，不能由客户端 UI 层伪造。

## 阶段 5/6 开工条件
- 至少确认 `queryDir/queryFile`、文件下载/缓存、`queryBindPdf`、`queryOfflineMeet`；
- 否则阶段 5/6 只允许做接口占位和 UI 空壳，不得标记为已完成。
