export const init: () => number;
export const selfTest: () => number;
export const setSurface: (surfaceId: string, res: number) => number;
export const clearSurface: (res: number) => number;
export const installDisplayCallbacks: () => number;

export const playFile: (path: string, surfaceId: string, res: number) => number;
export const playFileFd: (fd: number, surfaceId: string, res: number) => number;
export const stopPlay: () => number;

export const startDesktopCapture: (surfaceId: string, res: number,
                                   width?: number, height?: number,
                                   bitrate?: number) => number;
export const stopDesktopCapture: () => number;

export const startScreenStream: (surfaceId: string, res: number) => number;
export const pushScreenFrame: (data: ArrayBuffer, ptsMs: number,
                               keyframe?: number) => number;
export const stopScreenStream: () => number;

export const startAudioStream: (codecType?: number, sampleRate?: number,
                                channels?: number) => number;
export const pushAudioFrame: (data: ArrayBuffer, ptsMs?: number) => number;
export const stopAudioStream: () => number;

export const startScreenLoopback: (surfaceId: string, res: number,
                                   width?: number, height?: number,
                                   bitrate?: number) => number;
export const stopScreenLoopback: () => number;

export const startCameraPreview: (surfaceId: string) => number;
export const stopCameraPreview: () => number;

export const getStats: () => string;
export const stopAll: () => number;

/** 调试辅助：修改沙箱目录权限，便于 hdc file send 推送测试片源。 */
export const chmodPath: (path: string, mode: number) => number;
