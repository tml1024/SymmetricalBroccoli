/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */

/*

MIT License

Copyright (c) 2020 Tor Lillqvist

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#if !defined APL || !defined IBM || !defined LIN
#error APL, IBM and LIN must be defined in the Makefile or project
#endif

#if APL + IBM + LIN != 1
#error One and only one of APL, IBM or LIN must be defined as 1, the others as 0
#endif

#ifndef XPLM300
#error This must to be compiled against the XPLM300 SDK
#endif

#include <stdarg.h>             // Not <cstdarg> because then Visual C++ doesn't like va_copy()
#include <sys/types.h>

#if APL || LIN
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#if IBM

#include <winsock2.h>

#define CLOSESOCKET(s) closesocket(s)

#else

#include <netinet/in.h>
#include <sys/socket.h>

typedef int SOCKET;
#define CLOSESOCKET(s) close(s)

#endif

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#ifndef DEBUGWINDOW
#define DEBUGWINDOW 0
#endif

#ifndef DEBUGLOGDATA
#define DEBUGLOGDATA 0
#endif

#define MYNAME "SymmetricalBroccoli"
#define MYSIG "fi.iki.tml." MYNAME

#define X 0
#define Y 1
#define Z 2
#define PSI 3
#define THE 4
#define PHI 5

// XYZ are in centimetres, X-Plane wants metres. Additionally, exaggerate movement a bit.
#define X_FACTOR  0.015
#define Y_FACTOR -0.015
#define Z_FACTOR  0.02

// Angles are in degrees. Turning of the head must be exaggerated more so that you can still see the
// screen while turning your simulated head to the side.
#define PSI_FACTOR 2
#define THE_FACTOR 2
#define PHI_FACTOR 1

#if DEBUGWINDOW

static XPLMWindowID debug_window;

#else

static XPLMFlightLoopID flight_loop_id;

#endif

static XPLMDataRef view_type;
static XPLMDataRef head_x, head_y, head_z, head_psi, head_the, head_phi;

static SOCKET sock;
static float current_time;

static bool input_reset = true;

#pragma pack(push, 2)
struct PoseData {
    double d[6]; // x, y, z, yaw, pitch, roll
};
#pragma pack(pop)

#if !IBM

static void strcpy_s(char *dest, size_t dest_size, const char *src)
{
    strncpy(dest, src, dest_size);
}

static char *strerror_s(char *buf, size_t buflen, int errnum)
{
#if APL || (LIN && (_POSIX_C_SOURCE >= 200112L) && ! _GNU_SOURCE)
    // The XSI-compliant strerror_r()
    strerror_r(errnum, buf, buflen);
    return buf;
#else
    // The GNU-specific one
    return strerror_r(errnum, buf, buflen);
#endif
}


#endif

#if IBM

// From https://stackoverflow.com/questions/40159892/using-asprintf-on-windows

static int vscprintf(const char * format, va_list pargs)
{
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}

static int vasprintf(char **strp, const char *fmt, va_list ap)
{
    int len = vscprintf(fmt, ap);
    if (len == -1)
        return -1;
    char *str = static_cast<char *>(malloc((size_t) len + 1));
    if (!str)
        return -1;
    int r = vsnprintf(str, static_cast<size_t>(len) + 1, fmt, ap);
    if (r == -1) {
        free(str);
        return -1;
    }
    *strp = str;
    return r;
}

static int asprintf(char *strp[], const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}

#endif

static void log_string(const char *message)
{
    char *bufp;
    int n = static_cast<int>(floor(current_time));
    asprintf(&bufp, "%0d:%02d:%02d.%03d " MYSIG ": %s\n",
             n/3600, n/60, n%60, static_cast<int>(floor((current_time - n) * 1000)),
             message);

    XPLMDebugString(bufp);
    free(bufp);
}

static void log_stringf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    
    char *bufp;
    vasprintf(&bufp, format, ap);
    log_string(bufp);
    free(bufp);
    va_end(ap);
}

static void error_callback(const char *message)
{
    log_stringf("error callback: %s", message);
}

static void report_syscall_error(const char *syscall)
{
    int saved_errno = errno;
    char buf[100];
    log_stringf("%s  failed: %s", syscall, strerror_s(buf, sizeof(buf), saved_errno));
}

static void report_socket_error(const char *syscall)
{
#if IBM
    int saved_errno = WSAGetLastError();
    char *buf;
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, saved_errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL) == 0) {
        log_stringf("%s failed: %d", syscall, saved_errno);
    } else {
        log_stringf("%s  failed: %s", syscall, buf);
        LocalFree(buf);
    }
#else
    report_syscall_error(syscall);
#endif
}

#if DEBUGWINDOW

#if 0

static int dummy_mouse_handler(XPLMWindowID window_id, int x, int y, int is_down, void *refcon)
{
    return 0;
}

static XPLMCursorStatus dummy_cursor_status_handler(XPLMWindowID window_id, int x, int y, void *refcon)
{
    return xplm_CursorDefault;
}

static int dummy_wheel_handler(XPLMWindowID window_id, int x, int y, int wheel, int clicks, void *refcon)
{
    return 0;
}

#else

#define dummy_mouse_handler NULL
#define dummy_cursor_status_handler NULL
#define dummy_wheel_handler NULL

#endif

static void debug_window_key_handler(XPLMWindowID window_id, char key, XPLMKeyFlags flags, char virtual_key, void *refcon, int losing_focus)
{
    if (static_cast<unsigned char>(virtual_key) == XPLM_VK_PERIOD) {
        log_string("Re-loading plug-ins");
        XPLMReloadPlugins();
    }
}

static void draw_debug_window(const char *string)
{
    // Mandatory: We *must* set the OpenGL state before drawing
    // (we can't make any assumptions about it)
    XPLMSetGraphicsState(0 /* no fog */,
                         0 /* 0 texture units */,
                         0 /* no lighting */,
                         0 /* no alpha testing */,
                         1 /* do alpha blend */,
                         1 /* do depth testing */,
                         0 /* no depth writing */
                         );
    
    int l, t, r, b;
    XPLMGetWindowGeometry(debug_window, &l, &t, &r, &b);
    
    float col_white[] = {1.0, 1.0, 1.0}; // red, green, blue
    
    XPLMDrawString(col_white, l + 10, t - 20, const_cast<char*>(string), NULL, xplmFont_Proportional);
}

