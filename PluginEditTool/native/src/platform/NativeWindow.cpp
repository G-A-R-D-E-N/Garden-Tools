#include <fo4ide/platform/NativeWindow.hpp>

#if defined(_WIN32)

#include <windows.h>
#include <objbase.h>
#include <wrl.h>

#include <WebView2.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fo4ide/platform/PopoutRoute.hpp>

namespace fo4ide::platform {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t WindowClassName[] = L"FO4IDENativeWindow";
constexpr wchar_t PopoutTitle[] = L"FO4IDE Render";

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring(text.begin(), text.end());
    std::wstring value(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        value.data(), count);
    return value;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string value(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), count,
                        nullptr, nullptr);
    return value;
}

std::string hresultMessage(std::string_view operation, HRESULT value) {
    std::ostringstream stream;
    stream << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(value);
    return stream.str();
}

struct HostState;

struct WindowState {
    HWND hwnd{};
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring url;
    std::string route;
    std::weak_ptr<HostState> host;
    bool popout{false};
    bool fullscreen{false};
    WINDOWPLACEMENT restorePlacement{};
    LONG_PTR restoreStyle{0};
    std::string failure;

    void resize() const {
        if (!controller || !hwnd) return;
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        controller->put_Bounds(bounds);
    }
};

struct HostState {
    std::string appUrl;
    std::wstring userDataDirectory;
    ComPtr<ICoreWebView2Environment> environment;
    std::function<void(std::string_view, bool)> onPopoutChanged;
    std::map<std::string, std::shared_ptr<WindowState>, std::less<>> popouts;
};

void failWindow(const std::shared_ptr<WindowState>& state, std::string message) {
    state->failure = std::move(message);
    if (state->hwnd && IsWindow(state->hwnd)) DestroyWindow(state->hwnd);
}

void applyFullscreen(WindowState& state, bool enable) {
    if (!state.hwnd || state.fullscreen == enable) return;
    if (enable) {
        state.restoreStyle = GetWindowLongPtrW(state.hwnd, GWL_STYLE);
        state.restorePlacement = WINDOWPLACEMENT{};
        state.restorePlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(state.hwnd, &state.restorePlacement);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(state.hwnd, MONITOR_DEFAULTTONEAREST), &info)) return;
        const auto style = (state.restoreStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
                           static_cast<LONG_PTR>(WS_POPUP);
        SetWindowLongPtrW(state.hwnd, GWL_STYLE, style);
        SetWindowPos(state.hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(state.hwnd, GWL_STYLE, state.restoreStyle);
        SetWindowPlacement(state.hwnd, &state.restorePlacement);
        SetWindowPos(state.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    state.fullscreen = enable;
}

void detachPopout(WindowState& state) {
    if (state.controller) state.controller->Close();
    state.webview.Reset();
    state.controller.Reset();

    const auto host = state.host.lock();
    if (!host) return;
    const auto found = host->popouts.find(state.route);
    if (found == host->popouts.end() || found->second.get() != &state) return;
    auto keep = std::move(found->second);
    host->popouts.erase(found);
    if (host->onPopoutChanged) host->onPopoutChanged(state.route, false);
}

void closePopouts(HostState& host) {
    std::vector<HWND> handles;
    handles.reserve(host.popouts.size());
    for (const auto& entry : host.popouts) handles.push_back(entry.second->hwnd);
    for (const HWND hwnd : handles) {
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        if (state) {
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
    }

    switch (message) {
        case WM_SIZE:
            if (state) state->resize();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state && state->popout) {
                detachPopout(*state);
                return 0;
            }
            if (state) {
                if (const auto host = state->host.lock()) closePopouts(*host);
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

Result<void> registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, WindowClassName, &existing)) return {};

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&wc)) {
        return std::unexpected(Error{"WindowClassFailed", "Could not register the native FO4IDE window class"});
    }
    return {};
}

void configureSettings(const ComPtr<ICoreWebView2>& webview) {
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(TRUE);
        settings->put_AreDevToolsEnabled(TRUE);
        settings->put_IsZoomControlEnabled(TRUE);
    }
}

void openOrFocusPopout(const std::shared_ptr<HostState>& host, const std::string& route, const std::wstring& uri);
void createController(const std::shared_ptr<WindowState>& state, ICoreWebView2Environment* environment);

void attachWebViewHandlers(const std::shared_ptr<WindowState>& state, const std::shared_ptr<HostState>& host) {
    EventRegistrationToken token{};
    const std::weak_ptr<WindowState> weakState = state;
    const std::weak_ptr<HostState> weakHost = host;

    if (state->popout) {
        state->webview->add_WindowCloseRequested(
            Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                [weakState](ICoreWebView2*, IUnknown*) -> HRESULT {
                    if (const auto locked = weakState.lock(); locked && locked->hwnd) DestroyWindow(locked->hwnd);
                    return S_OK;
                }).Get(),
            &token);

        state->webview->add_ContainsFullScreenElementChanged(
            Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [weakState](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    BOOL contains = FALSE;
                    if (sender) sender->get_ContainsFullScreenElement(&contains);
                    if (const auto locked = weakState.lock()) applyFullscreen(*locked, contains != FALSE);
                    return S_OK;
                }).Get(),
            &token);
    }

    state->webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [weakHost](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                const auto locked = weakHost.lock();
                if (!locked || !args) return S_OK;
                LPWSTR raw = nullptr;
                if (FAILED(args->get_Uri(&raw)) || !raw) return S_OK;
                const std::wstring uri(raw);
                CoTaskMemFree(raw);
                const auto route = popoutRouteFor(narrow(uri), locked->appUrl);
                if (!route) return S_OK;
                args->put_Handled(TRUE);
                openOrFocusPopout(locked, *route, uri);
                return S_OK;
            }).Get(),
        &token);
}

