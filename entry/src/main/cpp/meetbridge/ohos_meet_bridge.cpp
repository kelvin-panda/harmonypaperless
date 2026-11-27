/* C++ bridge between the HarmonyOS demo/NAPI and libMeetInterface.
 *
 * The bridge owns the single meetcore_init/Call lifecycle and serialises all
 * calls with a mutex.  It never logs or persists the admin password.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <string>
#include <vector>

#include <hilog/log.h>

#include "meetc/libMeetInterface.h"
#include "meetc/meetinterface_type.h"
#include "meetc/ProtocalData.h"
#include "commdef.h"
#include "cJSON.h"
#include "ohos_meet_bridge.h"
#include "ohos_video_render.h"
#include "ohos_screencapture.h"

#ifndef MEET_LOG_TAG
#define MEET_LOG_TAG "meetcore"
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_inited;
static int g_ready;
static int g_loggedIn;
static int g_loginErr = -1;
static uint32_t g_adminId;
static int g_signinNotify;
static int g_signinStatus = -1; /* MEET_STATUS_* from DBSERVERERROR */
static std::vector<std::string> g_events;

/* Screen sharing / stream receive state. */
static int g_screenSharing;
static int g_captureReady;
static uint32_t g_screenStreamIdx = 2;
static int g_streamActive;
static uint32_t g_streamSrc;
static uint32_t g_streamSub;
static uint32_t g_streamRes;
static uint32_t g_streamTrigger;
static uint32_t g_streamCreated;   /* createdeviceid from the STREAMPLAY notify */
static uint32_t g_streamLastStopRes;

/* Latest media playback progress notification (Type_PlayPosCb). */
static int g_playPosValid;
static uint32_t g_playMediaId;
static int g_playStatus;
static int g_playPer;
static uint32_t g_playSec;
static uint32_t g_playPosRes;

static uint32_t meet_query_context_int(uint32_t propertyId);

/* libMeetInterface helpers used to stop a locally created play trigger
 * (testnet.cpp exports these with C++ linkage). */
int wallet_stoptrigger(int isself, uint32_t *triggerid, int tnum,
                       uint32_t userparam);
int wallet_stoptriggerex(int isself, uint32_t *triggerid, int tnum,
                         uint32_t *pdeviceids, int devnum,
                         uint32_t userparam);

static int tcp_probe(const char *ip, int port)
{
    struct sockaddr_in addr;
    struct timeval tv;
    fd_set wfds;
    int fd, flags, ret, err = 0;
    socklen_t len = sizeof(err);

    if (!ip || port <= 0)
        return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret != 0 && errno != EINPROGRESS) {
        err = errno;
        close(fd);
        return -err;
    }

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    ret = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        err = ret == 0 ? ETIMEDOUT : errno;
        close(fd);
        return -err;
    }
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    close(fd);
    return err ? -err : 0;
}

static void meet_log_cb(int level, const char *msg, void *user)
{
    (void)user;
    OH_LOG_Print(LOG_APP, level == MEETCORE_LOGLEVEL_ERROR ? LOG_ERROR : LOG_INFO,
                0x0C0D, MEET_LOG_TAG, "%{public}s", msg ? msg : "");
}

static int meet_cb(pType_HeaderInfo hdr, void *user)
{
    char line[256];
    (void)user;
    if (!hdr)
        return 0;

    if (hdr->type == TYPE_MEET_INTERFACE_READY &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY) {
        pthread_mutex_lock(&g_lock);
        g_ready = 1;
        pthread_mutex_unlock(&g_lock);
    }

    /* Stream push: HandleStreamPush() gates wallet_playstream() on this
     * return value, so 1 accepts the pushed stream and starts playback. */
    if (hdr->type == TYPE_MEET_INTERFACE_STREAMPUSH &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_StreamPush)) {
        pType_StreamPush p = (pType_StreamPush)hdr;
        snprintf(line, sizeof(line),
                 "STREAMPUSH notify src=0x%x sub=%d mode=%d -> accept",
                 p->deviceid, (int)p->substreamindex, (int)p->encodemode);
        pthread_mutex_lock(&g_lock);
        if (g_events.size() >= 200)
            g_events.erase(g_events.begin());
        g_events.push_back(line);
        pthread_mutex_unlock(&g_lock);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG, "%{public}s", line);
        return 1;
    }

    /* Another device asks us to publish a stream channel.  Accept only for
     * the screen channel we have actually registered (captureReady). */
    if (hdr->type == TYPE_MEET_INTERFACE_REQUESTSTREAMPUSH &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_ReqStreamPush)) {
        pType_ReqStreamPush p = (pType_ReqStreamPush)hdr;
        int accept = (g_captureReady &&
                      (int)p->substreamindex == (int)g_screenStreamIdx) ? 1 : 0;
        snprintf(line, sizeof(line),
                 "REQUESTSTREAMPUSH notify from=0x%x sub=%d accept=%d",
                 p->deviceid, (int)p->substreamindex, accept);
        pthread_mutex_lock(&g_lock);
        if (g_events.size() >= 200)
            g_events.erase(g_events.begin());
        g_events.push_back(line);
        pthread_mutex_unlock(&g_lock);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG, "%{public}s", line);
        return accept;
    }

    /* A stream started playing on this device (push / explicit play). */
    if (hdr->type == TYPE_MEET_INTERFACE_STREAMPLAY &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_MeetStreamPlay)) {
        pType_MeetStreamPlay p = (pType_MeetStreamPlay)hdr;
        pthread_mutex_lock(&g_lock);
        g_streamActive = 1;
        g_streamSrc = p->deviceid;
        g_streamSub = p->subid;
        g_streamRes = (uint32_t)p->res;
        g_streamTrigger = p->triggerid;
        g_streamCreated = p->createdeviceid;
        snprintf(line, sizeof(line),
                 "STREAMPLAY notify src=0x%x sub=%u res=%u trig=0x%x created=0x%x",
                 p->deviceid, p->subid, (unsigned)p->res, p->triggerid,
                 p->createdeviceid);
        if (g_events.size() >= 200)
            g_events.erase(g_events.begin());
        g_events.push_back(line);
        pthread_mutex_unlock(&g_lock);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG, "%{public}s", line);
        return 0;
    }

    if (hdr->type == TYPE_MEET_INTERFACE_STOPPLAY &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_MeetStopPlay)) {
        pType_MeetStopPlay p = (pType_MeetStopPlay)hdr;
        int cleared = 0;
        pthread_mutex_lock(&g_lock);
        g_streamLastStopRes = (uint32_t)p->res;
        if (g_streamActive &&
            (p->triggerid == g_streamTrigger ||
             (g_streamTrigger == 0 && (uint32_t)p->res == g_streamRes))) {
            g_streamActive = 0;
            cleared = 1;
        }
        pthread_mutex_unlock(&g_lock);
        if (cleared) {
            snprintf(line, sizeof(line),
                     "STOPPLAY notify trig=0x%x res=%u (stream stopped)",
                     p->triggerid, (unsigned)p->res);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                         "%{public}s", line);
        }
    }

    /* Media playback position (about once per second). */
    if (hdr->type == TYPE_MEET_INTERFACE_MEDIAPLAYPOSINFO &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_PlayPosCb)) {
        pType_PlayPosCb p = (pType_PlayPosCb)hdr;
        pthread_mutex_lock(&g_lock);
        g_playPosValid = 1;
        g_playMediaId = p->mediaId;
        g_playStatus = (int)p->status;
        g_playPer = (int)p->per;
        g_playSec = p->sec;
        g_playPosRes = p->resId;
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    if (hdr->type == TYPE_MEET_INTERFACE_MEETSIGN &&
        hdr->method == METHOD_MEET_INTERFACE_LOGON) {
        pthread_mutex_lock(&g_lock);
        g_signinNotify = 1;
        pthread_mutex_unlock(&g_lock);
    }

    if (hdr->type == TYPE_MEET_INTERFACE_DBSERVERERROR &&
        hdr->method == METHOD_MEET_INTERFACE_NOTIFY &&
        hdr->totalbufsize >= sizeof(Type_MeetDBServerOperError)) {
        pType_MeetDBServerOperError err = (pType_MeetDBServerOperError)hdr;
        if (err->type == TYPE_MEET_INTERFACE_MEETSIGN) {
            pthread_mutex_lock(&g_lock);
            g_signinStatus = (int)err->status;
            pthread_mutex_unlock(&g_lock);
        }
    }

    if (hdr->type == TYPE_MEET_INTERFACE_ADMIN &&
        hdr->method == METHOD_MEET_INTERFACE_LOGON &&
        hdr->totalbufsize >= sizeof(Type_AdminLogonStatus)) {
        pType_AdminLogonStatus st = (pType_AdminLogonStatus)hdr;
        pthread_mutex_lock(&g_lock);
        g_loginErr = (int)st->err;
        if (st->err == ADMINLOGON_ERR_NONE) {
            g_loggedIn = 1;
            g_adminId = st->adminid;
        }
        pthread_mutex_unlock(&g_lock);
    }

    snprintf(line, sizeof(line), "%s/%s",
             meetcore_GettypeStr(hdr->type) ? meetcore_GettypeStr(hdr->type) : "?",
             meetcore_GetMethodStr(hdr->method) ? meetcore_GetMethodStr(hdr->method) : "?");
    pthread_mutex_lock(&g_lock);
    if (g_events.size() >= 200)
        g_events.erase(g_events.begin());
    g_events.push_back(line);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static std::string trim_eol(const std::string &s);
static bool starts_with(const std::string &s, const char *prefix);

static std::string read_file(const char *path)
{
    FILE *fp;
    long size;
    std::string out;

    if (!path)
        return out;
    fp = fopen(path, "rb");
    if (!fp)
        return out;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size > 0) {
        out.resize((size_t)size);
        if (fread(&out[0], 1, (size_t)size, fp) != (size_t)size)
            out.clear();
    }
    fclose(fp);
    return out;
}