#endif

#if DEBUGLOGDATA

static void log_data(const PoseData &data, float pilot_head_x, float pilot_head_y, float pilot_head_z, float pilot_head_psi, float pilot_head_the)
{
    static bool been_here = false;
    static std::ofstream output;

    if (!been_here) {
        time_t now = time(NULL);
        char filename[100];
        strftime(filename, sizeof(filename), "/tmp/%F.%R.%S.log", localtime(&now));
        output = std::ofstream(filename);
        log_stringf("Logging data to %s", filename);
    }
}

#endif

static void average_data(double curr_value[6], const double prev_value[6], const float time_diff)
{
    constexpr double ALPHA = 0.1;

    const double prev_weight = pow(ALPHA, time_diff);

    for (int i = 0; i < 6; i++)
        curr_value[i] = (1 - prev_weight) * curr_value[i] + prev_weight * prev_value[i];
}

static void get_and_handle_data()
{
    PoseData data;
    long n;
    bool got_something = false;

    current_time = XPLMGetElapsedTime();
    
    // Get the most current data packet sent, i.e. read all buffered ones and use only the last.
    while (true) {
        n = recv(sock, reinterpret_cast<char *>(&data), sizeof(data), 0);
        if (n == -1) {
#if IBM
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                break;
#else
            if (errno == EAGAIN)
                break;
#endif
            static int errors = 0;
            if (errors <= 10)
                report_socket_error("recv");
            if (errors == 10)
                log_string("No further recv errors will be reported");
            errors++;
            return;
        } else if (n != sizeof(data)) {
            static int errors = 0;
            if (errors <= 10) {
                log_stringf("Got %ld bytes, expected %d", n, sizeof(data));
            }
            if (errors == 10)
                log_string("No further data amount discrepancies will be reported");
            errors++;
            return;
        } else {
            got_something = true;

#if DEBUGLOGDATA
            log_data(data, 0, 0, 0, 0, 0);
#endif
        }
    }

    if (!got_something)
        return;
        
    // 1026 is the 3D Cockpit
    if (XPLMGetDatai(view_type) != 1026)
        return;

    static PoseData first_data;
    static float initial_pilot_head_pos[6];

    static PoseData prev_data;
    static float prev_time;

    static bool first_time = true;

    // The very first time we get data we save the initial pilot head position
    if (first_time) {
        initial_pilot_head_pos[X] = XPLMGetDataf(head_x);
        initial_pilot_head_pos[Y] = XPLMGetDataf(head_y);
        initial_pilot_head_pos[Z] = XPLMGetDataf(head_z);
        initial_pilot_head_pos[PSI] = XPLMGetDataf(head_psi);
        initial_pilot_head_pos[THE] = XPLMGetDataf(head_the);
        initial_pilot_head_pos[PHI] = XPLMGetDataf(head_phi);

        if (initial_pilot_head_pos[X] == 0 && initial_pilot_head_pos[Y] == 0 && initial_pilot_head_pos[X] == 0 &&
            initial_pilot_head_pos[PSI] == 0 && initial_pilot_head_pos[THE] == 0 && initial_pilot_head_pos[PHI] == 0)
            return;

        log_stringf("Initial head pos: XYZ=(%5.2f,%5.2f,%5.2f) psi=%d the=%d",
                    initial_pilot_head_pos[X], initial_pilot_head_pos[Y], initial_pilot_head_pos[Z],
                    static_cast<int>(initial_pilot_head_pos[PSI]), static_cast<int>(initial_pilot_head_pos[THE]));

        first_time = false;
    }

    // The very first time, or when reset, we save the current tracked head poisition
    if (input_reset) {
        first_data = data;
        prev_data = data;
        prev_time = current_time;

        input_reset = false;
        return;
    }

    const float time_diff = current_time - prev_time;
    average_data(data.d, prev_data.d, time_diff);

#if DEBUGWINDOW
    static char *debug_buf;
    asprintf(&debug_buf,
             "(%.1f,%.1f,%.1f) %d %d",
             data.d[X], data.d[Y], data.d[Z],
             static_cast<int>(data.d[PSI]), static_cast<int>(data.d[THE]));
    draw_debug_window(debug_buf);
    free(debug_buf);
#endif

    XPLMSetDataf(head_x, static_cast<float>((data.d[X] - first_data.d[X]) * X_FACTOR + initial_pilot_head_pos[X]));
    XPLMSetDataf(head_y, static_cast<float>((data.d[Y] - first_data.d[Y]) * Y_FACTOR + initial_pilot_head_pos[Y]));
    XPLMSetDataf(head_z, static_cast<float>((data.d[Z] - first_data.d[Z]) * Z_FACTOR + initial_pilot_head_pos[Z]));
    XPLMSetDataf(head_psi, static_cast<float>((data.d[PSI] - first_data.d[PSI]) * PSI_FACTOR + initial_pilot_head_pos[PSI]));
    XPLMSetDataf(head_the, static_cast<float>((data.d[THE] - first_data.d[THE]) * THE_FACTOR + initial_pilot_head_pos[THE]));
    // No need to roll the head
    // XPLMSetDataf(head_phi, (data[PHI] - first_data[PHI]) * PHI_FACTOR + initial_pilot_head_pos[PHI]);

    static int num_logs = 0;
    if (num_logs < 100) {
        log_stringf("Setting XYZ=(%.2f,%.2f,%.2f) psi=%d the=%d",
                    (data.d[X] - first_data.d[X]) * X_FACTOR + initial_pilot_head_pos[X],
                    (data.d[Y] - first_data.d[Y]) * Y_FACTOR + initial_pilot_head_pos[Y],
                    (data.d[Z] - first_data.d[Z]) * Z_FACTOR + initial_pilot_head_pos[Z],
                    static_cast<int>((data.d[PSI] - first_data.d[PSI]) * PSI_FACTOR + initial_pilot_head_pos[PSI]),
                    static_cast<int>((data.d[THE] - first_data.d[THE]) * THE_FACTOR + initial_pilot_head_pos[THE]));
        num_logs++;
    }

    prev_data = data;
    prev_time = current_time;
}

