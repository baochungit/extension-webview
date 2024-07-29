#if defined(DM_PLATFORM_HTML5)

#include <stdlib.h>
#include <unistd.h>
#include <dmsdk/sdk.h>
#include <emscripten.h>

#include "webview_common.h"

enum CommandType
{
    CMD_LOAD_OK,
    CMD_LOAD_ERROR,
    CMD_EVAL_OK,
    CMD_EVAL_ERROR,
    CMD_LOADING,
};

struct WebViewCommand
{
    WebViewCommand()
    {
        memset(this, 0, sizeof(WebViewCommand));
    }
    CommandType m_Type;
    int         m_WebViewID;
    int         m_RequestID;
    void*       m_Data;
    const char* m_Url;
};

struct WebViewExtensionState
{
    WebViewExtensionState()
    {
        memset(this, 0, sizeof(*this));
    }

    void Clear()
    {
        for( int i = 0; i < dmWebView::MAX_NUM_WEBVIEWS; ++i )
        {
            ClearWebViewInfo(&m_Info[i]);
        }
        memset(m_RequestIds, 0, sizeof(m_RequestIds));
    }

    dmWebView::WebViewInfo  m_Info[dmWebView::MAX_NUM_WEBVIEWS];
    bool                    m_Used[dmWebView::MAX_NUM_WEBVIEWS];
    int                     m_RequestIds[dmWebView::MAX_NUM_WEBVIEWS];
    dmMutex::HMutex         m_Mutex;
    dmArray<WebViewCommand> m_CmdQueue;
};

WebViewExtensionState g_WebView;

namespace dmWebView {

typedef void (*OnAddToCommandQueue)(int webview_id, CommandType type, const char* url, int request_id);

extern "C" void JS_WebView_initialize(OnAddToCommandQueue f);
extern "C" void JS_WebView_finalize();
extern "C" void JS_WebView_create(int id);
extern "C" void JS_WebView_destroy(int id);
extern "C" void JS_WebView_openRaw(int id, const char* html, int request_id);
extern "C" void JS_WebView_eval(int id, const char* code, int request_id);
extern "C" void JS_WebView_open(int id, const char* url, int request_id);
extern "C" void JS_WebView_continueOpen(int id, const char* url, int request_id);
extern "C" void JS_WebView_setVisible(int id, int visible);
extern "C" int JS_WebView_isVisible(int id);
extern "C" void JS_WebView_setPosition(int id, int x, int y, int width, int height);
extern "C" const char* JS_WebView_getText(int id);

#define CHECK_WEBVIEW_AND_RETURN() if( webview_id >= MAX_NUM_WEBVIEWS || webview_id < 0 ) { dmLogError("%s: Invalid webview_id: %d", __FUNCTION__, webview_id); return -1; }

int Platform_Create(lua_State* L, dmWebView::WebViewInfo* _info)
{
    // Find a free slot
    int webview_id = -1;
    for( int i = 0; i < MAX_NUM_WEBVIEWS; ++i )
    {
        if( !g_WebView.m_Used[i] )
        {
            webview_id = i;
            break;
        }
    }

    if( webview_id == -1 )
    {
        dmLogError("Max number of webviews already opened: %d", MAX_NUM_WEBVIEWS);
        return -1;
    }

    g_WebView.m_Used[webview_id] = true;
    g_WebView.m_Info[webview_id] = *_info;

    JS_WebView_create(webview_id);

    return webview_id;
}

static int DestroyWebView(int webview_id)
{
    CHECK_WEBVIEW_AND_RETURN();
    JS_WebView_destroy(webview_id);
    g_WebView.m_Used[webview_id] = false;
    return 0;
}

int Platform_Destroy(lua_State* L, int webview_id)
{
    DestroyWebView(webview_id);
    return 0;
}

int Platform_Open(lua_State* L, int webview_id, const char* url, dmWebView::RequestInfo* options)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];
    JS_WebView_setVisible(webview_id, !options->m_Hidden);
    JS_WebView_open(webview_id, url, request_id);
    return request_id;
}

int Platform_ContinueOpen(lua_State* L, int webview_id, int request_id, const char* url)
{
    CHECK_WEBVIEW_AND_RETURN();
    JS_WebView_continueOpen(webview_id, url, request_id);
    return request_id;
}