static void read_server(const char *path, char *ip, size_t ipSize, int *port)
{
    std::string text = read_file(path);
    size_t pos = 0;
    bool inArea = false;

    if (ip && ipSize)
        ip[0] = '\0';
    if (port)
        *port = 0;

    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line;
        if (nl == std::string::npos) {
            line = text.substr(pos);
            pos = text.size() + 1;
        } else {
            line = text.substr(pos, nl - pos);
            pos = nl + 1;
        }
        line = trim_eol(line);
        if (!line.empty() && line[0] == '[') {
            inArea = (line == "[areaaddr]");
            continue;
        }
        if (!inArea)
            continue;
        if (starts_with(line, "area0ip=") && ip && ipSize) {
            snprintf(ip, ipSize, "%s", line.c_str() + 8);
        } else if (starts_with(line, "area0port=") && port) {
            *port = atoi(line.c_str() + 10);
        }
    }
}

static bool write_file(const char *path, const std::string &data)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return false;
    if (!data.empty() && fwrite(data.data(), 1, data.size(), fp) != data.size()) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

static std::string trim_eol(const std::string &s)
{
    size_t n = s.size();
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n'))
        n--;
    return s.substr(0, n);
}

static bool starts_with(const std::string &s, const char *prefix)
{
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

/* Rewrites "key=value" only inside [section], preserving all other bytes.
 * Lines are ASCII keys; values here are ASCII, so GBK content is untouched. */
static void set_key(std::string &text, const char *section,
                    const char *key, const std::string &value)
{
    std::string out;
    std::string cur;
    std::string line;
    size_t pos = 0;
    bool inSection = false;

    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            line = text.substr(pos);
            pos = text.size() + 1;
        } else {
            line = text.substr(pos, nl - pos);
            pos = nl + 1;
        }

        cur = trim_eol(line);
        if (!cur.empty() && cur[0] == '[') {
            inSection = (cur == section);
        }

        if (inSection && starts_with(cur, key) &&
            cur.size() > strlen(key) && cur[strlen(key)] == '=') {
            out += key;
            out += '=';
            out += value;
            if (line.size() != cur.size())
                out += '\r';
            if (nl != std::string::npos)
                out += '\n';
        } else {
            out += line;
            if (nl != std::string::npos)
                out += '\n';
        }
    }
    text.swap(out);
}

int MeetBridge_PrepareConfig(const char *templatePath, const char *outPath,
                             const char *serverIp, int serverPort)
{
    std::string text;
    std::string base;
    char port[16];
    size_t slash;

    if (!templatePath || !outPath || !serverIp || !serverIp[0])
        return -1;

    text = read_file(templatePath);
    if (text.empty())
        return -1;

    base = outPath;
    slash = base.find_last_of('/');
    if (slash == std::string::npos)
        return -1;
    base = base.substr(0, slash);
    mkdir(base.c_str(), 0770);
    mkdir((base + "/cache").c_str(), 0770);
    mkdir((base + "/media").c_str(), 0770);

    snprintf(port, sizeof(port), "%d", serverPort > 0 ? serverPort : 2160);
    /* assignid is left as supplied by the template: the tablet's client.dev
     * carries the device identity and the server assigns the self ID. */
    set_key(text, "[Buffer Dir]", "configdir", base + "/cache/");
    set_key(text, "[Buffer Dir]", "mediadir", base + "/media/");
    set_key(text, "[areaaddr]", "area0ip", serverIp);
    set_key(text, "[areaaddr]", "area0port", port);
    set_key(text, "[debug]", "mediatranscode", "0");
    set_key(text, "[debug]", "hwdecode", "0");
    set_key(text, "[debug]", "hwencode", "0");

    /* The OHOS decoder delivers NV12.  Asking libExecProc for NV12 lets the
     * decoder and renderer pass frames through without a per-frame
     * NV12 -> I420 -> NV12 round trip (which capped 1080p playback at
     * roughly half speed on the tablet). */
    if (text.find("[nosdl]") == std::string::npos)
        text += "\r\n[nosdl]\r\nvideopixmal=1\r\n";
    else
        set_key(text, "[nosdl]", "videopixmal", "1");

    return write_file(outPath, text) ? 0 : -1;
}

