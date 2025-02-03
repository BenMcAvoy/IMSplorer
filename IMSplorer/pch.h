#pragma once
#pragma comment(lib, "d3d11.lib")

// Windows
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winuser.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <psapi.h>

// GUI
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>

// Libs
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// STD/STL
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>
#include <chrono>
