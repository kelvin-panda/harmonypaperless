/* NAPI bridge for the HarmonyOS meeting SDK control-path test. */
#include <stdlib.h>
#include <string.h>

#include <napi/native_api.h>

#include "ohos_meet_bridge.h"

#ifndef MEET_NAPI_MODNAME
#define MEET_NAPI_MODNAME "meet"
#endif

enum MeetOp {
    OP_INIT = 1,
    OP_LOGIN,
    OP_QUERY_MEETINGS,
    OP_QUERY_ROOMS,
    OP_ADD_MEETING,
    OP_DELETE_MEETING,
    OP_QUERY_MEDIA,
    OP_PLAY_MEDIA,
    OP_CLOSE_PLAY_RES,
    OP_SIGN_IN,
    OP_ENTER_MEETING,
    OP_SET_FACE_STATE,
    OP_MODIFY_MEMBER_ROLE,
    OP_PLAY_STREAM,
    OP_STOP_PLAY_STREAM,
    OP_QUERY_MEETING_DEVICES,
    OP_SCREEN_SHARE_START,
    OP_SCREEN_SHARE_STOP,
};

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    int op;
    char s1[1024];
    char s2[256];
    int32_t i1;
    int64_t i2;
    int64_t i3;
    int32_t i4;
    int ret;
    char *json;
} MeetAsync;

static int arg_string(napi_env env, napi_value v, char *buf, size_t size)
{
    size_t len = 0;
    if (!buf || size == 0)
        return -1;
    if (napi_get_value_string_utf8(env, v, buf, size - 1, &len) != napi_ok)
        return -1;
    buf[len] = '\0';
    return 0;
}

static int arg_int(napi_env env, napi_value v, int32_t *out)
{
    return napi_get_value_int32(env, v, out) == napi_ok ? 0 : -1;
}

static int arg_i64(napi_env env, napi_value v, int64_t *out)
{
    return napi_get_value_int64(env, v, out) == napi_ok ? 0 : -1;
}

static napi_value ret_int(napi_env env, int v)
{
    napi_value out;
    napi_create_int32(env, v, &out);
    return out;
}

static napi_value ret_string(napi_env env, const char *s)
{
    napi_value out;
    napi_create_string_utf8(env, s ? s : "", NAPI_AUTO_LENGTH, &out);
    return out;
}

static void meet_execute(napi_env env, void *data)
{
    MeetAsync *a = (MeetAsync *)data;
    (void)env;
    switch (a->op) {
    case OP_INIT:
        a->ret = MeetBridge_Init(a->s1);
        break;
    case OP_LOGIN:
        a->ret = MeetBridge_Login(a->s1, a->s2);
        break;
    case OP_QUERY_MEETINGS:
        a->json = MeetBridge_QueryMeetings();
        a->ret = a->json ? 0 : -1;
        break;
    case OP_QUERY_ROOMS:
        a->json = MeetBridge_QueryRooms();
        a->ret = a->json ? 0 : -1;
        break;
    case OP_ADD_MEETING:
        a->ret = MeetBridge_AddMeeting(a->s1, (uint32_t)a->i1, a->i2, a->i3);
        break;
    case OP_DELETE_MEETING:
        a->ret = MeetBridge_DeleteMeeting((uint32_t)a->i1);
        break;
    case OP_QUERY_MEDIA:
        a->json = MeetBridge_QueryMediaFiles();
        a->ret = a->json ? 0 : -1;
        break;
    case OP_PLAY_MEDIA:
        a->ret = MeetBridge_PlayMediaLocal((uint32_t)a->i1, (int)a->i2, (int)a->i3);
        break;
    case OP_CLOSE_PLAY_RES:
        a->ret = MeetBridge_ClosePlayRes((int)a->i1);
        break;
    case OP_SIGN_IN:
        a->ret = MeetBridge_SignIn((int)a->i1);
        break;
    case OP_ENTER_MEETING:
        a->ret = MeetBridge_EnterMeeting();
        break;
    case OP_SET_FACE_STATE:
        a->ret = MeetBridge_SetFaceState((int)a->i1);
        break;
    case OP_MODIFY_MEMBER_ROLE:
        a->ret = MeetBridge_ModifyMemberRole((uint32_t)a->i1, (int)a->i2);
        break;
    case OP_PLAY_STREAM:
        a->ret = MeetBridge_PlayStreamOnSelf((uint32_t)a->i1, (int)a->i2,
                                             (int)a->i3, (int)a->i4);
        break;
    case OP_STOP_PLAY_STREAM:
        a->ret = MeetBridge_StopPlayStream();
        break;
    case OP_QUERY_MEETING_DEVICES:
        a->json = MeetBridge_QueryMeetingDevices();
        a->ret = a->json ? 0 : -1;
        break;
    case OP_SCREEN_SHARE_START:
        a->ret = MeetBridge_StartScreenShare();
        break;
    case OP_SCREEN_SHARE_STOP:
        a->ret = MeetBridge_StopScreenShare();
        break;
    default:
        a->ret = -1;
        break;
    }
}

