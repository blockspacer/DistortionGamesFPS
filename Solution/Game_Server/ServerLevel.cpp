#include "stdafx.h"
#include "ServerLevel.h"

#include <CollisionNote.h>
#include <DefendTouchMessage.h>
#include <Entity.h>
#include <EntityFactory.h>
#include <GrenadeComponent.h>
#include <HealthComponent.h>
#include "MissionManager.h"
#include <NetMessageLevelLoaded.h>
#include <NetMessageSetActive.h>
#include <NetMessageHealthPack.h>
#include <NetMessageEntityState.h>
#include <NetMessageRayCastRequest.h>
#include <NetMessagePressE.h>
#include <NetMessageShootGrenade.h>
#include <NetMessageText.h>
#include <NetMessagePressEText.h>
#include <NetworkComponent.h>
#include <PhysicsInterface.h>
#include <PhysicsComponent.h>
#include <BulletComponent.h>
#include "ServerNetworkManager.h"
#include <TriggerComponent.h>
#include <PollingStation.h>
#include <PostMaster.h>
#include <SetActiveMessage.h>
#include <RespawnMessage.h>
#include <RespawnTriggerMessage.h>
#include <ActivateSpawnpointMessage.h>
#include <SendTextToClientsMessage.h>

#include "ServerProjectileManager.h"
#include "ServerUnitManager.h"

ServerLevel::ServerLevel()
	: myLoadedClients(16)
	, myAllClientsLoaded(false)
	, myNextLevel(-1)
	, myRespawnTriggers(16)
	, myPressETriggers(16)
{
	Prism::PhysicsInterface::Create(std::bind(&ServerLevel::CollisionCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), true
		, std::bind(&SharedLevel::ContactCallback, this, std::placeholders::_1, std::placeholders::_2));

	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::ON_CONNECT, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::LEVEL_LOADED, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::ENTITY_STATE, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::SHOOT_GRENADE, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::HEALTH_PACK, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::RAY_CAST_REQUEST, this);
	ServerNetworkManager::GetInstance()->Subscribe(eNetMessageType::PRESS_E, this);

	PostMaster::GetInstance()->Subscribe(eMessageType::SET_ACTIVE, this);
	PostMaster::GetInstance()->Subscribe(eMessageType::RESPAWN_TRIGGER, this);
	PostMaster::GetInstance()->Subscribe(eMessageType::RESPAWN, this);
	PostMaster::GetInstance()->Subscribe(eMessageType::SEND_TEXT_TO_CLIENTS, this);

	ServerProjectileManager::Create();
	ServerUnitManager::Create();

	//myPressedERayCastHandler = [=](PhysicsComponent* aComponent, const CU::Vector3<float>& aDirection, const CU::Vector3<float>& aHitPosition, const CU::Vector3<float>& aHitNormal)
	//{
	//	this->HandlePressedERaycast(aComponent, aDirection, aHitPosition, aHitNormal);
	//};
}

ServerLevel::~ServerLevel()
{
#ifdef THREAD_PHYSICS
	Prism::PhysicsInterface::GetInstance()->ShutdownThread();
#endif
	ServerProjectileManager::Destroy();
	ServerUnitManager::Destroy();
	PollingStation::Destroy();
	SAFE_DELETE(myMissionManager);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::ON_CONNECT, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::LEVEL_LOADED, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::ENTITY_STATE, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::SHOOT_GRENADE, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::HEALTH_PACK, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::RAY_CAST_REQUEST, this);
	ServerNetworkManager::GetInstance()->UnSubscribe(eNetMessageType::PRESS_E, this);

	PostMaster::GetInstance()->UnSubscribe(eMessageType::SET_ACTIVE, this);
	PostMaster::GetInstance()->UnSubscribe(eMessageType::RESPAWN_TRIGGER, this);
	PostMaster::GetInstance()->UnSubscribe(eMessageType::RESPAWN, this);
	PostMaster::GetInstance()->UnSubscribe(eMessageType::SEND_TEXT_TO_CLIENTS, this);

	for (auto it = myPlayersInPressableTriggers.begin(); it != myPlayersInPressableTriggers.end(); ++it)
	{
		it->second.RemoveAll();
	}
	myRespawnTriggers.DeleteAll();
	myPressETriggers.DeleteAll();
}

