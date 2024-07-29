
var LibraryWebView = {

    // This can be accessed from the bootstrap code in the .html file
    $WebView: {
        list: {},
        CommandType: {
            CMD_LOAD_OK: 0,
            CMD_LOAD_ERROR: 1,
            CMD_EVAL_OK: 2,
            CMD_EVAL_ERROR: 3,
            CMD_LOADING: 4,
        },

        addEventToQueue: function(id, name, value) {
            if (WebView.list[id]) {
                var data = WebView.list[id];
                var AddToCommandQueue = WebView._AddToCommandQueue;
                var cstr = stringToNewUTF8(value);
                {{{ makeDynCall('viii', 'AddToCommandQueue')}}}(id, name, cstr, data.requestId);
                _free(cstr);
            }
        },

    },

    JS_WebView_initialize: function(c_AddToCommandQueue) {
        WebView._AddToCommandQueue = c_AddToCommandQueue;
    },

    JS_WebView_finalize: function() {
        for (var id in WebView.list) {
            var data = WebView.list[id];
            data.iframe.parentNode.removeChild(data.iframe);
            delete WebView.list[id];
        }
    },

    JS_WebView_create: function(id) {
        var iframe = document.createElement('iframe');
        iframe.setAttribute('id', 'LibraryWebView-' + id);
        iframe.style.position = 'absolute';
        iframe.style.left = 0;
        iframe.style.top = 0;
        iframe.style.width = 0;
        iframe.style.height = 0;
        iframe.style.padding = 0;
        iframe.style.outline = 'none';
        iframe.style.border = 'none';
        iframe.style.display = 'none';

        var container = document.getElementById('canvas-container');
        container.appendChild(iframe);

        iframe.addEventListener('load', function(ev) {
            WebView.addEventToQueue(id, WebView.CommandType.CMD_LOAD_OK, iframe.src);
        });
        // iframe.contentWindow.addEventListener('beforeunload', function(ev) {
        //     if (WebView.list[id].continueOpen) {
        //         delete WebView.list[id].continueOpen;
        //     } else {
        //         ev.preventDefault();
        //     }
        // });

        WebView.list[id] = {
            iframe,
            requestId: -1
        };
        return id;
    },

    JS_WebView_destroy: function(id) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.iframe.parentNode.removeChild(data.iframe);
            delete WebView.list[id];
        }
    },

    JS_WebView_openRaw: function (id, html, requestId) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.requestId = requestId;
            data.iframe.src = 'data:text/html;charset=utf-8,' + escape(UTF8ToString(html));
        }
    },

    // not available now
    JS_WebView_eval: function (id, code, requestId) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.requestId = requestId;
        }
    },

    JS_WebView_open: function (id, url, requestId) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.requestId = requestId;
            data.iframe.src = UTF8ToString(url);
        }
    },

    // not available now
    JS_WebView_continueOpen: function (id, url, requestId) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.requestId = requestId;
            // data.continueLoadingUrl = url;
            data.iframe.src = UTF8ToString(url);
        }
    },

    JS_WebView_setVisible: function(id, visible) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.iframe.style.display = visible ? 'block' : 'none';
        }
    },

    JS_WebView_isVisible: function(id) {
        return data.iframe.style.display == 'none' ? 1 : 0;
    },

    JS_WebView_setPosition: function(id, x, y, width, height) {
        if (WebView.list[id]) {
            var data = WebView.list[id];
            data.iframe.style.left = x + 'px';
            data.iframe.style.top = y + 'px';
            data.iframe.style.width = width >= 0 ? width + 'px' : '100%';
            data.iframe.style.height = height >= 0 ? height + 'px' : '100%';
        }
    },

};

autoAddDeps(LibraryWebView, '$WebView');
addToLibrary(LibraryWebView);