static void meet_complete(napi_env env, napi_status status, void *data)
{
    MeetAsync *a = (MeetAsync *)data;
    napi_value result;

    (void)status;
    if (a->op == OP_QUERY_MEETINGS || a->op == OP_QUERY_ROOMS ||
        a->op == OP_QUERY_MEDIA || a->op == OP_QUERY_MEETING_DEVICES)
        result = ret_string(env, a->json);
    else
        result = ret_int(env, a->ret);

    napi_resolve_deferred(env, a->deferred, result);
    napi_delete_async_work(env, a->work);
    free(a->json);
    free(a);
}

static napi_value start_async(napi_env env, MeetAsync *a, const char *name)
{
    napi_value promise;
    napi_value resource = ret_string(env, name);

    if (napi_create_promise(env, &a->deferred, &promise) != napi_ok)
        return ret_int(env, -1);
    if (napi_create_async_work(env, NULL, resource, meet_execute, meet_complete,
                               a, &a->work) != napi_ok) {
        free(a);
        return ret_int(env, -1);
    }
    napi_queue_async_work(env, a->work);
    return promise;
}

static napi_value PrepareConfig(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value argv[4];
    char tpl[1024], out[1024], ip[128];
    int32_t port = 2160;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 4)
        return ret_int(env, -1);
    if (arg_string(env, argv[0], tpl, sizeof(tpl)) != 0 ||
        arg_string(env, argv[1], out, sizeof(out)) != 0 ||
        arg_string(env, argv[2], ip, sizeof(ip)) != 0 ||
        arg_int(env, argv[3], &port) != 0)
        return ret_int(env, -1);
    return ret_int(env, MeetBridge_PrepareConfig(tpl, out, ip, (int)port));
}

static napi_value Init(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_string(env, argv[0], a->s1, sizeof(a->s1)) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->op = OP_INIT;
    return start_async(env, a, "meetInit");
}

static napi_value Login(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value argv[2];
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 2 ||
        arg_string(env, argv[0], a->s1, sizeof(a->s1)) != 0 ||
        arg_string(env, argv[1], a->s2, sizeof(a->s2)) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->op = OP_LOGIN;
    return start_async(env, a, "meetLogin");
}

static napi_value QueryMeetings(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_QUERY_MEETINGS;
    return start_async(env, a, "meetQueryMeetings");
}

static napi_value QueryRooms(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_QUERY_ROOMS;
    return start_async(env, a, "meetQueryRooms");
}

static napi_value AddMeeting(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value argv[4];
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 4 ||
        arg_string(env, argv[0], a->s1, sizeof(a->s1)) != 0 ||
        arg_int(env, argv[1], &a->i1) != 0 ||
        arg_i64(env, argv[2], &a->i2) != 0 ||
        arg_i64(env, argv[3], &a->i3) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->op = OP_ADD_MEETING;
    return start_async(env, a, "meetAddMeeting");
}

static napi_value DeleteMeeting(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_int(env, argv[0], &a->i1) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->op = OP_DELETE_MEETING;
    return start_async(env, a, "meetDeleteMeeting");
}

static napi_value SetSurfaceId(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value argv[2];
    int32_t res = 0;
    char sid[64] = { 0 };

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 2 ||
        arg_int(env, argv[0], &res) != 0 ||
        arg_string(env, argv[1], sid, sizeof(sid)) != 0)
        return ret_int(env, -1);
    return ret_int(env, MeetBridge_SetSurfaceId((int)res,
                                                (uint64_t)strtoull(sid, NULL, 10)));
}

static napi_value InitPlayRes(napi_env env, napi_callback_info info)
{
    size_t argc = 5;
    napi_value argv[5];
    int32_t res = 0, x = 0, y = 0, w = 1920, h = 1080;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 5 ||
        arg_int(env, argv[0], &res) != 0 ||
        arg_int(env, argv[1], &x) != 0 ||
        arg_int(env, argv[2], &y) != 0 ||
        arg_int(env, argv[3], &w) != 0 ||
        arg_int(env, argv[4], &h) != 0)
        return ret_int(env, -1);
    return ret_int(env, MeetBridge_InitPlayRes((int)res, (int)x, (int)y,
                                               (int)w, (int)h));
}