void ServerLevel::Init(const std::string& aMissionXMLPath)
{
	for (int i = 0; i < ServerNetworkManager::GetInstance()->GetClients().Size(); ++i)
	{
		const Connection& client = ServerNetworkManager::GetInstance()->GetClients()[i];

		Entity* newPlayer = EntityFactory::CreateEntity(client.myID, eEntityType::UNIT, "playerserver", nullptr, false, myPlayerStartPositions[client.myID]);
		newPlayer->GetComponent<NetworkComponent>()->SetPlayer(true);
		myPlayers.Add(newPlayer);

		Entity* newRespawnTrigger = EntityFactory::CreateEntity(99983 + i, eEntityType::TRIGGER, "respawn", nullptr, false, { 0.f, -1.f, 0.f });
		newRespawnTrigger->GetComponent<TriggerComponent>()->SetRespawnValue(i + 1);
		myRespawnTriggers.Add(newRespawnTrigger);

		myMissionManager = new MissionManager(aMissionXMLPath);

		myPlayersInPressableTriggers[client.myID] = CU::GrowingArray<Entity*>(8);
	}
	ServerProjectileManager::GetInstance()->CreateBullets(nullptr);
	ServerProjectileManager::GetInstance()->CreateGrenades(nullptr);


}

void ServerLevel::Update(const float aDeltaTime, bool aLoadingScreen)
{
	if (aLoadingScreen == true)
	{
		return;
	}

	if (myAllClientsLoaded == true && Prism::PhysicsInterface::GetInstance()->GetInitDone() == true)
	{
		__super::Update(aDeltaTime, aLoadingScreen);

		for (int i = 0; i < myPressETriggers.Size(); ++i)
		{
			ServerNetworkManager::GetInstance()->AddMessage(NetMessagePressEText(myPressETriggers[i]->GetGID(), myPressETriggers[i]->GetOrientation().GetPos(), true));
		}
		myPressETriggers.RemoveAll();

		myMissionManager->Update(aDeltaTime);
		ServerProjectileManager::GetInstance()->Update(aDeltaTime);
		ServerUnitManager::GetInstance()->Update(aDeltaTime);
		//	PollingStation::GetInstance()->FindClosestEntityToEntity(*myPlayers[0]);

		Prism::PhysicsInterface::GetInstance()->EndFrame();
	}

	for each (Entity* trigger in myRespawnTriggers)
	{
		trigger->Update(aDeltaTime);
	}
}

void ServerLevel::CollisionCallback(PhysicsComponent* aFirst, PhysicsComponent* aSecond, bool aHasEntered)
{
	Entity& first = aFirst->GetEntity();
	Entity& second = aSecond->GetEntity();

	switch (first.GetType())
	{
	case eEntityType::TRIGGER:
		HandleTrigger(first, second, aHasEntered);
		break;
	case eEntityType::EXPLOSION:
		if (aHasEntered == true)
		{
			SharedLevel::HandleExplosion(first, second);
		}
		break;
	case eEntityType::BULLET:
		if (second.GetComponent<HealthComponent>() != nullptr && second.GetIsEnemy() == false)
		{
			second.GetComponent<HealthComponent>()->TakeDamage(first.GetComponent<BulletComponent>()->GetDamage());
		}
		first.Kill();
		break;
	}
}

void ServerLevel::ReceiveNetworkMessage(const NetMessageLevelLoaded& aMessage, const sockaddr_in&)
{
	if (myLoadedClients.Find(aMessage.mySenderID) == myLoadedClients.FoundNone)
	{
		myLoadedClients.Add(aMessage.mySenderID);
	}
	if (ServerNetworkManager::GetInstance()->ListContainsAllClients(myLoadedClients) == true)
	{
		myAllClientsLoaded = true;

		//for (Entity* e : myActiveEnemies)
		//{
		//	ServerNetworkManager::GetInstance()->AddMessage(NetMessageAddEnemy(e->GetOrientation().GetPos(), e->GetGID()));
		//}
	}
}

