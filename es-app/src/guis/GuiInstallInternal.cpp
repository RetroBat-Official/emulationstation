#include "guis/GuiInstallInternal.h"

#include "components/ButtonComponent.h"
#include "components/ComponentGrid.h"
#include "components/MenuComponent.h"
#include "components/TextComponent.h"
#include "utils/Platform.h"
#include "utils/StringUtil.h"
#include "ApiSystem.h"
#include "LocaleES.h"
#include "Log.h"
#include "PowerSaver.h"
#include "Window.h"

class InstallProgressBar : public GuiComponent
{
public:
	InstallProgressBar(Window* window) : GuiComponent(window), mValue(0) { }

	void setValue(int value) { mValue = std::max(0, std::min(value, 100)); }

	void render(const Transform4x4f& parentTrans) override
	{
		if (!isVisible())
			return;

		Transform4x4f trans = parentTrans * getTransform();
		Renderer::setMatrix(trans);

		auto color = ThemeData::getMenuTheme()->Text.color;
		Renderer::drawRect(0.f, 0.f, mSize.x(), mSize.y(), (color & 0xFFFFFF00) | 0x30);
		Renderer::drawRect(0.f, 0.f, mSize.x() * mValue / 100.0f, mSize.y(), color);
	}

private:
	int mValue;
};

GuiInstallInternal::GuiInstallInternal(Window* window, const std::string& device, const std::string& options)
	: GuiComponent(window), mBackground(window, ":/frame.png"), mDevice(device), mOptions(options), mThread(nullptr),
	mStatusPercent(-1), mStatusChanged(false), mState(State::RUNNING), mResultShown(false)
{
	auto theme = ThemeData::getMenuTheme();

	mBackground.setImagePath(theme->Background.path);
	mBackground.setEdgeColor(theme->Background.color);
	mBackground.setCenterColor(theme->Background.centerColor);
	mBackground.setCornerSize(theme->Background.cornerSize);
	mBackground.setPostProcessShader(theme->Background.menuShader);

	mTitle = std::make_shared<TextComponent>(window, _("INSTALL TO INTERNAL STORAGE"), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mMessage = std::make_shared<TextComponent>(window, _("DO NOT TURN OFF THIS DEVICE") + "\n" + _("MIGRATION IN PROGRESS"), theme->Text.font, theme->Text.color, ALIGN_CENTER);
	mStep = std::make_shared<TextComponent>(window, _("STARTING..."), theme->Text.font, theme->Text.color, ALIGN_LEFT);
	mPercent = std::make_shared<TextComponent>(window, "", theme->Text.font, theme->Text.color, ALIGN_RIGHT);
	mBar = std::make_shared<InstallProgressBar>(window);
	mBar->setVisible(false);

	addChild(&mBackground);
	addChild(mTitle.get());
	addChild(mMessage.get());
	addChild(mStep.get());
	addChild(mPercent.get());
	addChild(mBar.get());

	layout();

	PowerSaver::pause();

	LOG(LogInfo) << "GuiInstallInternal: installing to " << mDevice << " " << mOptions;
	mThread = new std::thread(&GuiInstallInternal::threadInstall, this);
}

GuiInstallInternal::~GuiInstallInternal()
{
	if (mThread != nullptr)
	{
		mThread->join();
		delete mThread;
	}

	PowerSaver::resume();
}

void GuiInstallInternal::layout()
{
	auto theme = ThemeData::getMenuTheme();

	const float width = Renderer::getScreenWidth() * (Renderer::isSmallScreen() ? 0.9f : 0.6f);
	const float padding = Renderer::getScreenWidth() * 0.02f;
	const float inner = width - 2 * padding;
	const float line = theme->Text.font->getHeight();

	float y = padding / 2;

	mTitle->setSize(width, theme->Title.font->getHeight() * 1.5f);
	mTitle->setPosition(0, y);
	y += mTitle->getSize().y() + padding / 2;

	mMessage->setSize(inner, 0);
	mMessage->setPosition(padding, y);
	y += mMessage->getSize().y() + padding;

	if (mStep->isVisible())
	{
		mStep->setSize(inner * 0.8f, line);
		mStep->setPosition(padding, y);
		mPercent->setSize(inner * 0.2f, line);
		mPercent->setPosition(padding + inner * 0.8f, y);
		y += line + padding / 4;

		mBar->setSize(inner, line * 0.35f);
		mBar->setPosition(padding, y);
		y += mBar->getSize().y() + padding;
	}

	if (mButtonGrid != nullptr)
	{
		mButtonGrid->setPosition((width - mButtonGrid->getSize().x()) / 2, y);
		y += mButtonGrid->getSize().y() + padding / 2;
	}

	setSize(width, y);
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiInstallInternal::threadInstall()
{
	auto result = ApiSystem::getInstance()->installSystem([this](const std::string info)
	{
		std::unique_lock<std::mutex> lock(mLock);

		auto pos = info.find(">>>");
		if (pos != std::string::npos)
		{
			std::string percent = Utils::String::replace(Utils::String::replace(info.substr(pos + 3), "%", ""), " ", "");
			mStatusText = Utils::String::trim(info.substr(0, pos));
			mStatusPercent = atoi(percent.c_str());
		}
		else if (!Utils::String::trim(info).empty())
		{
			mStatusText = Utils::String::trim(info);
			mStatusPercent = -1;
			mLastLine = mStatusText;
		}
		mStatusChanged = true;
	}, mDevice, "internal", mOptions);

	LOG(LogInfo) << "GuiInstallInternal: finished with exit code " << result.second;

	std::unique_lock<std::mutex> lock(mLock);
	mState = result.second == 0 ? State::DONE : State::FAILED;
}

void GuiInstallInternal::showResult()
{
	mResultShown = true;
	mStep->setVisible(false);
	mPercent->setVisible(false);
	mBar->setVisible(false);

	std::vector<std::shared_ptr<ButtonComponent>> buttons;

	if (mState == State::DONE)
	{
		mMessage->setText(_("BATOCERA HAS BEEN INSTALLED TO THE INTERNAL STORAGE.") + "\n\n" +
			_("SHUT DOWN, THEN REMOVE THE SD CARD BEFORE STARTING AGAIN."));

		buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("SHUT DOWN"), _("SHUT DOWN"), []
		{
			Utils::Platform::quitES(Utils::Platform::QuitMode::SHUTDOWN);
		}));
		buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("LATER"), _("LATER"), [this] { delete this; }));
	}
	else
	{
		std::string error = mLastLine;
		if (Utils::String::startsWith(error, "ERROR: "))
			error = error.substr(7);
		else
			error = _("check the system/logs directory");

		mMessage->setText(_("AN ERROR OCCURRED") + "\n\n" + error);
		buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("OK"), _("OK"), [this] { delete this; }));
	}

	mButtonGrid = makeButtonGrid(mWindow, buttons);
	addChild(mButtonGrid.get());

	layout();
	updateHelpPrompts();
}

