#include "stdafx.h"

#include <AudioInterface.h>
#include "ClientLevel.h"
#include "ClientLevelFactory.h"
#include "Console.h"
#include <CommonHelper.h>
#include <GameStateMessage.h>
#include <EffectContainer.h>
#include <FadeMessage.h>
#include "InGameState.h"
#include <InputWrapper.h>
#include <ModelLoader.h>
#include <MurmurHash3.h>
#include "ServerSelectState.h"
#include <NetMessageAllClientsComplete.h>
#include <NetMessageDisconnect.h>
#include <NetMessageLevelComplete.h>
#include <NetMessageLevelLoaded.h>
#include <NetMessageLoadLevel.h>
#include <NetMessageSetLevel.h>
#include <NetMessageRequestStartLevel.h>
#include <PostMaster.h>
#include <ScriptSystem.h>
#include <VTuneApi.h>
#include <TextProxy.h>
#include <SpriteProxy.h>
#include <Cursor.h>
#include "ClientNetworkManager.h"

InGameState::InGameState(int aLevelID, unsigned int aServerHashLevelValue)
	: myGUIManager(nullptr)
	, myLevelToLoad(aLevelID)
	, myShouldLoadLevel(true)
	, myLevel(nullptr)
	, myLevelComplete(false)
	, myCanStartNextLevel(false)
	, myFailedLevelHash(false)
	, myServerHashLevelValue(aServerHashLevelValue)
	, myHasStartedMusicBetweenLevels(false)
	, myLastLevel(0)
{
	myIsActiveState = false;
	myLevelFactory = new ClientLevelFactory("Data/Level/LI_level.xml");
	myHashLevelValue = Hash(CU::ReadFileToString(myLevelFactory->GetLevelPath(aLevelID)).c_str());
	
	
	myElevatorSprite = Prism::ModelLoader::GetInstance()->LoadSprite(
		"Data/Resource/Texture/Menu/BetweenLevels/T_background_elevator.dds", { 1920.f, 1080.f });
	CU::Vector2<int> windowSize = Prism::Engine::GetInstance()->GetWindowSizeInt();
	OnResize(windowSize.x, windowSize.y);
}

InGameState::~InGameState()
{
	PostMaster::GetInstance()->UnSubscribe(eMessageType::GAME_STATE, this);
	ClientNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::LEVEL_COMPLETE, this);
	ClientNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::ALL_CLIENTS_COMPLETE, this);
	ClientNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::LOAD_LEVEL, this);
	Console::Destroy();
	SAFE_DELETE(myLevel);
	SAFE_DELETE(myLevelFactory);
	SAFE_DELETE(myText);
}

void InGameState::InitState(StateStackProxy* aStateStackProxy, GUI::Cursor* aCursor)
{
	myIsLetThrough = false;
	myStateStack = aStateStackProxy;
	myStateStatus = eStateStatus::eKeepState;
	myCursor = aCursor;
	myCursor->SetShouldRender(false);

	//PostMaster::GetInstance()->SendMessage(RunScriptMessage("Data/Script/Autorun.script"));
	//myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(myLevelToLoad));

	myIsActiveState = true;

	myText = Prism::ModelLoader::GetInstance()->LoadText(Prism::Engine::GetInstance()->GetFont(Prism::eFont::CONSOLE));
	myText->SetPosition(CU::Vector2<float>(800.f, 800.f));

	PostMaster::GetInstance()->Subscribe(eMessageType::GAME_STATE, this);
	ClientNetworkManager::GetInstance()->Subscribe(eNetMessageType::LEVEL_COMPLETE, this);
	ClientNetworkManager::GetInstance()->Subscribe(eNetMessageType::ALL_CLIENTS_COMPLETE, this);
	ClientNetworkManager::GetInstance()->Subscribe(eNetMessageType::LOAD_LEVEL, this);

	if (myHashLevelValue != myServerHashLevelValue)
	{
		ClientNetworkManager::GetInstance()->AddMessage(NetMessageDisconnect());
		myStateStatus = eStateStatus::ePopMainState;
		myFailedLevelHash = true;
		DL_MESSAGE_BOX("Level don't match the server level. Please update and try again.", "Failed to play level.", MB_OK | MB_ICONWARNING);
	}
}

void InGameState::EndState()
{
}