int Platform_CancelOpen(lua_State* L, int webview_id, int request_id, const char* url)
{
    return request_id;
}

int Platform_OpenRaw(lua_State* L, int webview_id, const char* html, dmWebView::RequestInfo* options)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];
    JS_WebView_setVisible(webview_id, !options->m_Hidden);
    JS_WebView_openRaw(webview_id, html, request_id);
    return request_id;
}

int Platform_Eval(lua_State* L, int webview_id, const char* code)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];
    JS_WebView_eval(webview_id, code, request_id);
    return request_id;
}

int Platform_SetVisible(lua_State* L, int webview_id, int visible)
{
    CHECK_WEBVIEW_AND_RETURN();
    JS_WebView_setVisible(webview_id, visible);
    return 0;
}

int Platform_IsVisible(lua_State* L, int webview_id)
{
    CHECK_WEBVIEW_AND_RETURN();
    return JS_WebView_isVisible(webview_id);
}

int Platform_SetPosition(lua_State* L, int webview_id, int x, int y, int width, int height)
{
    CHECK_WEBVIEW_AND_RETURN();
    JS_WebView_setPosition(webview_id, x, y, width, height);
    return 0;
}

#undef CHECK_WEBVIEW_AND_RETURN

static void QueueCommand(WebViewCommand* cmd)
{
    DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
    if (g_WebView.m_CmdQueue.Full())
    {
        g_WebView.m_CmdQueue.OffsetCapacity(8);
    }
    g_WebView.m_CmdQueue.Push(*cmd);
}

static void AddToCommandQueue(int webview_id, CommandType type, const char* url, int request_id)
{
	WebViewCommand cmd;
    cmd.m_Type = type;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = strdup(url);
    QueueCommand(&cmd);
}

dmExtension::Result Platform_Update(dmExtension::Params* params)
{
    if (g_WebView.m_CmdQueue.Empty())
    {
        return dmExtension::RESULT_OK; // avoid a lock (~300us on iPhone 4s)
    }

    dmArray<WebViewCommand> tmp;
    {
        DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
        tmp.Swap(g_WebView.m_CmdQueue);
    }

    for (uint32_t i=0; i != tmp.Size(); ++i)
    {
        const WebViewCommand& cmd = tmp[i];

        dmWebView::CallbackInfo cbinfo;
        switch (cmd.m_Type)
        {
        case CMD_LOADING:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_LOADING;
            cbinfo.m_Result = 0;
            RunCallback(&cbinfo);
            break;

        case CMD_LOAD_OK:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_OK;
            cbinfo.m_Result = 0;
            RunCallback(&cbinfo);
            break;

        case CMD_LOAD_ERROR:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_ERROR;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        case CMD_EVAL_OK:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = 0;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_EVAL_OK;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        case CMD_EVAL_ERROR:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = 0;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_EVAL_ERROR;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        default:
            assert(false);
        }
        if (cmd.m_Url) {
            free((void*)cmd.m_Url);
        }
        if (cmd.m_Data) {
            free(cmd.m_Data);
        }
    }
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_AppInitialize(dmExtension::AppParams* params)
{
    g_WebView.m_Mutex = dmMutex::New();
    g_WebView.m_CmdQueue.SetCapacity(8);

    JS_WebView_initialize((OnAddToCommandQueue)AddToCommandQueue);
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_Initialize(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_AppFinalize(dmExtension::AppParams* params)
{
    JS_WebView_finalize();
    dmMutex::Delete(g_WebView.m_Mutex);
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_Finalize(dmExtension::Params* params)
{
    for( int i = 0; i < dmWebView::MAX_NUM_WEBVIEWS; ++i )
    {
        if (g_WebView.m_Used[i]) {
            DestroyWebView(i);
        }
    }

    DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
    for (uint32_t i=0; i != g_WebView.m_CmdQueue.Size(); ++i)
    {
        const WebViewCommand& cmd = g_WebView.m_CmdQueue[i];
        if (cmd.m_Url) {
            free((void*)cmd.m_Url);
        }
    }
    g_WebView.m_CmdQueue.SetSize(0);
    return dmExtension::RESULT_OK;
}

} // namespace dmWebView

#endif // DM_PLATFORM_HTML5