#if DEBUGWINDOW

static void draw_debug_window_callback(XPLMWindowID in_window_id, void *refcon)
{
    get_and_handle_data();
}

#else

static float flight_loop_callback(float inElapsedSinceLastCall,    
                                  float inElapsedTimeSinceLastFlightLoop,    
                                  int inCounter,    
                                  void *refcon)
{
    get_and_handle_data();

    return 1.0f/30;
}

#endif

static XPLMDataRef find_data_ref(const char *name, int expected_type)
{
    XPLMDataRef result = XPLMFindDataRef(name);
    if (result == NULL) {
        log_stringf("Could not find %s");
        return NULL;
    }
    if (XPLMGetDataRefTypes(result) != expected_type) {
        log_stringf("%s is of unexpected type", name);
        return NULL;
    }
    return result;
}
 
PLUGIN_API int XPluginStart(char * outName,
                            char * outSig,
                            char * outDesc)
{
    // The buffers are mentioned in instructions to be 256 characters
    strcpy_s(outName, 256, MYNAME);
    strcpy_s(outSig, 256, MYSIG);
    strcpy_s(outDesc, 256, "A plug-in that receives an OpenTrack-compatible data stream and moves the pilot's head.");

    XPLMSetErrorCallback(error_callback);

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        report_socket_error("socket");
        return 0;
    }

    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(4242);

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        report_socket_error("bind");
        CLOSESOCKET(sock);
        return 0;
    }

