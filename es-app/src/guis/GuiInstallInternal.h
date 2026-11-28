#pragma once

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"

#include <mutex>
#include <thread>

class TextComponent;
class ComponentGrid;
class InstallProgressBar;

class GuiInstallInternal : public GuiComponent
{
public:
	GuiInstallInternal(Window* window, const std::string& device, const std::string& options);
	~GuiInstallInternal();

	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	enum class State { RUNNING, DONE, FAILED };

	void threadInstall();
	void showResult();
	void layout();

	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mMessage;
	std::shared_ptr<TextComponent> mStep;
	std::shared_ptr<TextComponent> mPercent;
	std::shared_ptr<InstallProgressBar> mBar;
	std::shared_ptr<ComponentGrid> mButtonGrid;

	std::string mDevice;
	std::string mOptions;
	std::thread* mThread;

	std::mutex mLock;
	std::string mStatusText;
	int mStatusPercent;
	bool mStatusChanged;
	State mState;
	bool mResultShown;
	std::string mLastLine;
};