int MeetBridge_Init(const char *configPath)
{
    MeetCore_InitParam pa;
    int ret;
    int probe;
    int port = 0;
    char ip[64] = { 0 };

    if (!configPath || !configPath[0])
        return -1;

    read_server(configPath, ip, sizeof(ip), &port);
    probe = tcp_probe(ip, port);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "tcp probe %{public}s:%{public}d -> %{public}d", ip, port, probe);

    pthread_mutex_lock(&g_lock);
    if (g_inited) {
        pthread_mutex_unlock(&g_lock);
        return -2;
    }
    g_ready = 0;
    g_loggedIn = 0;
    g_loginErr = -1;
    g_adminId = 0;
    g_events.clear();
    pthread_mutex_unlock(&g_lock);

    memset(&pa, 0, sizeof(pa));
    pa.pconfigpathname = configPath;
    pa.streamnum = 4;
    pa.programtype = MEET_PROGRAM_TYPE_MEETCLIENT;
    pa.pfunc = meet_cb;
    pa.plogfunc = meet_log_cb;
    /* libExecProc's video buffer pool carries the *compressed* video packets
     * (DECODEOUT_PARAM_DECODE) until the display session consumes them; it no
     * longer holds decoded frames for the meeting path.  The allocator still
     * uses the YUV formula, so 3840x2160 gives 12.4 MB per packet: typical
     * 4K keyframes are 2-6 MB and typical 8K HEVC keyframes (40-100 Mbps,
     * 1 s GOP) are 4-10 MB, so this covers both without extra memory for an
     * 8K decoded frame (which is never stored here).  Extreme 8K bitrates
     * can raise it from client.ini:
     * [debug] userset=1,outwidth=<w>,outheight=<h>,bufnum=<n>, packet limit
     * = w*h*1.5. */
    pa.screenwidth = 3840;
    pa.screenheight = 2160;
    pa.blogtofile = 0;

    ret = meetcore_init(&pa);
    pthread_mutex_lock(&g_lock);
    g_inited = (ret == 0);
    pthread_mutex_unlock(&g_lock);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "meetcore_init ret=%{public}d", ret);

    if (ret == 0) {
        int r = OhosRenderer_InstallDisplayCallbacks();
        uint32_t idx;
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                     "OhosRenderer_InstallDisplayCallbacks=%{public}d", r);

        /* Register the screen-capture channel (in-meeting speech stream
         * channel 2).  Capture stays idle until a viewer requests it. */
        idx = meet_query_context_int(MEETCONTEXT_PROPERTY_SCREENSTREAMINDEX);
        if (idx > 0 && idx < 32)
            g_screenStreamIdx = idx;
        if (OhosCapture_Init((int)g_screenStreamIdx) == 0) {
            OhosCapture_SetConfig((int)g_screenStreamIdx, 1920, 1080,
                                  4 * 1024 * 1024);
            g_captureReady = 1;
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                         "screen capture channel %{public}d registered",
                         (int)g_screenStreamIdx);
        } else {
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0C0D, MEET_LOG_TAG,
                         "screen capture channel %{public}d init failed",
                         (int)g_screenStreamIdx);
        }
    }
    return ret;
}

int MeetBridge_SetSurfaceId(int res, uint64_t surfaceId)
{
    int r = OhosRenderer_SetSurfaceId(res, surfaceId);
    if (r == 0)
        OhosRenderer_Start(res);
    return r;
}

int MeetBridge_StartRender(int res)
{
    return OhosRenderer_Start(res);
}

int MeetBridge_StopRender(int res)
{
    OhosRenderer_Stop(res);
    return 0;
}

