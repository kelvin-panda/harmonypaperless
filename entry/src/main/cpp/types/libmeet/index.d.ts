export const prepareConfig: (templatePath: string, outPath: string,
                              serverIp: string, serverPort: number) => number;
export const init: (configPath: string) => Promise<number>;
export const exit: () => number;
export const isReady: () => number;
export const isLoggedIn: () => number;
export const login: (user: string, password: string) => Promise<number>;
export const queryMeetings: () => Promise<string>;
export const queryRooms: () => Promise<string>;
export const addMeeting: (name: string, roomId: number,
                          startSec: number, endSec: number) => Promise<number>;
export const deleteMeeting: (id: number) => Promise<number>;
export const getEvents: () => string;
export const setSurfaceId: (res: number, surfaceId: string) => number;
export const initPlayRes: (res: number, x: number, y: number,
                            w: number, h: number) => number;
export const setCurrentMeeting: (meetingId: number) => number;
export const queryMedia: () => Promise<string>;
export const playMedia: (mediaId: number, res: number,
                          loop: number) => Promise<number>;
export const closePlayRes: (res: number) => Promise<number>;
export const getPlayProgress: () => string;
export const signIn: (signinType: number) => Promise<number>;
export const enterMeeting: () => Promise<number>;
export const setFaceState: (state: number) => Promise<number>;
export const playStream: (deviceId: number, subId: number, res: number,
                           playflag: number) => Promise<number>;
export const stopPlayStream: () => Promise<number>;
export const queryMeetingDevices: () => Promise<string>;
export const startScreenShare: () => Promise<number>;
export const stopScreenShare: () => Promise<number>;
export const getStreamState: () => string;
export const isScreenSharing: () => number;
export const getSelfMemberId: () => number;
export const isSignedIn: () => number;
