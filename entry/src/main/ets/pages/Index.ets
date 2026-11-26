import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { PaperlessSdk } from '../core_sdk/PaperlessSdk';
import { MeetingItem, MeetingDeviceItem } from '../core_sdk/model/Models';

const DOMAIN = 0x0000;
const TAG = 'sdk-smoke';

@Entry
@Component
struct Index {
  @State serverIp: string = '10.248.6.98';
  @State serverPort: string = '21600';
  @State user: string = '';
  @State password: string = '';
  @State status: string = '未初始化';
  @State ready: boolean = false;
  @State loggedIn: boolean = false;
  @State meetings: MeetingItem[] = [];
  @State logText: string = '';
  @State busy: boolean = false;
  @State selfMemberId: number = 0;
  @State currentMeetingId: number = 0;
  @State currentRoomId: number = 0;
  @State signedIn: boolean = false;
  @State devices: MeetingDeviceItem[] = [];

  private sdk: PaperlessSdk | null = null;

  aboutToAppear(): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    this.sdk = new PaperlessSdk(ctx);
    this.appendLog('core_sdk 已加载，等待初始化');
  }

  aboutToDisappear(): void {
    if (this.sdk) {
      this.sdk.exit();
      this.sdk = null;
    }
  }

  appendLog(msg: string): void {
    const line = `${new Date().toLocaleTimeString()} ${msg}`;
    const next = `${line}\n${this.logText}`;
    this.logText = next.length > 6000 ? next.substring(0, 6000) : next;
  }

  async doInit(): Promise<void> {
    if (!this.sdk || this.busy) return;
    this.busy = true;
    this.status = '初始化中...';
    try {
      await this.sdk.init(this.serverIp, Number(this.serverPort));
      this.ready = this.sdk.isReady();
      this.appendLog(`init 成功, ready=${this.ready}`);
      this.status = this.ready ? 'SDK 已就绪' : 'SDK 已初始化(未就绪)';
      // Demo 在初始化前就开始轮询 getEvents；目标在 init 成功后立即启动，
      // 否则登录前收不到 T:TIME/M:NOTIFY 等服务器事件。
      this.sdk.startEvents((event: string) => {
        this.appendLog(`事件: ${event}`);
      });
    } catch (e) {
      this.appendLog(`init 失败: ${String(e)}`);
      this.status = '初始化失败';
    } finally {
      this.busy = false;
    }
  }

  async doLogin(): Promise<void> {
    if (!this.sdk || this.busy) return;
    this.busy = true;
    try {
      await this.sdk.login(this.user, this.password);
      this.appendLog('login 成功');
      this.loggedIn = true;
      this.selfMemberId = this.sdk.getSelfMemberId();
      this.signedIn = this.sdk.isSignedIn();
      this.appendLog(`参会人 id=${this.selfMemberId}, signedIn=${this.signedIn}`);
      this.status = '已登录';
    } catch (e) {
      this.appendLog(`login 失败: ${String(e)}`);
      this.status = '登录失败';
    } finally {
      this.busy = false;
    }
  }

  async doQueryMeetings(): Promise<void> {
    if (!this.sdk || this.busy) return;
    this.busy = true;
    try {
      const list = await this.sdk.queryMeetings();
      this.meetings = list;
      this.appendLog(`queryMeetings 成功: ${list.length}`);
      list.forEach((m: MeetingItem) => {
        this.appendLog(`会议 ${m.id} ${m.name} status=${m.status}, roomId=${m.roomId}`);
      });
    } catch (e) {
      this.appendLog(`queryMeetings 失败: ${String(e)}`);
    } finally {
      this.busy = false;
    }
  }

  async doEnterAndSign(): Promise<void> {
    if (!this.sdk || this.busy || this.meetings.length === 0) {
      this.appendLog('没有可进入的会议');
      return;
    }
    this.busy = true;
    const first = this.meetings[0];
    try {
      await this.sdk.selectMeeting(first.id);
      this.currentMeetingId = first.id;
      this.currentRoomId = first.roomId;
      this.appendLog(`已设置当前会议: ${first.id}, roomId=${first.roomId}`);
      await this.sdk.enterMeeting();
      this.appendLog('进入会议成功');
      await this.sdk.signIn(first.signinType);
      this.signedIn = this.sdk.isSignedIn();
      this.selfMemberId = this.sdk.getSelfMemberId();
      this.appendLog(`签到成功, memberId=${this.selfMemberId}`);
      this.status = `已进入会议: ${first.name}`;
    } catch (e) {
      this.appendLog(`进入/签到失败: ${String(e)}`);
    } finally {
      this.busy = false;
    }
  }

  async doQueryDevices(): Promise<void> {
    if (!this.sdk || this.busy) return;
    this.busy = true;
    try {
      const list = await this.sdk.queryMeetingDevices();
      this.devices = list;
      this.appendLog(`入会设备: ${list.length} 台`);
      list.forEach((d: MeetingDeviceItem) => {
        this.appendLog(`设备 id=${d.id}, name=${d.name}`);
      });
    } catch (e) {
      this.appendLog(`查询设备失败: ${String(e)}`);
    } finally {
      this.busy = false;
    }
  }

  build() {
    Column({ space: 8 }) {
      Text('HarmonyPaperless · 阶段 0 SDK 验证')
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .width('100%')

      Row({ space: 8 }) {
        TextInput({ text: this.serverIp, placeholder: '服务器 IP' })
          .onChange((v: string) => this.serverIp = v)
          .layoutWeight(1)
        TextInput({ text: this.serverPort, placeholder: '端口' })
          .type(InputType.Number)
          .onChange((v: string) => this.serverPort = v)
          .width(100)
      }.width('100%')

      Row({ space: 8 }) {
        TextInput({ text: this.user, placeholder: '账号' })
          .onChange((v: string) => this.user = v)
          .layoutWeight(1)
        TextInput({ text: this.password, placeholder: '密码' })
          .type(InputType.Password)
          .onChange((v: string) => this.password = v)
          .layoutWeight(1)
      }.width('100%')

      Row({ space: 8 }) {
        Button('初始化').onClick(() => this.doInit()).layoutWeight(1)
        Button('登录').onClick(() => this.doLogin()).layoutWeight(1)
        Button('查询会议').onClick(() => this.doQueryMeetings()).layoutWeight(1)
        Button('进入/签到').onClick(() => this.doEnterAndSign()).layoutWeight(1)
      }.width('100%')

      Row({ space: 8 }) {
        Button('查询入会设备').onClick(() => this.doQueryDevices()).layoutWeight(1)
        Text(`入会设备：${this.devices.length} 台`)
          .fontSize(13)
          .layoutWeight(2)
      }.width('100%')

      Text(`状态：${this.status}   ready=${this.ready}   login=${this.loggedIn}   meetings=${this.meetings.length}`)
        .width('100%')
        .fontSize(14)

      Row({ space: 8 }) {
        Column({ space: 2 }) {
          Text('设备ID').fontSize(11).fontColor('#666666')
          Text('--').fontSize(13)
        }.layoutWeight(1)

        Column({ space: 2 }) {
          Text('会议室ID').fontSize(11).fontColor('#666666')
          Text(`${this.currentRoomId}`).fontSize(13)
        }.layoutWeight(1)

        Column({ space: 2 }) {
          Text('会议ID').fontSize(11).fontColor('#666666')
          Text(`${this.currentMeetingId}`).fontSize(13)
        }.layoutWeight(1)

        Column({ space: 2 }) {
          Text('参会人ID').fontSize(11).fontColor('#666666')
          Text(`${this.selfMemberId}`).fontSize(13)
        }.layoutWeight(1)

        Column({ space: 2 }) {
          Text('界面状态').fontSize(11).fontColor('#666666')
          Text(this.status).fontSize(13).maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
        }.layoutWeight(2)
      }
      .width('100%')
      .padding({ top: 6, bottom: 6, left: 6, right: 6 })
      .backgroundColor('#F7F7F7')
      .borderRadius(6)

      Scroll() {
        Text(this.logText)
          .width('100%')
          .fontSize(12)
          .textAlign(TextAlign.Start)
      }
      .layoutWeight(1)
      .width('100%')
      .backgroundColor('#F2F2F2')
      .padding(8)
    }
    .padding(12)
    .width('100%')
    .height('100%')
  }
}
