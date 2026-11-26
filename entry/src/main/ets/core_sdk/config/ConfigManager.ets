import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { meetNative } from '../native/MeetNative';

const DOMAIN = 0x0000;
const TAG = 'core-sdk-config';

/**
 * 负责 client.ini/client.dev 的准备与 prepareConfig。
 * 路径全部放应用沙箱 filesDir/meet，禁止使用 Android 路径。
 */
export class ConfigManager {
  private context: common.UIAbilityContext;
  private meetDir: string = '';

  constructor(context: common.UIAbilityContext) {
    this.context = context;
  }

  getMeetDir(): string {
    return this.meetDir;
  }

  getConfigPath(): string {
    return this.meetDir + '/client.ini';
  }

  private copyRaw(name: string, dst: string): void {
    const data = this.context.resourceManager.getRawFileContentSync(name);
    const file = fs.openSync(dst, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
    try {
      fs.writeSync(file.fd, data.buffer as ArrayBuffer);
    } finally {
      fs.closeSync(file);
    }
  }

  /** 复制 rawfile/meet 下的配置模板到沙箱，生成 client.ini */
  prepare(serverIp: string, serverPort: number): string {
    this.meetDir = this.context.filesDir + '/meet';
    try {
      fs.mkdirSync(this.meetDir, true);
    } catch (e) {
      hilog.info(DOMAIN, TAG, 'meet dir exists');
    }
    const templatePath = this.meetDir + '/client.tpl.ini';
    const configPath = this.meetDir + '/client.ini';
    this.copyRaw('meet/client.ini', templatePath);
    this.copyRaw('meet/client.dev', this.meetDir + '/client.dev');
    const ret = meetNative.prepareConfig(templatePath, configPath, serverIp, serverPort);
    hilog.info(DOMAIN, TAG, 'prepareConfig ret=%{public}d, path=%{public}s', ret, configPath);
    if (ret !== 0) {
      throw new Error(`prepareConfig failed: ${ret}`);
    }
    return configPath;
  }
}
