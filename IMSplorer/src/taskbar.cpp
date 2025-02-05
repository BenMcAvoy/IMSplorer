#include "pch.h"

#include "taskbar.h"

using namespace IMS;

static Taskbar* g_Taskbar = nullptr;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#define SAFE_CLEANUP(x) if (x) { x->Release(); x = nullptr; }

/// <summary>
/// Check if a window is valid (wants to be shown, not a tool window, not invisible, etc)
/// </summary>
/// <param name="hWnd"></param>
/// <returns>True if the window is valid</returns>
static bool validateWindow(HWND hWnd) {
	if (GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
	if (!IsWindowVisible(hWnd)) return false;
	if (GetWindowLong(hWnd, GWLP_HWNDPARENT) != 0) return false;

	if (g_Taskbar->windows.find(hWnd) != g_Taskbar->windows.end()) return false;

	char name[256] = { 0 };
	GetWindowTextA(hWnd, name, 256);
	if (strstr(name, "ImSplorer") != nullptr || memcmp(name, "Start", 5) == 0)
		return false;

	return true;
}

void Taskbar::InitWindow() {
	// Set window properties
	this->title = title;
	std::wstring titleW(title.begin(), title.end());

	// Create window
	this->wc.cbSize = sizeof(WNDCLASSEX);
	this->wc.style = CS_CLASSDC;
	this->wc.lpfnWndProc = WndProc;
	this->wc.cbClsExtra = 0L;
	this->wc.cbWndExtra = 0L;
	this->wc.hInstance = GetModuleHandle(nullptr);
	this->wc.hIcon = nullptr;
	this->wc.hCursor = nullptr;
	this->wc.hbrBackground = nullptr;
	this->wc.lpszMenuName = nullptr;
	this->wc.lpszClassName = L"ImSplorerTB";
	this->wc.style = CS_DBLCLKS; // Double click messages
	this->wc.hIconSm = nullptr;
	RegisterClassEx(&this->wc);

	DWORD dwStyle = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	DWORD dwExStyle = WS_EX_LEFT | WS_EX_LTRREADING | WS_EX_RIGHTSCROLLBAR |
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	RECT workArea = { 0 };
	if (SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0)) {
		int taskbarHeight = screenHeight - (workArea.bottom - workArea.top);
		if (taskbarHeight != (float)this->tbHeight) {
			spdlog::info("Taskbar height changed from {} to {}", this->tbHeight, this->tbHeight);
			this->tbHeight = (float)taskbarHeight;
		}
	}
	else {
		spdlog::warn("SystemParametersInfo failed, can't calculate taskbar height");
	}

	this->hWnd = CreateWindowExW(
		dwExStyle,
		wc.lpszClassName,
		titleW.c_str(),
		dwStyle,
		0, screenHeight - this->tbHeight, // X, Y position (bottom of screen)
		screenWidth, this->tbHeight,        // Width, Height
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	// Check for errors
	if (this->hWnd == nullptr) {
		// Get error message
		LPSTR messageBuffer = nullptr;
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
		std::string message(messageBuffer, size);
		LocalFree(messageBuffer);

		spdlog::error("Failed to create window: {}", message);

		return;
	}

	this->width = screenWidth;
	this->height = this->tbHeight;

	// Show window
	ShowWindow(this->hWnd, SW_SHOWDEFAULT);
	UpdateWindow(this->hWnd);

	this->isRunning = true;
}

void Taskbar::InitShell() const {
	auto user32 = LoadLibrary(L"user32.dll");
	if (user32) {
		auto SetShellWindow = (BOOL(WINAPI*)(HWND))GetProcAddress(user32, "SetShellWindow");
		SetShellWindow ? SetShellWindow(this->hWnd) : false;
		auto SetTaskmanWindow = (BOOL(WINAPI*)(HWND))GetProcAddress(user32, "SetTaskmanWindow");
		SetTaskmanWindow ? SetTaskmanWindow(this->hWnd) : false;

		if (!SetShellWindow || !SetTaskmanWindow)
			spdlog::error("SetShellWindow/SetTaskmanTaskbar not found or failed");
	}
	else {
		spdlog::error("user32.dll not found");
	}

	if (!RegisterShellHookWindow(this->hWnd)) {
		spdlog::error("RegisterShellHookTaskbar failed");
	}
}

void Taskbar::InitD3D() {
	// Before creating the device, we need to create the window
	if (!this->hWnd) {
		spdlog::error("Failed to create window");
		return;
	}

	// Create DX11
	DXGI_SWAP_CHAIN_DESC scd = { 0 };
	scd.BufferDesc.Width = this->width;
	scd.BufferDesc.Height = this->height;
	scd.BufferDesc.RefreshRate.Numerator = 60;
	scd.BufferDesc.RefreshRate.Denominator = 1;
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	scd.SampleDesc.Count = 1;
	scd.SampleDesc.Quality = 0;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 1;
	scd.OutputWindow = this->hWnd;
	scd.Windowed = TRUE;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Flags = 0;

#ifdef _DEBUG
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
	UINT createDeviceFlags = 0;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &scd, &this->swapChain, &this->device, &featureLevel, &this->deviceContext);

	if (FAILED(res)) {
		// Format error message
		LPSTR messageBuffer = nullptr;
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, res, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
		std::string message(messageBuffer, size);
		LocalFree(messageBuffer);

		std::ofstream file("error.txt");
		file << message;
		file.close();
	}

	ID3D11Texture2D* pBackBuffer;
	this->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	this->device->CreateRenderTargetView(pBackBuffer, nullptr, &this->renderTargetView);
	pBackBuffer->Release();
}