void ServerLevel::ReceiveNetworkMessage(const NetMessageEntityState& aMessage, const sockaddr_in&)
{
	for each (const Connection& client in ServerNetworkManager::GetInstance()->GetClients())
	{
		ServerNetworkManager::GetInstance()->AddMessage<NetMessageEntityState>(aMessage, client.myID);
	}
}

void ServerLevel::ReceiveNetworkMessage(const NetMessageHealthPack& aMessage, const sockaddr_in&)
{
	myPlayers[aMessage.mySenderID - 1]->GetComponent<HealthComponent>()->Heal(aMessage.myHealAmount);
}

void ServerLevel::ReceiveNetworkMessage(const NetMessageShootGrenade& aMessage, const sockaddr_in&)
{
	Entity* bullet = ServerProjectileManager::GetInstance()->RequestGrenade();
	CU::Matrix44<float> playerOrientation = myPlayers[aMessage.myPlayerGID - 1]->GetOrientation();

	bullet->Reset();
	CU::Vector3<float> pos = playerOrientation.GetPos();
	pos.y += 0.3f;
	pos += aMessage.myForwardVector * 1.5f;
	pos += playerOrientation.GetRight() * 0.5f;
	//pos -= playerOrientation.GetUp() * 0.1f;
	bullet->GetComponent<PhysicsComponent>()->TeleportToPosition(pos);
	bullet->GetComponent<GrenadeComponent>()->Activate(aMessage.myPlayerGID);
	bullet->GetComponent<PhysicsComponent>()->AddForce(aMessage.myForwardVector, float(aMessage.myForceStrength));

	//Skicka samma meddelande till clienten
	SharedNetworkManager::GetInstance()->AddMessage<NetMessageShootGrenade>(NetMessageShootGrenade(aMessage.myForceStrength
		, aMessage.myPlayerGID, pos));
}

void ServerLevel::ReceiveNetworkMessage(const NetMessagePressE& aMessage, const sockaddr_in&)
{
	bool errorSound = true;
	for (int i = 0; i < myPlayersInPressableTriggers[aMessage.myGID].Size(); ++i)
	{
		if (myPlayersInPressableTriggers[aMessage.myGID][i]->GetComponent<PhysicsComponent>()->IsInScene() == true)
		{
			Trigger(*myPlayersInPressableTriggers[aMessage.myGID][i], *myPlayers[aMessage.myGID - 1], true);
			myPlayersInPressableTriggers[aMessage.myGID][i]->
				SendNote<CollisionNote>(CollisionNote(myPlayers[aMessage.myGID - 1], true));
			
			errorSound = false;
		}
	}

	if (errorSound == false)
	{
		SharedNetworkManager::GetInstance()->AddMessage<NetMessageText>(
			NetMessageText("", 5.f, { 1.f, 1.f, 1.f, 1.f }, 0, false, true, "Play_Press_E"), aMessage.myGID);
	}
	else
	{
		SharedNetworkManager::GetInstance()->AddMessage<NetMessageText>(
			NetMessageText("", 5.f, { 1.f, 1.f, 1.f, 1.f }, 0, false, true, "Play_Error"), aMessage.myGID);
	}

}

void ServerLevel::ReceiveNetworkMessage(const NetMessageRayCastRequest& aMessage, const sockaddr_in&)
{
	if (static_cast<eNetRayCastType>(aMessage.myRayCastType) == eNetRayCastType::CLIENT_SHOOT_PISTOL ||
		static_cast<eNetRayCastType>(aMessage.myRayCastType) == eNetRayCastType::CLIENT_SHOOT_SHOTGUN)
	{
		ServerNetworkManager::GetInstance()->AddMessage(aMessage);
	}
	else
	{
		DL_ASSERT("NOT IMPLEMENTED");
	}
}

void ServerLevel::ReceiveMessage(const SendTextToClientsMessage& aMessage)
{
	SendTextMessageToClients(aMessage.myText);
}