static napi_value SetCurrentMeeting(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    int32_t meetingId = 0;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_int(env, argv[0], &meetingId) != 0)
        return ret_int(env, -1);
    return ret_int(env, MeetBridge_SetCurrentMeeting((uint32_t)meetingId));
}

static napi_value QueryMedia(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_QUERY_MEDIA;
    return start_async(env, a, "meetQueryMedia");
}

static napi_value PlayMedia(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value argv[3];
    int32_t res = 0, loop = 0;
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 3 ||
        arg_int(env, argv[0], &a->i1) != 0 ||
        arg_int(env, argv[1], &res) != 0 ||
        arg_int(env, argv[2], &loop) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->i2 = res;
    a->i3 = loop;
    a->op = OP_PLAY_MEDIA;
    return start_async(env, a, "meetPlayMedia");
}

static napi_value ClosePlayRes(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_int(env, argv[0], &a->i1) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->op = OP_CLOSE_PLAY_RES;
    return start_async(env, a, "meetClosePlayRes");
}

static napi_value SignIn(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    int32_t type = 0;
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_int(env, argv[0], &type) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->i1 = type;
    a->op = OP_SIGN_IN;
    return start_async(env, a, "meetSignIn");
}

static napi_value EnterMeeting(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_ENTER_MEETING;
    return start_async(env, a, "meetEnterMeeting");
}

static napi_value SetFaceState(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    int32_t state = 1;
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1 ||
        arg_int(env, argv[0], &state) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->i1 = state;
    a->op = OP_SET_FACE_STATE;
    return start_async(env, a, "meetSetFaceState");
}

static napi_value ModifyMemberRole(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value argv[2];
    int32_t memberId = 0, role = 1;
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 2 ||
        arg_int(env, argv[0], &memberId) != 0 ||
        arg_int(env, argv[1], &role) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    a->i1 = memberId;
    a->i2 = role;
    a->op = OP_MODIFY_MEMBER_ROLE;
    return start_async(env, a, "meetModifyMemberRole");
}

static napi_value PlayStream(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value argv[4];
    int32_t dev = 0, sub = 2, res = 0, flag = 0;
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    if (!a)
        return ret_int(env, -1);
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc < 1 ||
        arg_int(env, argv[0], &dev) != 0) {
        free(a);
        return ret_int(env, -1);
    }
    if (argc > 1 && arg_int(env, argv[1], &sub) != 0)
        sub = 2;
    if (argc > 2 && arg_int(env, argv[2], &res) != 0)
        res = 0;
    if (argc > 3 && arg_int(env, argv[3], &flag) != 0)
        flag = 0;
    a->i1 = dev;
    a->i2 = sub;
    a->i3 = res;
    a->i4 = flag;
    a->op = OP_PLAY_STREAM;
    return start_async(env, a, "meetPlayStream");
}

static napi_value StopPlayStream(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_STOP_PLAY_STREAM;
    return start_async(env, a, "meetStopPlayStream");
}

static napi_value QueryMeetingDevices(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_QUERY_MEETING_DEVICES;
    return start_async(env, a, "meetQueryMeetingDevices");
}

static napi_value StartScreenShare(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_SCREEN_SHARE_START;
    return start_async(env, a, "meetStartScreenShare");
}

static napi_value StopScreenShare(napi_env env, napi_callback_info info)
{
    MeetAsync *a = (MeetAsync *)calloc(1, sizeof(*a));
    (void)info;
    if (!a)
        return ret_int(env, -1);
    a->op = OP_SCREEN_SHARE_STOP;
    return start_async(env, a, "meetStopScreenShare");
}

static napi_value GetStreamState(napi_env env, napi_callback_info info)
{
    char *json;
    napi_value out;
    (void)info;
    json = MeetBridge_GetStreamState();
    out = ret_string(env, json);
    free(json);
    return out;
}

static napi_value IsScreenSharing(napi_env env, napi_callback_info info)
{
    (void)info;
    return ret_int(env, MeetBridge_IsScreenSharing());
}

static napi_value GetPlayProgress(napi_env env, napi_callback_info info)
{
    char *json;
    napi_value out;
    (void)info;
    json = MeetBridge_GetPlayProgress();
    out = ret_string(env, json);
    free(json);
    return out;
}

static napi_value GetSelfMemberId(napi_env env, napi_callback_info info)
{
    (void)info;
    return ret_int(env, MeetBridge_GetSelfMemberId());
}