int MeetBridge_InitPlayRes(int res, int x, int y, int w, int h)
{
    Type_MeetInitPlayRes req;
    pType_HeaderInfo pret = NULL;
    int ret;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEDIAPLAY;
    req.hdr.method = METHOD_MEET_INTERFACE_INIT;
    req.res = (int8u)res;
    req.parent = 0; /* _ANDROID path turns this into -1 (headless window) */
    req.x = x;
    req.y = y;
    req.w = w;
    req.h = h;

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "init play res=%{public}d %{public}dx%{public}d ret=%{public}d",
                 res, w, h, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

int MeetBridge_SetCurrentMeeting(uint32_t meetingId)
{
    Type_MeetContextInfo_int32u req;
    pType_HeaderInfo pret = NULL;
    int ret;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEETCONTEXT;
    req.hdr.method = METHOD_MEET_INTERFACE_SETPROPERTY;
    req.propertyid = MEETCONTEXT_PROPERTY_CURMEETINGID;
    req.propertyval = meetingId;

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "set current meeting=0x%{public}x ret=%{public}d",
                 meetingId, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

int MeetBridge_IsReady(void)
{
    int v;
    pthread_mutex_lock(&g_lock);
    v = g_ready;
    pthread_mutex_unlock(&g_lock);
    return v;
}

int MeetBridge_IsLoggedIn(void)
{
    int v;
    pthread_mutex_lock(&g_lock);
    v = g_loggedIn;
    pthread_mutex_unlock(&g_lock);
    return v;
}

int MeetBridge_Login(const char *user, const char *password)
{
    Type_AdminLogon hdr;
    pType_HeaderInfo pret = NULL;
    int ret;
    int i;

    if (!user || !password)
        return -1;

    memset(&hdr, 0, sizeof(hdr));
    hdr.hdr.type = TYPE_MEET_INTERFACE_ADMIN;
    hdr.hdr.method = METHOD_MEET_INTERFACE_LOGON;
    /* ADMINLOGON_FLAG_ISASCILL: the UI supplies plaintext; the core MD5s it.
     * logonmode 0 is the management/admin mode used for accounts like root. */
    hdr.flag = ADMINLOGON_FLAG_ISASCILL;
    hdr.logonmode = ADMINLOGON_MODE_ADMIN;
    strncpy(hdr.adminname, user, sizeof(hdr.adminname) - 1);
    strncpy(hdr.adminpwd, password, sizeof(hdr.adminpwd) - 1);

    pthread_mutex_lock(&g_lock);
    g_loginErr = -1;
    pthread_mutex_unlock(&g_lock);

    ret = meetcore_Call((pType_HeaderInfo)&hdr, &pret, 0);
    if (pret) {
        if (pret->totalbufsize >= sizeof(Type_AdminLogonStatus)) {
            pType_AdminLogonStatus st = (pType_AdminLogonStatus)pret;
            pthread_mutex_lock(&g_lock);
            g_loginErr = (int)st->err;
            if (st->err == ADMINLOGON_ERR_NONE) {
                g_loggedIn = 1;
                g_adminId = st->adminid;
            }
            pthread_mutex_unlock(&g_lock);
        }
        meetcore_free(pret);
    }

    /* The logon result may arrive as a callback after the call returns. */
    for (i = 0; i < 30; i++) {
        int err;
        pthread_mutex_lock(&g_lock);
        err = g_loginErr;
        pthread_mutex_unlock(&g_lock);
        if (err >= 0)
            return err;
        usleep(100 * 1000);
    }
    return ret < 0 ? ret : 0;
}

static void add_events_json(cJSON *root)
{
    cJSON *arr = cJSON_CreateArray();
    size_t i;
    pthread_mutex_lock(&g_lock);
    for (i = 0; i < g_events.size(); i++)
        cJSON_AddItemToArray(arr, cJSON_CreateString(g_events[i].c_str()));
    g_events.clear();
    pthread_mutex_unlock(&g_lock);
    cJSON_AddItemToObject(root, "items", arr);
}

char *MeetBridge_GetEvents(void)
{
    cJSON *root = cJSON_CreateObject();
    char *out;
    if (!root)
        return NULL;
    add_events_json(root);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *MeetBridge_QueryMeetings(void)
{
    Type_HeaderInfo hdr;
    pType_HeaderInfo pret = NULL;
    int ret;
    int i;
    cJSON *root;
    cJSON *arr;
    char *out;

    memset(&hdr, 0, sizeof(hdr));
    hdr.type = TYPE_MEET_INTERFACE_MEETINFO;
    hdr.method = METHOD_MEET_INTERFACE_QUERY;
    /* The first query only triggers SendGetMeetInfo() and returns NOREADY
     * until the area server answers; retry briefly for a one-tap UI. */
    for (i = 0; i < 20; i++) {
        ret = meetcore_Call(&hdr, &pret, 0);
        if (ret != ERROR_MEET_INTERFACE_NOREADY)
            break;
        if (pret) {
            meetcore_free(pret);
            pret = NULL;
        }
        usleep(300 * 1000);
    }

    root = cJSON_CreateObject();
    if (!root) {
        if (pret)
            meetcore_free(pret);
        return NULL;
    }
    cJSON_AddNumberToObject(root, "ret", ret);
    arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "items", arr);

    if (pret) {
        pType_MeetMeetInfo p = (pType_MeetMeetInfo)pret;
        if (p->num < 10000) {
            pItem_MeetMeetInfo item = (pItem_MeetMeetInfo)(p + 1);
            uint32_t i;
            for (i = 0; i < p->num; i++) {
                cJSON *o = cJSON_CreateObject();
                const char *name = item[i].name ? item[i].name : "";
                cJSON_AddNumberToObject(o, "id", item[i].id);
                cJSON_AddStringToObject(o, "name", name);
                cJSON_AddNumberToObject(o, "roomId", item[i].roomId);
                cJSON_AddStringToObject(o, "roomName", item[i].roomname);
                cJSON_AddNumberToObject(o, "secrecy", item[i].secrecy);
                cJSON_AddNumberToObject(o, "startTime", (double)item[i].startTime);
                cJSON_AddNumberToObject(o, "endTime", (double)item[i].endTime);
                cJSON_AddNumberToObject(o, "status", item[i].status);
                cJSON_AddNumberToObject(o, "signinType", item[i].signin_type);
                cJSON_AddNumberToObject(o, "managerId", item[i].managerid);
                cJSON_AddStringToObject(o, "orderName", item[i].ordername);
                cJSON_AddItemToArray(arr, o);
            }
        }
        meetcore_free(pret);
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *MeetBridge_QueryRooms(void)
{
    Type_HeaderInfo hdr;
    pType_HeaderInfo pret = NULL;
    int ret;
    int i;
    cJSON *root;
    cJSON *arr;
    char *out;

    memset(&hdr, 0, sizeof(hdr));
    hdr.type = TYPE_MEET_INTERFACE_ROOM;
    hdr.method = METHOD_MEET_INTERFACE_QUERY;
    for (i = 0; i < 20; i++) {
        ret = meetcore_Call(&hdr, &pret, 0);
        if (ret != ERROR_MEET_INTERFACE_NOREADY)
            break;
        if (pret) {
            meetcore_free(pret);
            pret = NULL;
        }
        usleep(300 * 1000);
    }

    root = cJSON_CreateObject();
    if (!root) {
        if (pret)
            meetcore_free(pret);
        return NULL;
    }
    cJSON_AddNumberToObject(root, "ret", ret);
    arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "items", arr);

    if (pret) {
        pType_MeetRoomDetailInfo p = (pType_MeetRoomDetailInfo)pret;
        if (p->num < 10000) {
            pItem_MeetRoomDetailInfo item = (pItem_MeetRoomDetailInfo)(p + 1);
            uint32_t i;
            for (i = 0; i < p->num; i++) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "id", item[i].roomid);
                cJSON_AddStringToObject(o, "name", item[i].name);
                cJSON_AddStringToObject(o, "addr", item[i].addr);
                cJSON_AddItemToArray(arr, o);
            }
        }
        meetcore_free(pret);
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int MeetBridge_AddMeeting(const char *name, uint32_t roomId,
                          int64_t startTime, int64_t endTime)
{
    char *buf;
    pType_MeetMeetInfo p;
    pItem_MeetMeetInfo item;
    pType_HeaderInfo pret = NULL;
    int len;
    int ret;
    int result = -1;

    if (!name || !name[0] || roomId == 0 || endTime <= startTime)
        return -1;

    len = (int)(sizeof(Type_MeetMeetInfo) + sizeof(Item_MeetMeetInfo));
    buf = (char *)calloc(1, (size_t)len);
    if (!buf)
        return -1;

    p = (pType_MeetMeetInfo)buf;
    p->hdr.type = TYPE_MEET_INTERFACE_MEETINFO;
    p->hdr.method = METHOD_MEET_INTERFACE_ADD;
    p->num = 1;
    item = (pItem_MeetMeetInfo)(p + 1);
    item->id = 0;
    item->name = strdup(name);
    item->roomId = roomId;
    item->startTime = (int64u)startTime;
    item->endTime = (int64u)endTime;
    item->secrecy = type_meeting_normal;
    item->status = MEETING_STATUS_Ready;
    item->managerid = g_adminId;
    item->signin_type = 0;
    strncpy(item->ordername, "HarmonyOS", sizeof(item->ordername) - 1);

    ret = meetcore_Call((pType_HeaderInfo)p, &pret, 0);
    if (ret == ERROR_MEET_INTERFACE_NULL)
        result = 0;
    if (pret)
        meetcore_free(pret);
    free(item->name);
    free(buf);
    return result;
}

int MeetBridge_DeleteMeeting(uint32_t meetingId)
{
    char *buf;
    pType_MeetMeetInfo p;
    pItem_MeetMeetInfo item;
    pType_HeaderInfo pret = NULL;
    int len;
    int ret;
    int result = -1;

    if (meetingId == 0)
        return -1;

    len = (int)(sizeof(Type_MeetMeetInfo) + sizeof(Item_MeetMeetInfo));
    buf = (char *)calloc(1, (size_t)len);
    if (!buf)
        return -1;

    p = (pType_MeetMeetInfo)buf;
    p->hdr.type = TYPE_MEET_INTERFACE_MEETINFO;
    p->hdr.method = METHOD_MEET_INTERFACE_DEL;
    p->num = 1;
    item = (pItem_MeetMeetInfo)(p + 1);
    item->id = meetingId;

    ret = meetcore_Call((pType_HeaderInfo)p, &pret, 0);
    if (ret == ERROR_MEET_INTERFACE_NULL)
        result = 0;
    if (pret)
        meetcore_free(pret);
    free(buf);
    return result;
}

static int meet_call_retry(pType_HeaderInfo hdr, pType_HeaderInfo *pret)
{
    int ret = 0;
    int i;

    for (i = 0; i < 20; i++) {
        ret = meetcore_Call(hdr, pret, 0);
        if (ret != ERROR_MEET_INTERFACE_NOREADY)
            break;
        if (*pret) {
            meetcore_free(*pret);
            *pret = NULL;
        }
        usleep(300 * 1000);
    }
    return ret;
}

char *MeetBridge_QueryMediaFiles(void)
{
    typedef struct {
        uint32_t id;
        char name[128];
    } DirEntry;
    DirEntry dirs[32];
    int dirCount = 0;
    int i;
    int ret = ERROR_MEET_INTERFACE_NOREADY;
    cJSON *root;
    cJSON *dirArr;
    cJSON *itemArr;

    {
        Type_MeetDirDetailInfo req;
        pType_HeaderInfo pret = NULL;

        memset(&req, 0, sizeof(req));
        req.hdr.type = TYPE_MEET_INTERFACE_MEETDIRECTORY;
        req.hdr.method = METHOD_MEET_INTERFACE_QUERY;
        ret = meet_call_retry((pType_HeaderInfo)&req, &pret);
        if (pret) {
            pType_MeetDirDetailInfo p = (pType_MeetDirDetailInfo)pret;
            if (p->num < 1000) {
                pItem_MeetDirDetailInfo item = (pItem_MeetDirDetailInfo)(p + 1);
                for (i = 0; i < (int)p->num && dirCount < 32; i++) {
                    dirs[dirCount].id = item[i].id;
                    snprintf(dirs[dirCount].name, sizeof(dirs[dirCount].name),
                             "%s", item[i].name ? item[i].name : "");
                    dirCount++;
                }
            }
            meetcore_free(pret);
        }
    }

    if (dirCount == 0) {
        dirs[0].id = 0;
        snprintf(dirs[0].name, sizeof(dirs[0].name), "%s", "root");
        dirCount = 1;
    }

    root = cJSON_CreateObject();
    if (!root)
        return NULL;
    cJSON_AddNumberToObject(root, "ret", ret);
    dirArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "dirs", dirArr);
    itemArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "items", itemArr);

    for (i = 0; i < dirCount; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", dirs[i].id);
        cJSON_AddStringToObject(o, "name", dirs[i].name);
        cJSON_AddItemToArray(dirArr, o);
    }

    for (i = 0; i < dirCount; i++) {
        Type_MeetDirFileDetailInfo req;
        pType_HeaderInfo pret = NULL;
        int fret;

        memset(&req, 0, sizeof(req));
        req.hdr.type = TYPE_MEET_INTERFACE_MEETDIRECTORYFILE;
        req.hdr.method = METHOD_MEET_INTERFACE_QUERY;
        req.dirid = dirs[i].id;
        fret = meet_call_retry((pType_HeaderInfo)&req, &pret);
        if (fret == ERROR_MEET_INTERFACE_NOREADY)
            continue;
        if (pret) {
            pType_MeetDirFileDetailInfo p = (pType_MeetDirFileDetailInfo)pret;
            if (p->num < 1000) {
                pItem_MeetDirFileDetailInfo item =
                    (pItem_MeetDirFileDetailInfo)(p + 1);
                uint32_t n;
                for (n = 0; n < p->num && cJSON_GetArraySize(itemArr) < 500; n++) {
                    cJSON *o = cJSON_CreateObject();
                    cJSON_AddNumberToObject(o, "dirId", dirs[i].id);
                    cJSON_AddNumberToObject(o, "id", item[n].mediaid);
                    cJSON_AddStringToObject(o, "name",
                                            item[n].name ? item[n].name : "");
                    cJSON_AddNumberToObject(o, "size", (double)item[n].size);
                    cJSON_AddNumberToObject(o, "filepos", item[n].filepos);
                    cJSON_AddItemToArray(itemArr, o);
                }
            }
            meetcore_free(pret);
        }
    }

    {
        char *out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return out;
    }
}

int MeetBridge_PlayMediaLocal(uint32_t mediaId, int res, int loop)
{
    Type_MeetDoMediaPlay req;
    pType_HeaderInfo pret = NULL;
    int ret;

    if (mediaId == 0)
        return -1;
    if (res < 0 || res >= MAX_RES_NUM)
        res = 0;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEDIAPLAY;
    req.hdr.method = METHOD_MEET_INTERFACE_START;
    req.playflag = loop ? MEDIA_PLAYFLAG_LOOP : 0;
    req.pos = 0;
    req.mediaid = mediaId;
    req.triggeruserval = 0;
    req.resnum = 1;
    req.res[0] = (int8u)res;
    req.devnum = 0; /* the SDK expands this to the control device list (self) */

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "play media 0x%{public}x res=%{public}d loop=%{public}d ret=%{public}d",
                 mediaId, res, loop, ret);
    if (ret == ERROR_MEET_INTERFACE_NULL || ret == 0) {
        /* The previous media's progress must not be shown for this one. */
        pthread_mutex_lock(&g_lock);
        g_playPosValid = 0;
        g_playMediaId = mediaId;
        g_playStatus = 0;
        g_playPer = 0;
        g_playSec = 0;
        g_playPosRes = (uint32_t)res;
        pthread_mutex_unlock(&g_lock);
    }
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

int MeetBridge_ClosePlayRes(int res)
{
    Type_MeetDoStopResWork req;
    pType_HeaderInfo pret = NULL;
    int ret;

    if (res < 0 || res >= MAX_RES_NUM)
        res = 0;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEDIAPLAY;
    req.hdr.method = METHOD_MEET_INTERFACE_CLOSE;
    req.playflag = 0;
    req.avalableres = 1;
    req.res[0] = (int8u)res;
    req.devnum = 0;

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "close play res=%{public}d ret=%{public}d", res, ret);
    if (ret == ERROR_MEET_INTERFACE_NULL || ret == 0) {
        pthread_mutex_lock(&g_lock);
        g_playPosValid = 0;
        g_playStatus = 0;
        g_playPer = 0;
        g_playSec = 0;
        pthread_mutex_unlock(&g_lock);
    }
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

char *MeetBridge_GetPlayProgress(void)
{
    cJSON *root = cJSON_CreateObject();
    char *out;

    if (!root)
        return NULL;
    pthread_mutex_lock(&g_lock);
    cJSON_AddNumberToObject(root, "valid", g_playPosValid);
    cJSON_AddNumberToObject(root, "mediaId", (double)g_playMediaId);
    cJSON_AddNumberToObject(root, "status", g_playStatus);
    cJSON_AddNumberToObject(root, "per", g_playPer);
    cJSON_AddNumberToObject(root, "sec", (double)g_playSec);
    cJSON_AddNumberToObject(root, "res", (double)g_playPosRes);
    pthread_mutex_unlock(&g_lock);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static uint32_t meet_query_context_int(uint32_t propertyId)
{
    Type_MeetContextInfo_int32u req;
    pType_HeaderInfo pret = NULL;
    int ret;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEETCONTEXT;
    req.hdr.method = METHOD_MEET_INTERFACE_QUERYPROPERTY;
    req.propertyid = propertyId;
    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    if (ret != ERROR_MEET_INTERFACE_NULL)
        return 0;
    return req.propertyval;
}

int MeetBridge_GetSelfMemberId(void)
{
    return (int)meet_query_context_int(MEETCONTEXT_PROPERTY_SELFMEMBERID);
}

int MeetBridge_IsSignedIn(void)
{
    Type_MeetSignInQueryProperty req;
    pType_HeaderInfo pret = NULL;
    int ret;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEETSIGN;
    req.hdr.method = METHOD_MEET_INTERFACE_QUERYPROPERTY;
    req.propertyid = MEETSIGN_PROPERTY_HADSIGNIN;
    req.parameterval = 0; /* the member bound to this device */

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    if (ret != ERROR_MEET_INTERFACE_NULL)
        return 0;
    return (int)req.propertyval;
}

int MeetBridge_SignIn(int signinType)
{
    Type_DoMeetSignIno req;
    pType_HeaderInfo pret = NULL;
    int ret;
    int i;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEETSIGN;
    req.hdr.method = METHOD_MEET_INTERFACE_ADD;
    req.memberid = 0; /* bound member */
    req.signin_type = (int8u)(signinType >= 0 ? signinType : signin_direct);
    req.signdatalen = 0;

    pthread_mutex_lock(&g_lock);
    g_signinNotify = 0;
    g_signinStatus = -1;
    pthread_mutex_unlock(&g_lock);

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "signin type=%{public}d ret=%{public}d member=%{public}d",
                 (int)req.signin_type, ret, MeetBridge_GetSelfMemberId());

    if (ret != ERROR_MEET_INTERFACE_NULL)
        return ret < 0 ? ret : -1;

    /* The ADD is fire-and-forget; the server answers with a DB status
     * notify (3 = MEET_STATUS_DONE) and/or a MEETSIGN/LOGON callback. */
    for (i = 0; i < 20; i++) {
        int notify;
        int status;

        usleep(250 * 1000);
        pthread_mutex_lock(&g_lock);
        notify = g_signinNotify;
        status = g_signinStatus;
        pthread_mutex_unlock(&g_lock);

        if (status >= 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                         "signin server status=%{public}d", status);
            return status == 3 ? 0 : -3;
        }
        if (notify) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                         "signin notify received");
            return 0;
        }
        if (MeetBridge_IsSignedIn() == 1) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                         "signin confirmed by query");
            return 0;
        }
    }
    return 1;
}