#if !IBM
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        report_socket_error("fcntl");
        CLOSESOCKET(sock);
        return 0;
    }
#else
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != NO_ERROR) {
        report_socket_error("ioctlsocket");
        return 0;
    }
#endif

    log_string("Starting");

    if ((view_type = find_data_ref("sim/graphics/view/view_type", xplmType_Int)) == NULL)
        return 0;
    if ((head_x = find_data_ref("sim/graphics/view/pilots_head_x", xplmType_Float)) == NULL)
        return 0;
    if ((head_y = find_data_ref("sim/graphics/view/pilots_head_y", xplmType_Float)) == NULL)
        return 0;
    if ((head_z = find_data_ref("sim/graphics/view/pilots_head_z", xplmType_Float)) == NULL)
        return 0;
    if ((head_psi = find_data_ref("sim/graphics/view/pilots_head_psi", xplmType_Float)) == NULL)
        return 0;
    if ((head_the = find_data_ref("sim/graphics/view/pilots_head_the", xplmType_Float)) == NULL)
        return 0;
    if ((head_phi = find_data_ref("sim/graphics/view/pilots_head_phi", xplmType_Float)) == NULL)
        return 0;

#if DEBUGWINDOW
    int left, bottom, right, top;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);

    const XPLMCreateWindow_t window_params = {
        sizeof(XPLMCreateWindow_t),
        left + 50,                   // left
        bottom + 50 + 200,           // top
        left + 50 + 400,             // right
        bottom + 50,                 // bottom
        1,                           // visible
        draw_debug_window_callback,
        dummy_mouse_handler,         // handleMouseClickFunc
        debug_window_key_handler,    // handleKeyFunc
        dummy_cursor_status_handler, // handleCursorFunc
        dummy_wheel_handler,         // handleMouseWheelFunc
        NULL,                        // refcon
        xplm_WindowDecorationRoundRectangle,
        xplm_WindowLayerFloatingWindows,
        NULL,                   // handleRightClickFunc
    };
    
    debug_window = XPLMCreateWindowEx(const_cast<XPLMCreateWindow_t*>(&window_params));
    
    if (debug_window == NULL)
        return 0;

    XPLMSetWindowPositioningMode(debug_window, xplm_WindowPositionFree, -1);
    XPLMSetWindowResizingLimits(debug_window, 200, 200, 400, 400);
    XPLMSetWindowTitle(debug_window, MYNAME " Debug Window");
#else
    const XPLMCreateFlightLoop_t flight_loop_params = {
        sizeof(XPLMCreateFlightLoop_t),
        xplm_FlightLoop_Phase_BeforeFlightModel,
        flight_loop_callback,
        NULL
    };

    flight_loop_id = XPLMCreateFlightLoop(const_cast<XPLMCreateFlightLoop_t*>(&flight_loop_params));
    XPLMScheduleFlightLoop(flight_loop_id, 1.0f/30, true);
#endif

    static int reset_item;
    XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
    int my_submenu_item = XPLMAppendMenuItem(plugins_menu, MYNAME, NULL, 0);
    XPLMMenuID my_menu = XPLMCreateMenu("", plugins_menu, my_submenu_item,
                                        [](void *menu, void *item) {
                                            if (item == &reset_item)
                                                input_reset = true;
                                        },
                                        NULL);
                                                
    XPLMAppendMenuItem(my_menu, "Reset", &reset_item, 0);

    return 1;
}

PLUGIN_API void XPluginStop(void)
{
}

PLUGIN_API void XPluginDisable(void)
{
}

PLUGIN_API int  XPluginEnable(void)
{
    return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
}