static napi_value IsSignedIn(napi_env env, napi_callback_info info)
{
    (void)info;
    return ret_int(env, MeetBridge_IsSignedIn());
}

static napi_value Exit(napi_env env, napi_callback_info info)
{
    (void)info;
    MeetBridge_Exit();
    return ret_int(env, 0);
}

static napi_value IsReady(napi_env env, napi_callback_info info)
{
    (void)info;
    return ret_int(env, MeetBridge_IsReady());
}

static napi_value IsLoggedIn(napi_env env, napi_callback_info info)
{
    (void)info;
    return ret_int(env, MeetBridge_IsLoggedIn());
}

static napi_value GetEvents(napi_env env, napi_callback_info info)
{
    char *json;
    napi_value out;
    (void)info;
    json = MeetBridge_GetEvents();
    out = ret_string(env, json);
    free(json);
    return out;
}

static napi_value ModuleInit(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "prepareConfig", NULL, PrepareConfig, NULL, NULL, NULL, napi_default, NULL },
        { "init",          NULL, Init,          NULL, NULL, NULL, napi_default, NULL },
        { "exit",          NULL, Exit,          NULL, NULL, NULL, napi_default, NULL },
        { "isReady",       NULL, IsReady,       NULL, NULL, NULL, napi_default, NULL },
        { "isLoggedIn",    NULL, IsLoggedIn,    NULL, NULL, NULL, napi_default, NULL },
        { "login",         NULL, Login,         NULL, NULL, NULL, napi_default, NULL },
        { "queryMeetings", NULL, QueryMeetings, NULL, NULL, NULL, napi_default, NULL },
        { "queryRooms",    NULL, QueryRooms,    NULL, NULL, NULL, napi_default, NULL },
        { "addMeeting",    NULL, AddMeeting,    NULL, NULL, NULL, napi_default, NULL },
        { "deleteMeeting", NULL, DeleteMeeting, NULL, NULL, NULL, napi_default, NULL },
        { "getEvents",     NULL, GetEvents,     NULL, NULL, NULL, napi_default, NULL },
        { "setSurfaceId",  NULL, SetSurfaceId,  NULL, NULL, NULL, napi_default, NULL },
        { "initPlayRes",   NULL, InitPlayRes,   NULL, NULL, NULL, napi_default, NULL },
        { "setCurrentMeeting", NULL, SetCurrentMeeting, NULL, NULL, NULL, napi_default, NULL },
        { "queryMedia",    NULL, QueryMedia,    NULL, NULL, NULL, napi_default, NULL },
        { "playMedia",     NULL, PlayMedia,     NULL, NULL, NULL, napi_default, NULL },
        { "closePlayRes",  NULL, ClosePlayRes,  NULL, NULL, NULL, napi_default, NULL },
        { "signIn",        NULL, SignIn,        NULL, NULL, NULL, napi_default, NULL },
        { "enterMeeting",  NULL, EnterMeeting,  NULL, NULL, NULL, napi_default, NULL },
        { "setFaceState",  NULL, SetFaceState,  NULL, NULL, NULL, napi_default, NULL },
    { "modifyMemberRole", NULL, ModifyMemberRole, NULL, NULL, NULL, napi_default, NULL },
        { "playStream",    NULL, PlayStream,    NULL, NULL, NULL, napi_default, NULL },
        { "stopPlayStream", NULL, StopPlayStream, NULL, NULL, NULL, napi_default, NULL },
        { "queryMeetingDevices", NULL, QueryMeetingDevices, NULL, NULL, NULL, napi_default, NULL },
        { "startScreenShare", NULL, StartScreenShare, NULL, NULL, NULL, napi_default, NULL },
        { "stopScreenShare",  NULL, StopScreenShare,  NULL, NULL, NULL, napi_default, NULL },
        { "getStreamState", NULL, GetStreamState, NULL, NULL, NULL, napi_default, NULL },
        { "isScreenSharing", NULL, IsScreenSharing, NULL, NULL, NULL, napi_default, NULL },
        { "getPlayProgress", NULL, GetPlayProgress, NULL, NULL, NULL, napi_default, NULL },
        { "getSelfMemberId", NULL, GetSelfMemberId, NULL, NULL, NULL, napi_default, NULL },
        { "isSignedIn",    NULL, IsSignedIn,    NULL, NULL, NULL, napi_default, NULL },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

static napi_module g_meetModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = NULL,
    .nm_register_func = ModuleInit,
    .nm_modname = MEET_NAPI_MODNAME,
    .nm_priv = NULL,
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterMeetModule(void)
{
    napi_module_register(&g_meetModule);
}
