#ifndef OHOS_MEET_BRIDGE_H
#define OHOS_MEET_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copy the client.ini template to outPath and rewrite sandbox paths plus the
 * server address from the UI.  Keeps the original (GBK) bytes untouched. */
int MeetBridge_PrepareConfig(const char *templatePath, const char *outPath,
                             const char *serverIp, int serverPort);

int MeetBridge_Init(const char *configPath);
int MeetBridge_IsReady(void);
int MeetBridge_IsLoggedIn(void);
int MeetBridge_Login(const char *user, const char *password);
void MeetBridge_Exit(void);

/* JSON: {"ret":<meetcore ret>,"items":[...]}; caller frees with free(). */
char *MeetBridge_QueryMeetings(void);
char *MeetBridge_QueryRooms(void);

/* start/end are Unix seconds.  Returns 0 on success. */
int MeetBridge_AddMeeting(const char *name, uint32_t roomId,
                          int64_t startTime, int64_t endTime);
int MeetBridge_DeleteMeeting(uint32_t meetingId);

/* JSON: {"items":["type/method", ...]}; drains the event queue. */
char *MeetBridge_GetEvents(void);

/* ---- receive / local playback (OHOS renderer + OHAudio) ---------------- */

/* Bind the XComponent surface to a libExecProc video resource. */
int MeetBridge_SetSurfaceId(int res, uint64_t surfaceId);
int MeetBridge_StartRender(int res);
int MeetBridge_StopRender(int res);

/* Mark a resource as displayable (MEETINITPLAYRES).  Called for res 0..3
 * after meetcore_init; safe to call again with actual rect values. */
int MeetBridge_InitPlayRes(int res, int x, int y, int w, int h);

/* Set/query MEETCONTEXT_PROPERTY_CURMEETINGID (media list/playback scope). */
int MeetBridge_SetCurrentMeeting(uint32_t meetingId);

/* JSON: {"ret":x,"dirs":[{"id":..,"name":".."}],
 *        "items":[{"dirId":..,"id":..,"name":"..","size":..,"filepos":..}]} */
char *MeetBridge_QueryMediaFiles(void);

/* Local media playback on resource "res": devnum=0 -> the SDK adds self.
 * loop != 0 sets MEDIA_PLAYFLAG_LOOP.  Returns 0 on success. */
int MeetBridge_PlayMediaLocal(uint32_t mediaId, int res, int loop);
int MeetBridge_ClosePlayRes(int res);

/* JSON of the latest TYPE_MEET_INTERFACE_MEDIAPLAYPOSINFO notification:
 * {"mediaId":..,"status":0..3,"per":0..100,"sec":..,"res":..,"valid":0|1}
 * status: 0=playing, 1=paused, 2=stopped, 3=resumed. */
char *MeetBridge_GetPlayProgress(void);

/* ---- meeting sign-in / enter ------------------------------------------ */

/* Bound member id of the current meeting (0 = none / not in a meeting). */
int MeetBridge_GetSelfMemberId(void);

/* 1 if the bound member is already signed in. */
int MeetBridge_IsSignedIn(void);

/* Send MEETSIGN/ADD for the bound member (memberid=0).  signinType < 0 uses
 * signin_direct (0).  Waits briefly for the server round-trip.
 * Returns 0 = signed in, 1 = request sent but not confirmed, <0 = error. */
int MeetBridge_SignIn(int signinType);

/* Send DEVICEOPER/ENTER for this device so the server switches it into the
 * meeting face.  Call after sign-in.  Returns 0 on success. */
int MeetBridge_EnterMeeting(void);

/* Set MEETCONTEXT_PROPERTY_ROLE (interface/face state).  The library
 * broadcasts it to all terminals via SmartSendDevOperState.
 * MemState_MainFace=0 (main UI), MemState_MemFace=1 (meeting UI). */
int MeetBridge_SetFaceState(int state);

/* Modify a participant's role: TYPE_MEET_INTERFACE_MEETSEAT + MODIFY.
 * memberId is the participant id; role uses Pb_MeetMemberRole values
 * (1=normal, 3=compere, 4=secretary, 9=admin). */
int MeetBridge_ModifyMemberRole(uint32_t memberId, int role);

/* ---- screen share / stream receive ------------------------------------ */

/* JSON: {"ret":n,"items":[{"id":..,"name":"..","memberId":..,"facestate":..}]}
 * Devices currently in the meeting (online, meetingid = current). */
char *MeetBridge_QueryMeetingDevices(void);

/* Play a source device's stream on this tablet (e.g. a PC's desktop).
 * subId is the source stream channel (screen = 2), res the local play
 * resource.  devnum=0 lets the SDK expand the play device list to self. */
int MeetBridge_PlayStreamOnSelf(uint32_t deviceId, int subId, int res,
                                int playflag);

/* Stops the stream currently playing on this tablet using the trigger id
 * from the STREAMPLAY notification (falls back to closing the resource). */
int MeetBridge_StopPlayStream(void);

/* JSON: {"active":1,"src":..,"sub":..,"res":..,"trigger":..}
 * Filled from STREAMPLAY / STOPPLAY notifications. */
char *MeetBridge_GetStreamState(void);

/* Share this tablet's screen on all devices that are in the current
 * meeting.  Returns the number of target devices on success, <0 on error. */
int MeetBridge_StartScreenShare(void);
int MeetBridge_StopScreenShare(void);
int MeetBridge_IsScreenSharing(void);

#ifdef __cplusplus
}
#endif

#endif