int MeetBridge_EnterMeeting(void)
{
    uint32_t selfId = meet_query_context_int(MEETCONTEXT_PROPERTY_SELFID);
    int len = (int)(sizeof(Type_MeetDoEnterMeet) + sizeof(uint32_t));
    char *buf;
    pType_MeetDoEnterMeet p;
    pType_HeaderInfo pret = NULL;
    int ret;

    if (selfId == 0)
        return -1;

    buf = (char *)calloc(1, (size_t)len);
    if (!buf)
        return -1;
    p = (pType_MeetDoEnterMeet)buf;
    p->hdr.type = TYPE_MEET_INTERFACE_DEVICEOPER;
    p->hdr.method = METHOD_MEET_INTERFACE_ENTER;
    p->devnum = 1;
    ((uint32_t *)(p + 1))[0] = selfId;

    ret = meetcore_Call((pType_HeaderInfo)p, &pret, 0);
    if (pret)
        meetcore_free(pret);
    free(buf);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "enter meeting self=0x%{public}x ret=%{public}d", selfId, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

int MeetBridge_SetFaceState(int state)
{
    Type_MeetContextInfo_int32u req;
    pType_HeaderInfo pret = NULL;
    int ret;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_MEETCONTEXT;
    req.hdr.method = METHOD_MEET_INTERFACE_SETPROPERTY;
    req.propertyid = MEETCONTEXT_PROPERTY_ROLE;
    req.propertyval = (int32u)state;

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "set face state=%{public}d ret=%{public}d", state, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

int MeetBridge_ModifyMemberRole(uint32_t memberId, int role)
{
    Type_MeetSeatDetailInfo *req;
    pType_HeaderInfo pret = NULL;
    size_t size;
    int ret;

    if (memberId == 0)
        return -1;

    size = sizeof(Type_MeetSeatDetailInfo) + sizeof(Item_MeetSeatDetailInfo);
    req = (Type_MeetSeatDetailInfo *)calloc(1, size);
    if (!req)
        return -1;

    req->hdr.type = TYPE_MEET_INTERFACE_MEETSEAT;
    req->hdr.method = METHOD_MEET_INTERFACE_MODIFY;
    req->num = 1;
    {
        Item_MeetSeatDetailInfo *item =
            (Item_MeetSeatDetailInfo *)(req + 1);
        item->nameId = memberId;
        item->seatid = 0; /* only change role, not seat binding */
        item->role = role;
    }

    ret = meetcore_Call((pType_HeaderInfo)req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    free(req);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "modify member role member=%{public}u role=%{public}d ret=%{public}d",
                 memberId, role, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

/* ------------------------------------------------------------------ */
/* Screen share / stream receive                                       */
/* ------------------------------------------------------------------ */

/* Devices that are online and in the current meeting (meetingid match),
 * excluding this device and server pseudo-devices. */
static int query_meeting_device_ids(std::vector<uint32_t> &ids,
                                    std::vector<std::string> *names)
{
    Type_DeviceComplexQuery req;
    pType_HeaderInfo pret = NULL;
    uint32_t selfId = meet_query_context_int(MEETCONTEXT_PROPERTY_SELFID);
    uint32_t meetId = meet_query_context_int(MEETCONTEXT_PROPERTY_CURMEETINGID);
    int ret = ERROR_MEET_INTERFACE_NOREADY;
    int i, tries;

    ids.clear();
    if (names)
        names->clear();
    if (meetId == 0)
        return 0;

    for (tries = 0; tries < 20; tries++) {
        memset(&req, 0, sizeof(req));
        req.hdr.type = TYPE_MEET_INTERFACE_DEVICEINFO;
        req.hdr.method = METHOD_MEET_INTERFACE_COMPLEXQUERY;
        req.queryflag = DEVICE_COMPLEXQUERY_DEVICESTAT |
                        DEVICE_COMPLEXQUERY_DEVICEMEET;
        req.netstate = DEVICE_ONLINE;
        req.meetingid = meetId;

        if (pret) {
            meetcore_free(pret);
            pret = NULL;
        }
        ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
        if (ret != ERROR_MEET_INTERFACE_NOREADY)
            break;
        usleep(300 * 1000);
    }

    if (ret > 0 && pret) {
        pType_DeviceDetailInfo p = (pType_DeviceDetailInfo)pret;
        pItem_DeviceDetailInfo item = (pItem_DeviceDetailInfo)(p + 1);
        int n = (int)p->devnum;
        if (n > 1000)
            n = 0;
        for (i = 0; i < n; i++) {
            uint32_t id = item[i].devcieid;
            if (id == 0 || id == selfId || id >= SERVER_ID_STREAM)
                continue;
            if (item[i].netstate != DEVICE_ONLINE ||
                item[i].meetingid != meetId)
                continue;
            ids.push_back(id);
            if (names)
                names->push_back(std::string(item[i].devname));
        }
    }
    if (pret)
        meetcore_free(pret);
    return (int)ids.size();
}

char *MeetBridge_QueryMeetingDevices(void)
{
    std::vector<uint32_t> ids;
    std::vector<std::string> names;
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    char *out;
    int i, n;

    if (!root)
        return NULL;
    n = query_meeting_device_ids(ids, &names);
    cJSON_AddNumberToObject(root, "ret", n);
    arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "items", arr);
    for (i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", (double)ids[i]);
        cJSON_AddStringToObject(o, "name", names[i].c_str());
        cJSON_AddItemToArray(arr, o);
    }
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int MeetBridge_PlayStreamOnSelf(uint32_t deviceId, int subId, int res,
                                int playflag)
{
    Type_MeetDoStreamPlay req;
    pType_HeaderInfo pret = NULL;
    int ret;
    uint32_t selfId = meet_query_context_int(MEETCONTEXT_PROPERTY_SELFID);

    if (selfId == 0 || deviceId == 0)
        return -1;
    if (subId < 0)
        subId = (int)g_screenStreamIdx;
    if (res < 0 || res >= MAX_RES_NUM)
        res = 0;

    memset(&req, 0, sizeof(req));
    req.hdr.type = TYPE_MEET_INTERFACE_STREAMPLAY;
    req.hdr.method = METHOD_MEET_INTERFACE_START;
    req.encodemode = 0;
    req.deviceid = deviceId;
    req.subid = (uint32_t)subId;
    req.playflag = (uint32_t)playflag;
    req.triggeruserval = (subId == (int)g_screenStreamIdx)
                             ? EXCEC_USERDEF_FLAG_SCREENSHARE : 0;
    req.resnum = 1;
    req.res[0] = (int8u)res;
    req.devnum = 0;   /* SDK expands to the control device list (self) */

    ret = meetcore_Call((pType_HeaderInfo)&req, &pret, 0);
    if (pret)
        meetcore_free(pret);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                 "play stream src=0x%{public}x sub=%{public}d res=%{public}d "
                 "ret=%{public}d", deviceId, subId, res, ret);
    return ret == ERROR_MEET_INTERFACE_NULL ? 0 : ret;
}

char *MeetBridge_GetStreamState(void)
{
    cJSON *root = cJSON_CreateObject();
    char *out;

    if (!root)
        return NULL;
    pthread_mutex_lock(&g_lock);
    cJSON_AddNumberToObject(root, "active", g_streamActive);
    cJSON_AddNumberToObject(root, "src", (double)g_streamSrc);
    cJSON_AddNumberToObject(root, "sub", (double)g_streamSub);
    cJSON_AddNumberToObject(root, "res", (double)g_streamRes);
    cJSON_AddNumberToObject(root, "trigger", (double)g_streamTrigger);
    cJSON_AddNumberToObject(root, "created", (double)g_streamCreated);
    cJSON_AddNumberToObject(root, "lastStopRes", (double)g_streamLastStopRes);
    cJSON_AddNumberToObject(root, "sharing", g_screenSharing);
    cJSON_AddNumberToObject(root, "capture", g_captureReady);
    cJSON_AddNumberToObject(root, "screenIdx", (double)g_screenStreamIdx);
    pthread_mutex_unlock(&g_lock);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int MeetBridge_StopPlayStream(void)
{
    uint32_t trigger, created, res;
    uint32_t selfId = meet_query_context_int(MEETCONTEXT_PROPERTY_SELFID);
    int ret;

    pthread_mutex_lock(&g_lock);
    trigger = g_streamTrigger;
    created = g_streamCreated;
    res = g_streamRes;
    pthread_mutex_unlock(&g_lock);

    if (trigger != 0) {
        /* The trigger was created on the play device (createdeviceid ==
         * selfid for both push and self-pull); stop it as a local trigger.
         * StopPlay()/CloseRes() use wallet_stoptriggerex(), which does not
         * reliably stop a locally created temp trigger. */
        int isself = (created == 0 || created == selfId) ? 1 : 0;
        if (isself) {
            ret = wallet_stoptrigger(1, &trigger, 1, 0);
        } else {
            uint32_t devid = selfId;
            ret = wallet_stoptriggerex(0, &trigger, 1, &devid, 1, 0);
        }
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                     "stop play stream trig=0x%{public}x created=0x%{public}x "
                     "isself=%{public}d ret=%{public}d",
                     trigger, created, isself, ret);
        if (ret == 0) {
            pthread_mutex_lock(&g_lock);
            g_streamActive = 0;
            g_streamTrigger = 0;
            g_playPosValid = 0;
            g_playStatus = 0;
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
    }

    /* Fallback: close the play resource directly. */
    ret = MeetBridge_ClosePlayRes((int)res);
    if (ret == 0) {
        pthread_mutex_lock(&g_lock);
        g_streamActive = 0;
        g_streamTrigger = 0;
        pthread_mutex_unlock(&g_lock);
    }
    return ret;
}

int MeetBridge_StartScreenShare(void)
{
    std::vector<uint32_t> ids;
    pType_MeetDoStreamPlay p;
    pType_HeaderInfo pret = NULL;
    char *buf;
    int len, n, ret;
    uint32_t selfId;
    int idx;

    if (!g_inited)
        return -1;

    selfId = meet_query_context_int(MEETCONTEXT_PROPERTY_SELFID);
    if (selfId == 0)
        return -1;

    idx = (int)meet_query_context_int(MEETCONTEXT_PROPERTY_SCREENSTREAMINDEX);
    if (idx > 0 && idx < 32)
        g_screenStreamIdx = (uint32_t)idx;
    else
        idx = (int)g_screenStreamIdx;

    if (OhosCapture_Init(idx) != 0) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0C0D, MEET_LOG_TAG,
                     "screen capture channel %{public}d init failed", idx);
        return -2;
    }
    OhosCapture_SetConfig(idx, 1920, 1080, 4 * 1024 * 1024);
    g_captureReady = 1;

    n = query_meeting_device_ids(ids, NULL);
    if (n <= 0) {
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                     "screen share: no in-meeting devices");
        return ERROR_MEET_INTERFACE_ZERO;
    }

    len = (int)sizeof(Type_MeetDoStreamPlay) + n * (int)sizeof(uint32_t);
    buf = (char *)calloc(1, (size_t)len);
    if (!buf)
        return -1;
    p = (pType_MeetDoStreamPlay)buf;
    p->hdr.totalbufsize = (int32u)len;
    p->hdr.type = TYPE_MEET_INTERFACE_STREAMPLAY;
    p->hdr.method = METHOD_MEET_INTERFACE_START;
    p->encodemode = 0;
    p->deviceid = selfId;
    p->subid = (uint32_t)idx;
    p->playflag = 0;
    p->triggeruserval = EXCEC_USERDEF_FLAG_SCREENSHARE;
    p->resnum = 1;
    p->res[0] = 0;
    p->devnum = n;
    memcpy(p + 1, ids.data(), (size_t)n * sizeof(uint32_t));

    ret = meetcore_Call((pType_HeaderInfo)p, &pret, 0);
    if (pret)
        meetcore_free(pret);
    free(buf);

    if (ret == ERROR_MEET_INTERFACE_NULL) {
        pthread_mutex_lock(&g_lock);
        g_screenSharing = 1;
        pthread_mutex_unlock(&g_lock);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                     "screen share started: %{public}d devices, ch=%{public}d",
                     n, idx);
        return n;
    }
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0C0D, MEET_LOG_TAG,
                 "screen share start failed ret=%{public}d", ret);
    return ret;
}