const eStateStatus InGameState::Update(const float& aDeltaTime)
{
	if (myFailedLevelHash == true)
	{
		return eStateStatus::ePopMainState;
	}
	Prism::EffectContainer::GetInstance()->Update(aDeltaTime);

	if (CU::InputWrapper::GetInstance()->KeyDown(DIK_ESCAPE))
	{
		return eStateStatus::ePopMainState;
	}

	//if (CU::InputWrapper::GetInstance()->KeyDown(DIK_NUMPAD1))
	//{
	//	ClientNetworkManager::GetInstance()->AddMessage(NetMessageSetLevel(0));
	//	//SET_RUNTIME(false);
	//	//SAFE_DELETE(myLevel);
	//	//myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(0));
	//}
	//else if (CU::InputWrapper::GetInstance()->KeyDown(DIK_NUMPAD2))
	//{
	//	ClientNetworkManager::GetInstance()->AddMessage(NetMessageSetLevel(1));
	//	//SET_RUNTIME(false);
	//	//SAFE_DELETE(myLevel);
	//	//myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(1));
	//}
	//else if (CU::InputWrapper::GetInstance()->KeyDown(DIK_NUMPAD3))
	//{
	//	ClientNetworkManager::GetInstance()->AddMessage(NetMessageSetLevel(2));
	//	//SET_RUNTIME(false);
	//	//SAFE_DELETE(myLevel);
	//	//myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(2));
	//}



	if (myShouldLoadLevel == true)
	{
		myShouldLoadLevel = false;
		SET_RUNTIME(false);
		SAFE_DELETE(myLevel);
		myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(myLevelToLoad));
		myLevelToLoad = -1;
	}

	if (myLevelComplete == true)
	{
		DEBUG_PRINT("LEVEL COMPLETE");
		myElevatorSprite->Render({ 0.f, 0.f });
		if (myHasStartedMusicBetweenLevels == false)
		{
			int levelMusic = myLastLevel + 1;
			std::string musicEvent("ElevatorToLevel" + std::to_string(levelMusic));
			myHasStartedMusicBetweenLevels = true;
			Prism::Audio::AudioInterface::GetInstance()->PostEvent(musicEvent.c_str(), 0);
		}

		if (myCanStartNextLevel == true)
		{
			DEBUG_PRINT("Press space to continue!");
			myText->SetText("Level Complete or Game Over\nPress space to continue");
			if (CU::InputWrapper::GetInstance()->KeyDown(DIK_SPACE) == true)
			{
				ClientNetworkManager::GetInstance()->AddMessage(NetMessageRequestStartLevel());
			}
		}
		else
		{
			myText->SetText("Level Complete or Game Over");
		}
	}
	else
	{
		DL_ASSERT_EXP(myLevel != nullptr, "Invalid level");
		myLevel->Update(aDeltaTime);
	}

	//LUA::ScriptSystem::GetInstance()->CallFunction("Update", { aDeltaTime });
	//LUA::ScriptSystem::GetInstance()->Update();

	return myStateStatus;
}

void InGameState::Render()
{
	VTUNE_EVENT_BEGIN(VTUNE::GAME_RENDER);

	if (myLevelComplete == false)
	{
		myLevel->Render();
	}
	else
	{
		myText->Render();
	}

	VTUNE_EVENT_END();
}

void InGameState::ResumeState()
{
	myIsActiveState = true;
	PostMaster::GetInstance()->SendMessage(FadeMessage(1.f / 3.f));
	myLevelToLoad = -1;
}

void InGameState::ReceiveMessage(const GameStateMessage& aMessage)
{
	switch (aMessage.myGameState)
	{
	case eGameState::LOAD_LEVEL:
		myLevelToLoad = aMessage.myID;
		break;
	}
}

void InGameState::ReceiveNetworkMessage(const NetMessageAllClientsComplete& aMessage, const sockaddr_in&)
{
	DL_ASSERT_EXP(aMessage.myType == NetMessageAllClientsComplete::eType::LEVEL_COMPLETE
		|| aMessage.myType == NetMessageAllClientsComplete::eType::LEVEL_LOAD, "Unknown All clients complete message type");

	switch (aMessage.myType)
	{
	case NetMessageAllClientsComplete::eType::LEVEL_COMPLETE:
 		myCanStartNextLevel = true;
		break;
	case NetMessageAllClientsComplete::eType::LEVEL_LOAD:
		myLevelComplete = false;
		break;
	}
}

void InGameState::ReceiveNetworkMessage(const NetMessageLevelComplete&, const sockaddr_in&)
{
	ClientNetworkManager::GetInstance()->AddMessage(NetMessageLevelComplete());
	ClientNetworkManager::GetInstance()->AllowSendWithoutSubscriber(true);
	SAFE_DELETE(myLevel);
	myLevelComplete = true;
	myHasStartedMusicBetweenLevels = false;
}

void InGameState::ReceiveNetworkMessage(const NetMessageLoadLevel& aMessage, const sockaddr_in&)
{
	if (myLevel != nullptr)
	{
		bool shouldAssertLater = true;
		return;
	}
	//DL_ASSERT_EXP(myLevel == nullptr, "Level has to be nullptr here");
	SET_RUNTIME(false);
	myLevel = static_cast<ClientLevel*>(myLevelFactory->LoadLevel(aMessage.myLevelID));
	myLastLevel = aMessage.myLevelID;
	ClientNetworkManager::GetInstance()->AllowSendWithoutSubscriber(false);
	ClientNetworkManager::GetInstance()->AddMessage(NetMessageLevelLoaded());
}

void InGameState::OnResize(int aWidth, int aHeight)
{
	myElevatorSprite->SetSize({ float(aWidth), float(aHeight )}, { 0.f, 0.f });
}