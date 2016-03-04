#include "stdafx.h"

#include "AnimationComponent.h"
#include "GraphicsComponent.h"
#include "HealthComponent.h"
#include "ProjectileComponent.h"
#include <Scene.h>
#include <Instance.h>
#include <EmitterMessage.h>
#include <PostMaster.h>
#include "TriggerComponent.h"

#include <PhysEntity.h>

Entity::Entity(const EntityData& aEntityData, Prism::Scene* aScene, bool aClientSide, const CU::Vector3<float>& aStartPosition,
		const CU::Vector3f& aRotation, const CU::Vector3f& aScale)
	: myScene(aScene)
	, myEntityData(aEntityData)
	, myEmitterConnection(nullptr)
	, myPhysEntity(nullptr)
	, myIsClientSide(aClientSide)
{
	for (int i = 0; i < static_cast<int>(eComponentType::_COUNT); ++i)
	{
		myComponents[i] = nullptr;
	}

	myOrientation.SetPos(aStartPosition);

	if (myScene != nullptr)
	{
		if (aEntityData.myAnimationData.myExistsInEntity == true)
		{
			myComponents[static_cast<int>(eComponentType::ANIMATION)] = new AnimationComponent(*this, aEntityData.myAnimationData);
			GetComponent<AnimationComponent>()->SetRotation(aRotation);
			GetComponent<AnimationComponent>()->SetScale(aScale);
			myPhysEntity = new Prism::PhysEntity(aEntityData.myPhysData, myOrientation, aEntityData.myAnimationData.myModelPath, this);
		}
		else if (aEntityData.myGraphicsData.myExistsInEntity == true)
		{
			myComponents[static_cast<int>(eComponentType::GRAPHICS)] = new GraphicsComponent(*this, aEntityData.myGraphicsData);
			GetComponent<GraphicsComponent>()->SetRotation(aRotation);
			GetComponent<GraphicsComponent>()->SetScale(aScale);
			myPhysEntity = new Prism::PhysEntity(aEntityData.myPhysData, myOrientation, aEntityData.myGraphicsData.myModelPath, this);
		}
	}
	
	if (aEntityData.myProjecileData.myExistsInEntity == true)
	{
		myComponents[static_cast<int>(eComponentType::PROJECTILE)] = new ProjectileComponent(*this, aEntityData.myProjecileData);
	}

	if (aEntityData.myHealthData.myExistsInEntity == true)
	{
		myComponents[static_cast<int>(eComponentType::HEALTH)] = new HealthComponent(*this, aEntityData.myHealthData);
	}

	if (aEntityData.myTriggerData.myExistsInEntity == true)
	{
		myComponents[static_cast<int>(eComponentType::TRIGGER)] = new TriggerComponent(*this, aEntityData.myTriggerData);
	}
	

	//myPhysEntity = new Prism::PhysEntity(&pos.x, aEntityData.myPhysData, myOrientation, aEntityData.myGraphicsData.myModelPath);
	Reset();
}

Entity::~Entity()
{
	if (myIsInScene == true)
	{
		RemoveFromScene();
	}
	for (int i = 0; i < static_cast<int>(eComponentType::_COUNT); ++i)
	{
		delete myComponents[i];
		myComponents[i] = nullptr;
	}
	SAFE_DELETE(myPhysEntity);
}

void Entity::Reset()
{
	myAlive = false;
	for (int i = 0; i < static_cast<int>(eComponentType::_COUNT); ++i)
	{
		if (myComponents[i] != nullptr)
		{
			myComponents[i]->Reset();
		}
	}
}

void Entity::Update(float aDeltaTime)
{
	for (int i = 0; i < static_cast<int>(eComponentType::_COUNT); ++i)
	{
		if (myComponents[i] != nullptr)
		{
			myComponents[i]->Update(aDeltaTime);
		}
	}

	if (myEntityData.myPhysData.myPhysics == ePhysics::DYNAMIC)
	{
		myPhysEntity->UpdateOrientation();
		memcpy(&myOrientation.myMatrix[0], myPhysEntity->GetOrientation(), sizeof(float) * 16);
	}
}

void Entity::AddComponent(Component* aComponent)
{
	DL_ASSERT_EXP(myComponents[int(aComponent->GetType())] == nullptr, "Tried to add component several times");
	myComponents[int(aComponent->GetType())] = aComponent;
}

void Entity::RemoveComponent(eComponentType aComponent)
{
	DL_ASSERT_EXP(myComponents[int(aComponent)] != nullptr, "Tried to remove an nonexisting component");
	delete myComponents[int(aComponent)];
	myComponents[int(aComponent)] = nullptr;
}

void Entity::AddToScene()
{
	DL_ASSERT_EXP(myIsInScene == false, "Tried to add Entity to scene twice");
	DL_ASSERT_EXP(myIsClientSide == true, "You can't add Entity to scene on server side.");

	if (GetComponent<GraphicsComponent>() != nullptr && GetComponent<GraphicsComponent>()->GetInstance() != nullptr)
	{
		myScene->AddInstance(GetComponent<GraphicsComponent>()->GetInstance());
	}
	else if (GetComponent<AnimationComponent>() != nullptr && GetComponent<AnimationComponent>()->GetInstance() != nullptr)
	{
		myScene->AddInstance(GetComponent<AnimationComponent>()->GetInstance());
	}
	
	myIsInScene = true;
}

void Entity::RemoveFromScene()
{
	DL_ASSERT_EXP(myIsInScene == true, "Tried to remove Entity not in scene");
	DL_ASSERT_EXP(myIsClientSide == true, "You can't remove Entity to scene on server side.");

	if (GetComponent<GraphicsComponent>() != nullptr)
	{
		myScene->RemoveInstance(GetComponent<GraphicsComponent>()->GetInstance());
	}
	else if (GetComponent<AnimationComponent>() != nullptr)
	{
		myScene->RemoveInstance(GetComponent<AnimationComponent>()->GetInstance());
	}

	myIsInScene = false;
}

eEntityType Entity::GetType() const
{
	return myEntityData.myType;
}

void Entity::AddEmitter(Prism::ParticleEmitterInstance* anEmitterConnection)
{
	myEmitterConnection = anEmitterConnection;
}

Prism::ParticleEmitterInstance* Entity::GetEmitter()
{
	return myEmitterConnection;
}

Prism::PhysEntity* Entity::GetPhysEntity() const
{
	return myPhysEntity;
}

void Entity::Kill()
{
	myAlive = false;

	if (myIsInScene == true)
	{
		RemoveFromScene();
		myPhysEntity->RemoveFromScene();
	}
}