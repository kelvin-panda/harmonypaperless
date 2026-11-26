import { hilog } from '@kit.PerformanceAnalysisKit';
import { meetNative } from '../native/MeetNative';

const DOMAIN = 0x0000;
const TAG = 'core-sdk-events';

export interface NativeEventResult {
  items: string[];
}

export type EventCallback = (event: string) => void;

/**
 * 轮询 libmeet 的 getEvents()，把 JSON 数组拆成逐条事件。
 * 事件只做分发，不在回调里再次触发对账/刷新，避免死循环。
 */
export class EventPoller {
  private timer: number = -1;
  private last: string = '[]';
  private callback: EventCallback | null = null;

  start(callback: EventCallback, intervalMs: number = 1000): void {
    this.callback = callback;
    this.stop();
    this.timer = setInterval(() => {
      try {
        const raw = meetNative.getEvents();
        if (!raw || raw.length <= 2 || raw === this.last) {
          return;
        }
        this.last = raw;
        const result = JSON.parse(raw) as NativeEventResult;
        if (result.items && result.items.length > 0) {
          result.items.forEach((item: string) => {
            if (this.callback) {
              this.callback(item);
            }
          });
        }
      } catch (e) {
        hilog.error(DOMAIN, TAG, 'poll events failed: %{public}s', String(e));
      }
    }, intervalMs);
  }

  stop(): void {
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
  }
}
