#ifndef OHOS_VIDEO_RENDER_H
#define OHOS_VIDEO_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int OhosRenderer_InstallDisplayCallbacks(void);
int OhosRenderer_SetSurfaceId(int res, uint64_t surfaceId);
int OhosRenderer_Start(int res);
int OhosRenderer_Stop(int res);
void OhosRenderer_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
