#ifndef OHOS_SCREENCAPTURE_H
#define OHOS_SCREENCAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

int OhosCapture_Init(int index);
int OhosCapture_SetConfig(int index, int width, int height, int bitrate);

#ifdef __cplusplus
}
#endif

#endif
