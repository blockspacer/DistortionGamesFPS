#include "stdafx.h"
#include "ServerGame.h"
#include "ServerInGameState.h"
#include <TimerManager.h>
#include "ServerNetworkManager.h"
#include "NetworkMessageTypes.h"
#include "PostMaster.h"

ServerGame::ServerGame()
{
}

ServerGame::~ServerGame()
{
	CU::TimerManager::Destroy();
	ServerNetworkManager::Destroy();
	myStateStack.Clear();
	PostMaster::Destroy();
}

bool ServerGame::Init()
{
	PostMaster::Create();
	CU::TimerManager::Create();
	ServerNetworkManager::Create();
	ServerNetworkManager::GetInstance()->StartNetwork();
	myStateStack.PushMainState(new ServerInGameState());
	return true;
}

bool ServerGame::Update()
{
	CU::TimerManager::GetInstance()->Update();

	myDeltaTime = CU::TimerManager::GetInstance()->GetMasterTimer().GetTime().GetFrameTime();

	if (myStateStack.UpdateCurrentState(myDeltaTime) == false)
	{
		return false;
	}
	ServerNetworkManager::GetInstance()->Update(myDeltaTime);


	ServerNetworkManager::GetInstance()->MainIsDone();
	ServerNetworkManager::GetInstance()->WaitForReceieve();
	return true;
}