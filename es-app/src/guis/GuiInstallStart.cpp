#include "guis/GuiInstallStart.h"

#include "ApiSystem.h"
#include "components/OptionListComponent.h"
#include "guis/GuiInstall.h"
#include "guis/GuiInstallInternal.h"
#include "views/ViewController.h"
#include "utils/StringUtil.h"
#include "LocaleES.h"
#include "guis/GuiMsgBox.h"
#include "components/SwitchComponent.h"
#include "components/SliderComponent.h"
#include "guis/GuiSettings.h"

GuiInstallStart::GuiInstallStart(Window* window) : GuiComponent(window),
mMenu(window, _("INSTALL ON A NEW DISK").c_str())
{
	addChild(&mMenu);

	std::vector<std::string> availableStorage = ApiSystem::getInstance()->getAvailableInstallDevices();
	std::vector<std::string> availableArchitecture = ApiSystem::getInstance()->getAvailableInstallArchitectures();
	std::string runningBoard = ApiSystem::getInstance()->getRunningBoard();

	// the internal Android storage is installed from the running system, no network needed
	bool installationPossible = (availableArchitecture.size() != 0 || availableStorage.size() != 0);

	// available install storage
	if (installationPossible) 
	{
		moptionsStorage = std::make_shared<OptionListComponent<std::string> >(window, _("TARGET DEVICE"), false);
		moptionsStorage->add(_("SELECT"), "", true);

		for (auto it = availableStorage.begin(); it != availableStorage.end(); it++) 
		{
			std::vector<std::string> tokens = Utils::String::split(*it, ' ');

			if (tokens.size() >= 2) {
				// concatenat the ending words
				std::string vname = "";
				for (unsigned int i = 1; i < tokens.size(); i++) {
					if (i > 1) vname += " ";
					vname += tokens.at(i);
				}
				moptionsStorage->add(vname, tokens.at(0), false);
			}
		}
		
		mMenu.addWithLabel(_("TARGET DEVICE"), moptionsStorage);
	
		// available install architecture
		moptionsArchitecture = std::make_shared<OptionListComponent<std::string> >(window, _("TARGET ARCHITECTURE"), false);
		moptionsArchitecture->add(_("SELECT"), "", false);

		for (auto it = availableArchitecture.begin(); it != availableArchitecture.end(); it++)
			moptionsArchitecture->add(*it, *it, *it == runningBoard);
		if (!(moptionsArchitecture->hasSelection()))
			moptionsArchitecture->selectFirstItem();

		if (availableArchitecture.size() != 0)
			mMenu.addWithLabel(_("TARGET ARCHITECTURE"), moptionsArchitecture);

		moptionsValidation = std::make_shared<SwitchComponent>(mWindow);
		mMenu.addWithLabel(_("ARE YOU SURE?"), moptionsValidation);
		
		mMenu.addButton(_("INSTALL"), "install", std::bind(&GuiInstallStart::start, this));
		mMenu.addButton(_("BACK"), "back", [&] { delete this; });
	}
	else
		mMenu.addButton(_("NETWORK REQUIRED"), "back", [&] { delete this; });	

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);
	else
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.1f);
}

void GuiInstallStart::start()
{
	if (!moptionsStorage->hasSelection() || moptionsStorage->getSelected() == "" || !moptionsValidation->getState())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("INVALID PARAMETERS")));
		return;
	}

	std::string device = moptionsStorage->getSelected();
	auto info = ApiSystem::getInstance()->getInstallDiskInfo(device);

	if (info["TYPE"] == "android_internal")
	{
		if (info.find("MAX_ANDROID_GB") == info.cend())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow, _("THE INTERNAL STORAGE CANNOT BE INSTALLED TO")));
			return;
		}

		auto msg = new GuiMsgBox(mWindow,
			_("THIS DISK CONTAINS ANDROID") + "\n\n" +
			_("BATOCERA WILL BE INSTALLED ALONGSIDE ANDROID, NOT REPLACE IT. THE ANDROID USERDATA PARTITION WILL BE SHRUNK AND WIPED: APPS, GAMES, PHOTOS AND ANY OTHER FILES STORED IN ANDROID WILL BE LOST. ANDROID ITSELF STAYS INSTALLED AND WILL START AS A NEWLY RESET DEVICE.") + "\n\n" +
			_("BACK UP ANYTHING YOU WANT TO KEEP BEFORE CONTINUING.") + "\n\n" +
			_("CONTINUE?"),
			_("YES"), [this, device, info] { startInternal(device, info); },
			_("NO"), nullptr,
			ICON_WARNING);
		msg->setDefaultButton(1);
		mWindow->pushGui(msg);
		return;
	}

	if (!moptionsArchitecture->hasSelection() || moptionsArchitecture->getSelected() == "")
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("NETWORK REQUIRED")));
		return;
	}

	mWindow->pushGui(new GuiInstall(mWindow, device, moptionsArchitecture->getSelected()));
	delete this;
}

