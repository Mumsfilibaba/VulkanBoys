#include "IWindow.h"
#include "GLFWWindow.h"

IWindow* IWindow::create(const std::string& title, uint32_t width, uint32_t height)
{
	return new GLFWWindow(title, width, height);
}