void ServerLevel::ReceiveMessage(const SetActiveMessage& aMessage)
{
	if (aMessage.myShouldActivate == true)
	{
		myActiveEntitiesMap[aMessage.myGID]->GetComponent<PhysicsComponent>()->AddToScene();
		if (myActiveEntitiesMap[aMessage.myGID]->GetType() == eEntityType::TRIGGER
			&& myActiveEntitiesMap[aMessage.myGID]->GetComponent<TriggerComponent>()->IsPressable() == true)
		{
			ServerNetworkManager::GetInstance()->AddMessage(
				NetMessagePressEText(aMessage.myGID, myActiveEntitiesMap[aMessage.myGID]->GetOrientation().GetPos(), true));
		}
	}
	else
	{
		myActiveEntitiesMap[aMessage.myGID]->GetComponent<PhysicsComponent>()->RemoveFromScene();
		if (myActiveEntitiesMap[aMessage.myGID]->GetType() == eEntityType::TRIGGER
			&& myActiveEntitiesMap[aMessage.myGID]->GetComponent<TriggerComponent>()->IsPressable() == true)
		{
			ServerNetworkManager::GetInstance()->AddMessage(
				NetMessagePressEText(aMessage.myGID, myActiveEntitiesMap[aMessage.myGID]->GetOrientation().GetPos(), false));
		}
	}
}

void ServerLevel::ReceiveMessage(const RespawnMessage &aMessage)
{
	int gid = aMessage.myGID - 1;

	myPlayers[gid]->GetComponent<HealthComponent>()->Heal(myPlayers[gid]->GetComponent<HealthComponent>()->GetMaxHealth());
	myPlayers[gid]->SetState(eEntityState::IDLE);
	SharedNetworkManager::GetInstance()->AddMessage<NetMessageSetActive>(NetMessageSetActive(true, false, gid + 1));
	SharedNetworkManager::GetInstance()->AddMessage<NetMessageEntityState>(NetMessageEntityState(eEntityState::IDLE, gid + 1), gid + 1);
	myRespawnTriggers[gid]->GetComponent<PhysicsComponent>()->RemoveFromScene();
	myPlayers[gid]->GetComponent<PhysicsComponent>()->Wake();
}

void ServerLevel::ReceiveMessage(const RespawnTriggerMessage& aMessage)
{
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<TriggerComponent>()->Activate();
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<TriggerComponent>()->SetRespawnValue(aMessage.myGID);
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<TriggerComponent>()->SetPlayerRespawnPosition(myPlayers[aMessage.myGID - 1]->GetOrientation().GetPos());
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<PhysicsComponent>()->AddToScene();
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<PhysicsComponent>()->TeleportToPosition(myPlayers[aMessage.myGID - 1]->GetOrientation().GetPos());
	myRespawnTriggers[aMessage.myGID - 1]->GetComponent<PhysicsComponent>()->UpdateOrientationStatic();
}

void ServerLevel::AddPressETrigger(Entity* aTrigger)
{
	myPressETriggers.Add(aTrigger);
}

//void ServerLevel::HandlePressedERaycast(PhysicsComponent* aComponent, const CU::Vector3<float>&, const CU::Vector3<float>&, const CU::Vector3<float>&)
//{
//	if (aComponent != nullptr)
//	{
//		//Entity& entity = aComponent->GetEntity();
//
//		
//	}
//}

