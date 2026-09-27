// Minimal UI boundary for the fixed-content Vita MVC2 build. Keep ImGui's
// context only because the existing OpenGL presenter still initializes its
// ImGui backend; no menu, scanner, box art or font atlas is constructed.
#include "gui.h"
#include "imgui_driver.h"
#include "mainui.h"
#include "log/LogManager.h"
#include <cstdarg>
#include <cstdio>

std::unique_ptr<ImGuiDriver> imguiDriver;
GuiState gui_state = GuiState::Closed;

void gui_init() { ImGui::CreateContext(); }
void gui_initFonts() {}
void gui_term() { ImGui::DestroyContext(); }
void gui_open_settings() {}
void gui_display_ui() {}
void gui_display_osd() {}
void gui_open_onboarding() {}
void gui_cancel_load() {}
void gui_refresh_files() {}
void gui_cheats() {}
void gui_keyboard_input(u16) {}
void gui_keyboard_inputUTF8(const std::string&) {}
void gui_keyboard_key(u8, bool, u8) {}
bool gui_keyboard_captured() { return false; }
bool gui_mouse_captured() { return false; }
void gui_set_mouse_position(int, int) {}
void gui_set_mouse_button(int, bool) {}
void gui_set_mouse_wheel(float) {}
void gui_set_insets(int, int, int, int) {}
void gui_setOnScreenKeyboardCallback(void (*)(bool)) {}
void gui_save() {}

void gui_display_notification(const char *msg, int)
{
	INFO_LOG(COMMON, "%s", msg);
}

void gui_stop_game(const std::string& message)
{
	ERROR_LOG(BOOT, "MVC2 stopped: %s", message.c_str());
	mainui_stop();
}

void gui_start_game(const std::string&)
{
	WARN_LOG(BOOT, "Content switching is unavailable in MVC2 standalone");
}

void gui_error(const std::string& what)
{
	ERROR_LOG(BOOT, "%s", what.c_str());
}

void fatal_error(const char *format, ...)
{
	char message[2048];
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	ERROR_LOG(COMMON, "%s", message);
}