void Taskbar::InitImGui() {
	// Create ImGui
	IMGUI_CHECKVERSION();
	this->imguiContext = ImGui::CreateContext();
	this->imguiIO = &ImGui::GetIO();
	this->imguiStyle = &ImGui::GetStyle();

	// Set ImGui io
	this->imguiIO->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	this->imguiIO->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	this->imguiIO->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	this->imguiIO->IniFilename = nullptr;

	ImGui_ImplWin32_Init(this->hWnd);
	ImGui_ImplDX11_Init(this->device, this->deviceContext);

	// Set ImGui style
	ImGui::StyleColorsDark();
	this->imguiStyle->WindowRounding = 0.0f;
    this->imguiStyle->Colors[ImGuiCol_WindowBg].w = 1.0f;
	this->imguiStyle->FrameRounding = 4.0f;

	// Load font (C:\Taskbars\Fonts\segoeui.ttf)
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 1;
	fontConfig.OversampleV = 1;
	fontConfig.PixelSnapH = true;
	this->imguiIO->Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &fontConfig);
	this->imguiIO->Fonts->Build();

	// set all hue values to 0
	for (int i = 0; i < ImGuiCol_COUNT; i++) {
		ImVec4& col = this->imguiStyle->Colors[i];
		float H, S, V;
		ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, H, S, V);
		H = 0.0f;
		ImGui::ColorConvertHSVtoRGB(H, S, V, col.x, col.y, col.z);
	}
}

Taskbar::Taskbar(const std::string& title) {
	g_Taskbar = this;

	this->InitWindow();
	this->InitShell();
	this->InitD3D();
	this->InitImGui();

	// Fill in the windows map
	EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
		Taskbar* taskbar = (Taskbar*)lParam;

		DWORD processId;
		GetWindowThreadProcessId(hWnd, &processId);
		HANDLE iterProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
		if (!iterProc) return TRUE;
		char exePath[MAX_PATH];
		GetModuleFileNameExA(iterProc, nullptr, exePath, MAX_PATH);
		CloseHandle(iterProc);

		// Validate (not a tool window, not us, not invisible)
		if (!validateWindow(hWnd)) return TRUE;

		std::string exeFileName = std::filesystem::path(exePath).filename().string();
		taskbar->windows[hWnd] = { exeFileName, false };
		}, (LPARAM)this);

	// Set the hook
	this->kbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);
}

Taskbar::~Taskbar() {
	// Destroy ImGui
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext(this->imguiContext);

	// Destroy DX11
	this->renderTargetView->Release();
	this->swapChain->Release();
	this->deviceContext->Release();
	this->device->Release();

	// Destroy window
	DestroyWindow(this->hWnd);
	UnregisterClass(this->wc.lpszClassName, this->wc.hInstance);

	// Unhook
	UnhookWindowsHookEx(this->kbdHook);
}

