#pragma once

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include <map>

template<typename T>
class OptionListComponent;

class SwitchComponent;

class GuiInstallStart : public GuiComponent
{
public:
	GuiInstallStart(Window* window);
	bool input(InputConfig* config, Input input) override;

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void start();
	void startInternal(const std::string& device, std::map<std::string, std::string> info);

	MenuComponent mMenu;
	std::shared_ptr< OptionListComponent<std::string> >moptionsStorage;
	std::shared_ptr< OptionListComponent<std::string> >moptionsArchitecture;
	std::shared_ptr<SwitchComponent> moptionsValidation;
};
