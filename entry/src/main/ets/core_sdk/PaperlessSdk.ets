import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { ConfigManager } from './config/ConfigManager';
import { EventPoller, EventCallback } from './events/EventPoller';
import { meetNative } from './native/MeetNative';
import { MeetingItem, MeetingResult, RoomItem, RoomResult, MeetingDeviceItem, MeetingDeviceResult } from './model/Models';

const DOMAIN = 0x0000;
const TAG = 'core-sdk';

/**
 * 会议 SDK 门面：页面只依赖本类，不直接依赖 libmeet.so。
 * 本阶段只封装 Demo 已确认存在的接口；文件/离线/绑定 PDF 等接口待确认后再加。
 */
export class PaperlessSdk {
  private context: common.UIAbilityContext;
  private configManager: ConfigManager;
  private eventPoller: EventPoller = new EventPoller();
  private initialized: boolean = false;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
    this.configManager = new ConfigManager(context);
  }

  isReady(): boolean {
    return meetNative.isReady() === 1;
  }

  isLoggedIn(): boolean {
    return meetNative.isLoggedIn() === 1;
  }

  async init(serverIp: string, serverPort: number): Promise<void> {
    const configPath = this.configManager.prepare(serverIp, serverPort);
    const ret = await meetNative.init(configPath);
    if (ret !== 0) {
      throw new Error(`SDK init failed: ${ret}`);
    }
    this.initialized = true;
    // 与 Demo 保持一致：初始化播放资源；queryMeetings 必须在 SDK READY 之后调用
    for (let res = 0; res < 4; res++) {
      meetNative.initPlayRes(res, 0, 0, 1920, 1080);
    }
    await this.waitUntilReady();
    hilog.info(DOMAIN, TAG, 'SDK initialized and ready');
  }

  /** 轮询 isReady，避免 init 返回后立刻查询会议得到空列表 */
  private async waitUntilReady(timeoutMs: number = 15000): Promise<void> {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
      if (meetNative.isReady() === 1) {
        return;
      }
      await new Promise<void>((resolve) => {
        setTimeout(resolve, 300);
      });
    }
    throw new Error('SDK ready timeout');
  }

  async login(user: string, password: string): Promise<void> {
    const ret = await meetNative.login(user, password);
    if (ret !== 0) {
      throw new Error(`SDK login failed: ${ret}`);
    }
    hilog.info(DOMAIN, TAG, 'SDK login success');
  }

  async queryMeetings(): Promise<MeetingItem[]> {
    const raw = await meetNative.queryMeetings();
    const result = JSON.parse(raw) as MeetingResult;
    // Demo 的 queryMeetings 返回 ret=1、count=1 仍表示成功；不能要求 ret==0
    hilog.info(DOMAIN, TAG, 'queryMeetings ret=%{public}d count=%{public}d',
      result.ret, result.items ? result.items.length : 0);
    if (!result.items) {
      throw new Error(`queryMeetings invalid result: ${raw}`);
    }
    return result.items;
  }

  async queryRooms(): Promise<RoomItem[]> {
    const raw = await meetNative.queryRooms();
    const result = JSON.parse(raw) as RoomResult;
    hilog.info(DOMAIN, TAG, 'queryRooms ret=%{public}d count=%{public}d',
      result.ret, result.items ? result.items.length : 0);
    if (!result.items) {
      throw new Error(`queryRooms invalid result: ${raw}`);
    }
    return result.items;
  }

  async queryMeetingDevices(): Promise<MeetingDeviceItem[]> {
    const raw = await meetNative.queryMeetingDevices();
    const result = JSON.parse(raw) as MeetingDeviceResult;
    // Demo 中 ret 表示入会设备数量，不是 0/1 成功码
    hilog.info(DOMAIN, TAG, 'queryMeetingDevices ret=%{public}d count=%{public}d',
      result.ret, result.items ? result.items.length : 0);
    if (!result.items) {
      throw new Error(`queryMeetingDevices invalid result: ${raw}`);
    }
    return result.items;
  }

  async selectMeeting(meetingId: number): Promise<void> {
    const ret = meetNative.setCurrentMeeting(meetingId);
    if (ret !== 0) {
      throw new Error(`setCurrentMeeting failed: ${ret}`);
    }
  }

  async enterMeeting(): Promise<void> {
    const ret = await meetNative.enterMeeting();
    if (ret !== 0) {
      throw new Error(`enterMeeting failed: ${ret}`);
    }
  }

  async signIn(signinType: number = 0): Promise<void> {
    const ret = await meetNative.signIn(signinType);
    if (ret !== 0) {
      throw new Error(`signIn failed: ${ret}`);
    }
  }

  /** 当前参会人 id；设备 id 当前 NAPI 未暴露 */
  getSelfMemberId(): number {
    return meetNative.getSelfMemberId();
  }

  isSignedIn(): boolean {
    return meetNative.isSignedIn() === 1;
  }

  startEvents(callback: EventCallback): void {
    this.eventPoller.start(callback);
  }

  stopEvents(): void {
    this.eventPoller.stop();
  }

  exit(): void {
    this.stopEvents();
    if (this.initialized) {
      meetNative.exit();
      this.initialized = false;
    }
  }
}