void ServerLevel::HandleTrigger(Entity& aFirstEntity, Entity& aSecondEntity, bool aHasEntered)
{
	if (aHasEntered == true)
	{
		if (aSecondEntity.GetType() == eEntityType::UNIT && aSecondEntity.GetSubType() == "playerserver")
		{
			if (aFirstEntity.GetComponent<TriggerComponent>()->IsPressable() == false)
			{
				Trigger(aFirstEntity, aSecondEntity, aHasEntered);
			}
			else
			{
				myPlayersInPressableTriggers[aSecondEntity.GetGID()].Add(&aFirstEntity);
			}
		}
		else if (aSecondEntity.GetType() == eEntityType::UNIT && aSecondEntity.GetSubType() != "playerserver")
		{
			if (myMissionManager->GetCurrentMissionType() == eMissionType::DEFEND)
			{
				printf("DefendTrigger with GID: %i entered by: %s with GID: %i \n", aFirstEntity.GetGID(), aSecondEntity.GetSubType().c_str(), aSecondEntity.GetGID());
				PostMaster::GetInstance()->SendMessage<DefendTouchMessage>(DefendTouchMessage(true));
			}
		}
	}
	else
	{
		if (aSecondEntity.GetType() == eEntityType::UNIT && aSecondEntity.GetSubType() == "playerserver" && aFirstEntity.GetComponent<TriggerComponent>()->IsPressable() == true)
		{
			myPlayersInPressableTriggers[aSecondEntity.GetGID()].RemoveCyclic(&aFirstEntity);
		}
		else if (aSecondEntity.GetType() == eEntityType::UNIT && aSecondEntity.GetSubType() != "playerserver")
		{
			if (myMissionManager->GetCurrentMissionType() == eMissionType::DEFEND)
			{
				printf("DefendTrigger with GID: %i exited by: %s with GID: %i \n", aFirstEntity.GetGID(), aSecondEntity.GetSubType().c_str(), aSecondEntity.GetGID());
				PostMaster::GetInstance()->SendMessage<DefendTouchMessage>(DefendTouchMessage(false));
			}
		}
	}
	if (aFirstEntity.GetComponent<TriggerComponent>()->IsPressable() == false)
	{
		aFirstEntity.SendNote<CollisionNote>(CollisionNote(&aSecondEntity, aHasEntered));
	}
}

void ServerLevel::Trigger(Entity& aFirstEntity, Entity& aSecondEntity, bool aHasEntered)
{
	TriggerComponent* firstTrigger = aFirstEntity.GetComponent<TriggerComponent>();

	switch (firstTrigger->GetTriggerType())
	{
	case eTriggerType::UNLOCK:
		printf("UnlockTrigger with GID: %i entered by: %s with GID: %i \n", aFirstEntity.GetGID(), aSecondEntity.GetSubType().c_str(), aSecondEntity.GetGID());
		SharedNetworkManager::GetInstance()->AddMessage(NetMessageSetActive(false, true, firstTrigger->GetValue()));
		myActiveEntitiesMap[firstTrigger->GetValue()]->GetComponent<PhysicsComponent>()->RemoveFromScene();
		// do "open" animation
		//SendTextMessageToClients("Unlocked door");
		break;
	case eTriggerType::LOCK:
		printf("LockTrigger with GID: %i entered by: %s with GID: %i \n", aFirstEntity.GetGID(), aSecondEntity.GetSubType().c_str(), aSecondEntity.GetGID());
		SharedNetworkManager::GetInstance()->AddMessage(NetMessageSetActive(true, true, firstTrigger->GetValue()));
		myActiveEntitiesMap[firstTrigger->GetValue()]->GetComponent<PhysicsComponent>()->AddToScene();
		// do "close" animation
		//SendTextMessageToClients("Locked door");
		break;
	case eTriggerType::MISSION:
		printf("MissionTrigger with GID: %i entered by: %s with GID: %i \n", aFirstEntity.GetGID(), aSecondEntity.GetSubType().c_str(), aSecondEntity.GetGID());
		myMissionManager->SetMission(firstTrigger->GetValue());
		if (myMissionManager->GetCurrentMissionType() == eMissionType::DEFEND)
		{
			PollingStation::GetInstance()->SetEnemyTargetPosition(&aFirstEntity);
		}
		break;
	case eTriggerType::LEVEL_CHANGE:
		myNextLevel = firstTrigger->GetValue();
		break;
	case eTriggerType::ENEMY_SPAWN:
		PostMaster::GetInstance()->SendMessage(ActivateSpawnpointMessage(firstTrigger->GetEntity().GetGID()));
		break;
	}
	aSecondEntity.SendNote<CollisionNote>(CollisionNote(&aFirstEntity, aHasEntered));
}

void ServerLevel::SendTextMessageToClients(const std::string& aText, float)
{
	for each (const Connection& client in ServerNetworkManager::GetInstance()->GetClients())
	{
		ServerNetworkManager::GetInstance()->AddMessage(NetMessageText(aText), client.myID);
	}
}

bool ServerLevel::ChangeLevel(int& aNextLevel)
{
	if (myNextLevel != -1)
	{
		aNextLevel = myNextLevel;
		return true;
	}
	return false;
}

