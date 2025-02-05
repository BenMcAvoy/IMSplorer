#include "pch.h"

#include "taskbar.h"

// WinMain
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(pCmdLine);

	// Open a console window
	AllocConsole();
	FILE* fDummy;
	freopen_s(&fDummy, "CONOUT$", "w", stdout);

	auto logger = spdlog::stdout_color_mt("console");
	logger->set_pattern("[%H:%M:%S] [%^%l%$] [thread %t] %v"); // [year-month-day hour:minute:second] [log level] [thread id] log message
	logger->set_level(spdlog::level::trace);
	logger->info("Starting IMSplorer");
	spdlog::set_default_logger(logger);

	// Check if a window named "IMSplorerTB" already exists
	if (!FindWindowA("ImSplorerTB", nullptr)) {
		IMS::Taskbar taskbar("ImSplorerTB");

		taskbar.Run();
	}
	else {
		// Start a file explorer window
		// TODO: use our own file explorer, ditch Windows Explorer
		ShellExecuteA(nullptr, "open", "explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
	}

	return 0;
}