void createController(const std::shared_ptr<WindowState>& state, ICoreWebView2Environment* environment) {
    const auto callback = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [state](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
            if (FAILED(controllerResult) || !controller) {
                failWindow(state, hresultMessage("CreateCoreWebView2Controller", controllerResult));
                return S_OK;
            }
            if (!state->hwnd || !IsWindow(state->hwnd)) {
                controller->Close();
                return S_OK;
            }

            state->controller = controller;
            HRESULT hr = state->controller->get_CoreWebView2(&state->webview);
            if (FAILED(hr) || !state->webview) {
                failWindow(state, hresultMessage("get_CoreWebView2", hr));
                return S_OK;
            }

            configureSettings(state->webview);
            if (const auto host = state->host.lock()) attachWebViewHandlers(state, host);
            state->resize();
            hr = state->webview->Navigate(state->url.c_str());
            if (FAILED(hr)) failWindow(state, hresultMessage("WebView2 Navigate", hr));
            return S_OK;
        });

    const auto hr = environment->CreateCoreWebView2Controller(state->hwnd, callback.Get());
    if (FAILED(hr)) failWindow(state, hresultMessage("CreateCoreWebView2Controller", hr));
}

void openPopout(const std::shared_ptr<HostState>& host, const std::string& route, const std::wstring& uri) {
    if (!host->environment) return;

    auto state = std::make_shared<WindowState>();
    state->url = uri;
    state->route = route;
    state->host = host;
    state->popout = true;

    const auto hwnd = CreateWindowExW(
        0,
        WindowClassName,
        PopoutTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        800,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        state.get());
    if (!hwnd) return;

    host->popouts[route] = state;
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    if (host->onPopoutChanged) host->onPopoutChanged(route, true);
    createController(state, host->environment.Get());
}

void openOrFocusPopout(const std::shared_ptr<HostState>& host, const std::string& route, const std::wstring& uri) {
    const auto found = host->popouts.find(route);
    if (found != host->popouts.end()) {
        const HWND hwnd = found->second->hwnd;
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return;
    }
    openPopout(host, route, uri);
}

Result<void> createWebView(const std::shared_ptr<WindowState>& state, const std::shared_ptr<HostState>& host) {
    const wchar_t* userData = host->userDataDirectory.empty() ? nullptr : host->userDataDirectory.c_str();
    const auto callback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [state, host](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment) {
                failWindow(state, hresultMessage("CreateCoreWebView2Environment", result));
                return S_OK;
            }
            host->environment = environment;
            createController(state, environment);
            return S_OK;
        });

    const auto hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData, nullptr, callback.Get());
    if (FAILED(hr)) {
        return std::unexpected(Error{"WebView2InitFailed", hresultMessage("CreateCoreWebView2EnvironmentWithOptions", hr)});
    }
    return {};
}

}

bool NativeWindow::available() noexcept {
    return true;
}

Result<int> NativeWindow::run(const WindowOptions& options) {
    if (options.url.empty()) return std::unexpected(Error{"WindowUrlMissing", "Native window URL is empty"});

    const auto coResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(coResult);
    if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
        return std::unexpected(Error{"ComInitFailed", hresultMessage("CoInitializeEx", coResult)});
    }

    const auto cleanup = [&]() {
        if (uninitialize) CoUninitialize();
    };

    const auto instance = GetModuleHandleW(nullptr);
    auto registered = registerWindowClass(instance);
    if (!registered) {
        cleanup();
        return std::unexpected(registered.error());
    }

    auto host = std::make_shared<HostState>();
    host->appUrl = options.url;
    host->onPopoutChanged = options.onPopoutChanged;
    if (!options.userDataDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.userDataDirectory, ec);
        host->userDataDirectory = options.userDataDirectory.wstring();
    }

    auto state = std::make_shared<WindowState>();
    state->url = widen(options.url);
    state->host = host;

    const auto title = widen(options.title);
    const auto hwnd = CreateWindowExW(
        0,
        WindowClassName,
        title.empty() ? L"FO4IDE" : title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        std::max(options.width, 640),
        std::max(options.height, 480),
        nullptr,
        nullptr,
        instance,
        state.get());
    if (!hwnd) {
        cleanup();
        return std::unexpected(Error{"WindowCreateFailed", "Could not create the native FO4IDE window"});
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    auto webview = createWebView(state, host);
    if (!webview) {
        DestroyWindow(hwnd);
        cleanup();
        return std::unexpected(webview.error());
    }

    MSG message{};
    int result = 0;
    while (true) {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status == 0) {
            result = static_cast<int>(message.wParam);
            break;
        }
        if (status == -1) {
            cleanup();
            return std::unexpected(Error{"WindowMessageFailed", "GetMessage failed for the native window"});
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    state->webview.Reset();
    if (state->controller) state->controller->Close();
    state->controller.Reset();
    cleanup();

    if (!state->failure.empty()) return std::unexpected(Error{"WebView2InitFailed", state->failure});
    return result;
}

}

#else

namespace fo4ide::platform {

bool NativeWindow::available() noexcept {
    return false;
}

Result<int> NativeWindow::run(const WindowOptions&) {
    return std::unexpected(Error{"NativeWindowUnsupported", "Embedded native window hosting is currently available on Windows; use --browser on this platform"});
}

}

#endif