void GuiInstallInternal::update(int deltaTime)
{
	{
		std::unique_lock<std::mutex> lock(mLock);

		if (mStatusChanged)
		{
			mStatusChanged = false;
			mStep->setText(mStatusText);
			mPercent->setText(mStatusPercent >= 0 ? std::to_string(mStatusPercent) + "%" : "");
			mBar->setVisible(mStatusPercent >= 0);
			mBar->setValue(mStatusPercent);
		}
	}

	if (!mResultShown && mState != State::RUNNING)
	{
		showResult();
		return;
	}

	// the screensaver would hide the progress of a long copy
	if (!mResultShown)
		mWindow->cancelScreenSaver();

	GuiComponent::update(deltaTime);
}

void GuiInstallInternal::render(const Transform4x4f& parentTrans)
{
	Renderer::setMatrix(parentTrans);
	Renderer::drawRect(0.f, 0.f, (float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(), 0x000000A0);

	Transform4x4f trans = parentTrans * getTransform();
	renderChildren(trans);
}

bool GuiInstallInternal::input(InputConfig* config, Input input)
{
	// nothing may interrupt the install, and back does not dismiss the result
	if (mButtonGrid != nullptr && !config->isMappedTo(BUTTON_BACK, input))
		mButtonGrid->input(config, input);

	return true;
}

std::vector<HelpPrompt> GuiInstallInternal::getHelpPrompts()
{
	if (mButtonGrid != nullptr)
		return mButtonGrid->getHelpPrompts();

	return std::vector<HelpPrompt>();
}
