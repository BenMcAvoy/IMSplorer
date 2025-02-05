#pragma once

#include "pch.h"

namespace IMS {
	struct Window {
		std::string exe;
		bool isFocused = false;
	};

	class Taskbar {
	public:
		Taskbar(const std::string& title);
		~Taskbar();

		void Run();

		int width = 0;
		int height = 0;

	//private:
		HHOOK kbdHook = nullptr;

		static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

		void InitWindow();
		void InitShell() const;
		void InitD3D();
		void InitImGui();

		// DX11
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* deviceContext = nullptr;
		IDXGISwapChain* swapChain = nullptr;
		ID3D11RenderTargetView* renderTargetView = nullptr;

		// ImGui
		ImGuiContext* imguiContext = nullptr;
		ImGuiIO* imguiIO = nullptr;
		ImGuiStyle* imguiStyle = nullptr;

		// Window
		HWND hWnd = nullptr;
		WNDCLASSEX wc = { 0 };
		std::string title = "";

		// Window properties
		int resizeWidth = 0;
		int resizeHeight = 0;
		bool occluded = false;

		// Window flags
		bool isRunning = false;

		// TB data
		std::unordered_map<HWND, Window> windows;
		bool showStartMenu = false;
		float tbHeight = 48.0f;
	};
} // namespace IMS
