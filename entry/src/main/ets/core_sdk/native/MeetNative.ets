import meet from 'libmeet.so';

/**
 * libmeet.so 的 ArkTS 类型镜像。
 * 只声明 Demo/当前 SDK 已确认存在的接口；缺失接口不要在这里臆造。
 */
export interface MeetNativeApi {
  prepareConfig(templatePath: string, outPath: string, serverIp: string, serverPort: number): number;
  init(configPath: string): Promise<number>;
  exit(): number;
  isReady(): number;
  isLoggedIn(): number;
  login(user: string, password: string): Promise<number>;

  queryMeetings(): Promise<string>;
  queryRooms(): Promise<string>;
  addMeeting(name: string, roomId: number, startSec: number, endSec: number): Promise<number>;
  deleteMeeting(id: number): Promise<number>;
  getEvents(): string;

  setCurrentMeeting(meetingId: number): number;
  enterMeeting(): Promise<number>;
  signIn(signinType: number): Promise<number>;
  isSignedIn(): number;
  getSelfMemberId(): number;
  setFaceState(state: number): Promise<number>;
  queryMeetingDevices(): Promise<string>;

  initPlayRes(res: number, x: number, y: number, w: number, h: number): number;

  queryMedia(): Promise<string>;
  playMedia(mediaId: number, res: number, loop: number): Promise<number>;
  closePlayRes(res: number): Promise<number>;
  getPlayProgress(): string;
  setSurfaceId(res: number, surfaceId: string): number;
  initPlayRes(res: number, x: number, y: number, w: number, h: number): number;
  playStream(deviceId: number, subId: number, res: number, playflag: number): Promise<number>;
  stopPlayStream(): Promise<number>;

  startScreenShare(): Promise<number>;
  stopScreenShare(): Promise<number>;
  getStreamState(): string;
  isScreenSharing(): number;
}

export const meetNative: MeetNativeApi = meet as MeetNativeApi;