void Taskbar::Run() {
	MSG msg = { 0 };
	while (this->isRunning) {
		if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				spdlog::info("WM_QUIT received, stopping taskbar");
				this->isRunning = false;
			}
		}

		if (!this->isRunning)
			break;

		if (this->occluded && this->swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}

		this->occluded = false;

		if (this->resizeWidth != 0 && this->resizeHeight != 0) {
			SAFE_CLEANUP(this->renderTargetView);

			this->swapChain->ResizeBuffers(0, this->resizeWidth, this->resizeHeight, DXGI_FORMAT_UNKNOWN, 0);

			ID3D11Texture2D* pBackBuffer;
			this->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			this->device->CreateRenderTargetView(pBackBuffer, nullptr, &this->renderTargetView);
			pBackBuffer->Release();

			this->width = this->resizeWidth;
			this->height = this->resizeHeight;
			this->resizeWidth = 0; this->resizeHeight = 0;
		}

		// Render
		static ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
		this->deviceContext->ClearRenderTargetView(this->renderTargetView, (float*)&clear_color);

		// ImGui
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		float screenWidth = (float)GetSystemMetrics(SM_CXSCREEN);
		float screenHeight = (float)GetSystemMetrics(SM_CYSCREEN);

		{
			ImGui::SetNextWindowPos(ImVec2(0, screenHeight - this->height));
			ImGui::SetNextWindowSize(ImVec2(this->width, this->height));
			ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
			ImGui::Begin("IMSplorer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

			float windowPadding = ImGui::GetStyle().WindowPadding.y * 2;
			//float width = ImGui::CalcTextSize("Start").x + windowPadding;
			if (ImGui::Button(" ", ImVec2(this->tbHeight - windowPadding, this->tbHeight - windowPadding))) {
				this->showStartMenu = !this->showStartMenu;
			}

			ImGui::SameLine();

			const auto end = this->windows.end();
			for (auto it = this->windows.begin(); it != end; ++it) {
				ImGui::PushID(reinterpret_cast<void*>(it->first));

				if (!it->second.isFocused)
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

				float width = ImGui::CalcTextSize(it->second.exe.c_str()).x + windowPadding;
				if (ImGui::Button(it->second.exe.c_str(), ImVec2(width, this->tbHeight - windowPadding))) {
					HWND hWnd = it->first;

					if (!it->second.isFocused) {
						AllowSetForegroundWindow(ASFW_ANY);
						SetForegroundWindow(hWnd);
						if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
					}
					else {
						AllowSetForegroundWindow(ASFW_ANY);
						SetForegroundWindow(hWnd);
						ShowWindow(hWnd, SW_MINIMIZE);
					}
				}

				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					char title[256] = { 0 };
					GetWindowTextA(it->first, title, 256);
					ImGui::Text(title);
					ImGui::EndTooltip();
				}

				if (!it->second.isFocused)
					ImGui::PopStyleColor();

				if (ImGui::BeginPopupContextWindow()) {
					if (ImGui::MenuItem("Close")) {
						PostMessage(it->first, WM_CLOSE, 0, 0);
					}
					ImGui::EndPopup();
				}

				if (it != end)
					ImGui::SameLine();

				ImGui::PopID();
			}

			float windowWidth = ImGui::GetWindowContentRegionMax().x - this->imguiStyle->WindowPadding.x;
			auto now = std::chrono::system_clock::now();
			time_t now_t = std::chrono::system_clock::to_time_t(now);
			std::string time = fmt::format("{:%H:%M:%S}", fmt::localtime(now_t));
			std::string date = fmt::format("{:%A, %B %d, %Y}", fmt::localtime(now_t));

			float timeWidth = ImGui::CalcTextSize(time.c_str()).x;
			float dateWidth = ImGui::CalcTextSize(date.c_str()).x;
			ImGui::SameLine(windowWidth - max(timeWidth, dateWidth));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::BeginChild("Clock");
			ImGui::Text(date.c_str());
			ImGui::SetCursorPosX(dateWidth - timeWidth);
			ImGui::Text(time.c_str());
			ImGui::EndChild();
			ImGui::PopStyleVar(2);

			ImGui::End();
		}

		{
			if (!this->showStartMenu) goto RENDER;

			static constexpr float startMenuWidth = 400;
			static constexpr float startMenuHeight = 400;
			ImGui::SetNextWindowPos(ImVec2(0, screenHeight - startMenuHeight - this->tbHeight));
			ImGui::SetNextWindowSize(ImVec2(400, 400));
			ImGui::Begin("Start", &this->showStartMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

			static char buffer[256] = { 0 };
			ImGui::SetNextItemWidth(ImGui::GetWindowContentRegionMax().x - this->imguiStyle->WindowPadding.x);
			ImGui::InputText("##Search", buffer, IM_ARRAYSIZE(buffer));

			// Go through C:\ProgramData\Microsoft\Windows\Start Menu\Programs
			// TODO: This code is extremely slow and inefficient, fix it
			ImGui::BeginChild("##Display", ImVec2(0, 0), false);
			for (auto& file : std::filesystem::recursive_directory_iterator("C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\")) {
				if (file.is_directory() || file.path().extension() != ".lnk") continue;

				std::string fileName = file.path().filename().string();
				std::string jsFileName = fileName.substr(0, fileName.find(".lnk"));

				// Check if the file name contains the search query (case insensitive)
				std::string searchQuery = buffer;
				std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), ::tolower);
				std::string jsFileNameLower = jsFileName;
				std::transform(jsFileNameLower.begin(), jsFileNameLower.end(), jsFileNameLower.begin(), ::tolower);
				if (searchQuery != "" && jsFileNameLower.find(searchQuery) == std::string::npos) continue;

				float windowWidth = ImGui::GetWindowContentRegionMax().x - this->imguiStyle->WindowPadding.x;
				if (ImGui::Button(jsFileName.c_str(), ImVec2(windowWidth, this->tbHeight))) {
					ShellExecuteA(nullptr, "open", file.path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
			}
			ImGui::EndChild();

			ImGui::End();
		}

	RENDER:

		ImGui::Render();
		this->deviceContext->OMSetRenderTargets(1, &this->renderTargetView, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		if (this->imguiIO->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		// Swap buffers
		if (this->swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED)
			this->occluded = true;
	}
}

LRESULT WINAPI Taskbar::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//RegisterTaskbarMessageW(TEXT("SHELLHOOK"));

	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg) {
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;

		g_Taskbar->resizeWidth = (UINT)LOWORD(lParam); // Queue resize
		g_Taskbar->resizeHeight = (UINT)HIWORD(lParam);

		return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;

		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		if (msg == RegisterWindowMessage(TEXT("SHELLHOOK"))) {
			if (wParam == HSHELL_WINDOWDESTROYED) {
				auto it = g_Taskbar->windows.find((HWND)lParam);
				if (it != g_Taskbar->windows.end()) {
					g_Taskbar->windows.erase(it);
				}

				return 0;
			}

			if (wParam == HSHELL_WINDOWCREATED) {
				DWORD processId;
				GetWindowThreadProcessId((HWND)lParam, &processId);
				HANDLE iterProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
				if (!iterProc) return 0;
				char exePath[MAX_PATH];
				GetModuleFileNameExA(iterProc, nullptr, exePath, MAX_PATH);
				CloseHandle(iterProc);

				// Validate (not a tool window, not us, not invisible)
				if (!validateWindow((HWND)lParam)) return 0;

				std::string exeFileName = std::filesystem::path(exePath).filename().string();
				g_Taskbar->windows[(HWND)lParam] = { exeFileName, false };
				return 0;
			}

			if (wParam == HSHELL_WINDOWACTIVATED) {
				// If we clicked the taskbar, we don't want to focus on the window
				if (GetForegroundWindow() == g_Taskbar->hWnd) return 0;

				for (auto& [_, window] : g_Taskbar->windows) {
					window.isFocused = false;
				}

				auto it = g_Taskbar->windows.find((HWND)lParam);
				if (it != g_Taskbar->windows.end()) {
					it->second.isFocused = true;
				}
			}

			if (wParam == HSHELL_WINDOWREPLACING) {
				auto it = g_Taskbar->windows.find((HWND)lParam);
				if (it != g_Taskbar->windows.end()) {
					it->second.isFocused = false;
				}

				return 0;
			}

			if (wParam == HSHELL_WINDOWREPLACED) {
				auto it = g_Taskbar->windows.find((HWND)lParam);
				if (it != g_Taskbar->windows.end()) {
					it->second.isFocused = true;
				}
				return 0;
			}

			return 0;
		}
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// TODO: More slow code, optimize this or find a better way
LRESULT CALLBACK Taskbar::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* kbd = (KBDLLHOOKSTRUCT*)lParam;

		if (kbd->vkCode == VK_LWIN && wParam == WM_KEYUP) {
			static std::chrono::time_point<std::chrono::system_clock> lastPress = std::chrono::system_clock::now() - std::chrono::milliseconds(500);
			for (int i = 0; i < 256; i++) {
				if (GetAsyncKeyState(i) & 0x8000 && i != VK_LWIN) {
					lastPress = std::chrono::system_clock::now();
					break;
				}
			}

			if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - lastPress).count() > 250)
				g_Taskbar->showStartMenu = !g_Taskbar->showStartMenu;
		}
	}
	return CallNextHookEx(g_Taskbar->kbdHook, nCode, wParam, lParam);
}