int MeetBridge_StopScreenShare(void)
{
    std::vector<uint32_t> ids;
    pType_MeetDoStopResWork p;
    pType_HeaderInfo pret = NULL;
    char *buf;
    int len, n, ret = ERROR_MEET_INTERFACE_NULL;

    if (!g_inited)
        return -1;

    n = query_meeting_device_ids(ids, NULL);
    if (n > 0) {
        len = (int)sizeof(Type_MeetDoStopResWork) + n * (int)sizeof(uint32_t);
        buf = (char *)calloc(1, (size_t)len);
        if (!buf)
            return -1;
        p = (pType_MeetDoStopResWork)buf;
        p->hdr.totalbufsize = (int32u)len;
        p->hdr.type = TYPE_MEET_INTERFACE_STOPPLAY;
        p->hdr.method = METHOD_MEET_INTERFACE_CLOSE;
        p->playflag = 0;
        p->triggeruserval = 0;
        p->avalableres = 1;
        p->res[0] = 0;
        p->devnum = n;
        memcpy(p + 1, ids.data(), (size_t)n * sizeof(uint32_t));

        ret = meetcore_Call((pType_HeaderInfo)p, &pret, 0);
        if (pret)
            meetcore_free(pret);
        free(buf);
    }

    if (ret == ERROR_MEET_INTERFACE_NULL) {
        pthread_mutex_lock(&g_lock);
        g_screenSharing = 0;
        pthread_mutex_unlock(&g_lock);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG,
                     "screen share stopped (%{public}d devices)", n);
        return 0;
    }
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0C0D, MEET_LOG_TAG,
                 "screen share stop failed ret=%{public}d", ret);
    return ret;
}

int MeetBridge_IsScreenSharing(void)
{
    int v;
    pthread_mutex_lock(&g_lock);
    v = g_screenSharing;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void MeetBridge_Exit(void)
{
    int inited;
    pthread_mutex_lock(&g_lock);
    inited = g_inited;
    g_inited = 0;
    g_ready = 0;
    g_loggedIn = 0;
    g_screenSharing = 0;
    g_captureReady = 0;
    g_streamActive = 0;
    g_streamSrc = 0;
    g_streamSub = 0;
    g_streamRes = 0;
    g_streamTrigger = 0;
    g_streamCreated = 0;
    g_playPosValid = 0;
    g_playMediaId = 0;
    g_playStatus = 0;
    g_playPer = 0;
    g_playSec = 0;
    g_playPosRes = 0;
    pthread_mutex_unlock(&g_lock);
    if (inited) {
        meetcore_exit();
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0C0D, MEET_LOG_TAG, "meetcore_exit");
    }
    OhosRenderer_Shutdown();
}
