export interface MeetingItem {
  id: number;
  name: string;
  roomId: number;
  roomName: string;
  secrecy: number;
  startTime: number;
  endTime: number;
  status: number;
  signinType: number;
  managerId: number;
  orderName: string;
}

export interface MeetingResult {
  ret: number;
  items: MeetingItem[];
}

export interface RoomItem {
  id: number;
  name: string;
  addr: string;
}

export interface RoomResult {
  ret: number;
  items: RoomItem[];
}

export interface MeetingDeviceItem {
  id: number;
  name: string;
}

export interface MeetingDeviceResult {
  ret: number;
  items: MeetingDeviceItem[];
}

export interface EventResult {
  items: string[];
}

export interface SdkResult<T> {
  ok: boolean;
  code: number;
  message: string;
  data?: T;
}
