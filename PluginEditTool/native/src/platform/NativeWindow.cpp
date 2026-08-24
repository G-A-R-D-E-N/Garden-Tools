#include <fo4ide/platform/NativeWindow.hpp>

#if defined(_WIN32)

#include <windows.h>
#include <wrl.h>

#include <WebView2.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace fo4ide::platform {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

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

std::string hresultMessage(std::string_view operation, HRESULT value) {
    std::ostringstream stream;
    stream << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(value);
    return stream.str();
}

struct WindowState {
    HWND hwnd{};
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring url;
    std::wstring userDataDirectory;
    std::string failure;

    void resize() const {
        if (!controller || !hwnd) return;
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        controller->put_Bounds(bounds);
    }
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static_cast<void>(wParam);
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
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

Result<void> registerWindowClass(HINSTANCE instance) {
    static constexpr wchar_t ClassName[] = L"FO4IDENativeWindow";
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, ClassName, &existing)) return {};

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = ClassName;
    if (!RegisterClassExW(&wc)) {
        return std::unexpected(Error{"WindowClassFailed", "Could not register the native FO4IDE window class"});
    }
    return {};
}

void failWindow(const std::shared_ptr<WindowState>& state, std::string message) {
    state->failure = std::move(message);
    if (state->hwnd && IsWindow(state->hwnd)) DestroyWindow(state->hwnd);
}

Result<void> createWebView(const std::shared_ptr<WindowState>& state) {
    const wchar_t* userData = state->userDataDirectory.empty() ? nullptr : state->userDataDirectory.c_str();
    const auto callback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [state](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment) {
                failWindow(state, hresultMessage("CreateCoreWebView2Environment", result));
                return S_OK;
            }

            const auto controllerCallback = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [state](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(controllerResult) || !controller) {
                        failWindow(state, hresultMessage("CreateCoreWebView2Controller", controllerResult));
                        return S_OK;
                    }

                    state->controller = controller;
                    HRESULT hr = state->controller->get_CoreWebView2(&state->webview);
                    if (FAILED(hr) || !state->webview) {
                        failWindow(state, hresultMessage("get_CoreWebView2", hr));
                        return S_OK;
                    }

                    ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(state->webview->get_Settings(&settings)) && settings) {
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(TRUE);
                        settings->put_AreDevToolsEnabled(TRUE);
                        settings->put_IsZoomControlEnabled(TRUE);
                    }

                    state->resize();
                    hr = state->webview->Navigate(state->url.c_str());
                    if (FAILED(hr)) failWindow(state, hresultMessage("WebView2 Navigate", hr));
                    return S_OK;
                });

            const auto hr = environment->CreateCoreWebView2Controller(state->hwnd, controllerCallback.Get());
            if (FAILED(hr)) failWindow(state, hresultMessage("CreateCoreWebView2Controller", hr));
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

    auto state = std::make_shared<WindowState>();
    state->url = widen(options.url);
    if (!options.userDataDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.userDataDirectory, ec);
        state->userDataDirectory = options.userDataDirectory.wstring();
    }

    const auto title = widen(options.title);
    const auto hwnd = CreateWindowExW(
        0,
        L"FO4IDENativeWindow",
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

    auto webview = createWebView(state);
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