void GuiInstallStart::startInternal(const std::string& device, std::map<std::string, std::string> info)
{
	int androidGb = Utils::String::toInteger(info["ANDROID_GB"]);
	int minGb = Utils::String::toInteger(info["MIN_ANDROID_GB"]);
	int maxGb = Utils::String::toInteger(info["MAX_ANDROID_GB"]);
	unsigned long totalKb = std::stoul("0" + info["TOTAL_USERDATA_KB"]);
	unsigned long essentialKb = std::stoul("0" + info["ESSENTIAL_USERDATA_KB"]);

	Window* window = mWindow;
	auto s = new GuiSettings(window, _("INSTALL TO INTERNAL STORAGE"));

	s->addGroup(_("ANDROID"));

	auto androidSize = std::make_shared<SliderComponent>(window, (float)minGb, (float)maxGb, 1.f, "GB");
	androidSize->setValue((float)std::max(minGb, std::min(maxGb, 16)));
	s->addWithDescription(_("ANDROID STORAGE SIZE"), _("Android user data is erased, Batocera uses the rest."), androidSize);

	s->addGroup(_("DATA"));

	auto copyData = std::make_shared<OptionListComponent<std::string>>(window, _("COPY FROM THE SD CARD"), false);
	copyData->add(_("EVERYTHING") + " (" + Utils::FileSystem::kiloBytesToString(totalKb) + ")", "all", false);
	copyData->add(_("SETTINGS AND BIOS ONLY") + " (" + Utils::FileSystem::kiloBytesToString(essentialKb) + ")", "bios", true);
	copyData->add(_("NOTHING"), "none", false);
	s->addWithLabel(_("COPY FROM THE SD CARD"), copyData);

	s->addEntry(_("INSTALL"), false, [this, window, s, device, androidGb, androidSize, copyData, totalKb, essentialKb]
	{
		if (!ApiSystem::getInstance()->isInstallPowerConnected())
		{
			window->pushGui(new GuiMsgBox(window, _("PLUG IN THE CHARGER BEFORE INSTALLING TO THE INTERNAL STORAGE.")));
			return;
		}

		int size = (int)androidSize->getValue();
		std::string copy = copyData->getSelected();
		unsigned long needKb = copy == "all" ? totalKb : copy == "bios" ? essentialKb : 0;
		unsigned long shareKb = (unsigned long)std::max(0, androidGb - size - 8) * 1024 * 1024;
		if (needKb > shareKb)
		{
			window->pushGui(new GuiMsgBox(window, _("THE SELECTED DATA DOES NOT FIT ON THE INTERNAL STORAGE.")));
			return;
		}

		std::string options = "--android-size " + std::to_string(size) + " --copy-data " + copy;

		std::string copyText = _("NOTHING");
		if (copy == "all")
			copyText = _("EVERYTHING") + " (" + Utils::FileSystem::kiloBytesToString(totalKb) + ")";
		else if (copy == "bios")
			copyText = _("SETTINGS AND BIOS ONLY") + " (" + Utils::FileSystem::kiloBytesToString(essentialKb) + ")";

		std::string summary =
			_("ANDROID") + ": " + std::to_string(size) + " GB  -  BATOCERA: 8 GB  -  SHARE: " + std::to_string(androidGb - size - 8) + " GB\n" +
			_("COPY FROM THE SD CARD") + ": " + copyText + "\n\n" +
			_("START THE INSTALL?");

		window->pushGui(new GuiMsgBox(window, summary,
			_("YES"), [this, window, s, device, options]
			{
				window->pushGui(new GuiInstallInternal(window, device, options));
				s->close();
				delete this;
			},
			_("NO"), nullptr));
	});

	window->pushGui(s);
}

bool GuiInstallStart::input(InputConfig* config, Input input)
{
	bool consumed = GuiComponent::input(config, input);
	if(consumed)
		return true;
	
	if(input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
	{
		delete this;
		return true;
	}

	if(config->isMappedTo("start", input) && input.value != 0)
	{
		// close everything
		Window* window = mWindow;
		while(window->peekGui() && window->peekGui() != ViewController::get())
			delete window->peekGui();
	}


	return false;
}

std::vector<HelpPrompt> GuiInstallStart::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	prompts.push_back(HelpPrompt("start", _("CLOSE")));
	return prompts;
}
