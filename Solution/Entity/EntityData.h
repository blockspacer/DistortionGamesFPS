#pragma once

#include "AIComponentData.h"
#include "AnimationComponentData.h"
#include "GraphicsComponentData.h"
#include "FirstPersonRenderComponentData.h"
#include "PhysicsComponentData.h"
#include "HealthComponentData.h"
#include "InputComponentData.h"
#include "NetworkComponentData.h"
#include "GrenadeComponentData.h"
#include "ShootingComponentData.h"
#include "TriggerComponentData.h"
#include "UpgradeComponentData.h"
#include "BulletComponentData.h"
struct EntityData
{
	eEntityType myType;
	AIComponentData myAIComponentData;
	AnimationComponentData myAnimationData;
	FirstPersonRenderComponentData myFirstPersonRenderData;
	GraphicsComponentData myGraphicsData;
	HealthComponentData myHealthData;
	InputComponentData myInputData;
	NetworkComponentData myNetworkData;
	GrenadeComponentData myGrenadeData;
	BulletComponentData myProjecileData;
	PhysicsComponentData myPhysicsData;
	ShootingComponentData myShootingData;
	TriggerComponentData myTriggerData;
	UpgradeComponentData myUpgradeData;

	std::string mySubType;
};