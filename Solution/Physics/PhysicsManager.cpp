#include "stdafx.h"
#include "PhysicsManager.h"
#include <ThreadUtilities.h>
#include "PhysicsHelperFunc.h"
#include <GameConstants.h>
#ifdef _DEBUG
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3DEBUG_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3CommonDEBUG_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3CookingDEBUG_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysXProfileSDKDEBUG.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PxTaskDEBUG.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3ExtensionsDEBUG.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysXVisualDebuggerSDKDEBUG.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3CharacterKinematicDEBUG_x86.lib")
#else
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3Common_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3Cooking_x86.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysXProfileSDK.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PxTask.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3Extensions.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysXVisualDebuggerSDK.lib")
#pragma comment(lib, "PhysX\\vc12win32\\PhysX3CharacterKinematic_x86.lib")
#endif

#include <CommonHelper.h>
#include <pxphysicsapi.h>
#include <PxQueryReport.h>
#include <TimerManager.h>
#include "PhysicsComponentData.h"
#include "../Entity/InputComponentData.h"
#include "../Network/SharedNetworkManager.h"
#include "../Network/NetMessageEntityState.h"
#include "wavefront.h"
#include "../InputWrapper/InputWrapper.h"
#define MATERIAL_ID			0x01

//#ifndef RELEASE_BUILD
#define GENERATE_COW
//#endif

physx::PxFilterFlags SampleSubmarineFilterShader(
	physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
	physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
	physx::PxPairFlags& pairFlags, const void* constantBlock, physx::PxU32 constantBlockSize)
{
	// let triggers through
	if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
		return physx::PxFilterFlag::eDEFAULT;
	}
	// generate contacts for all that were not filtered above
	pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT;

	// trigger the contact callback for pairs (A,B) where
	// the filtermask of A contains the ID of B and vice versa.
	if ((filterData0.word0 & filterData1.word1) && (filterData1.word0 & filterData0.word1))
	{
		pairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;
	}

	return physx::PxFilterFlag::eDEFAULT;
}

namespace Prism
{
	PhysicsManager::PhysicsManager(std::function<void(PhysicsComponent*, PhysicsComponent*, bool)> anOnTriggerCallback, bool aIsServer
		, std::function<void(PhysicsComponent*, PhysicsComponent*)> aOnContactCallback)
		: myPhysicsComponentCallbacks(4096)
		, myOnTriggerCallback(anOnTriggerCallback)
		, myOnContactCallback(aOnContactCallback)
#ifdef THREAD_PHYSICS
		, myQuit(false)
		, myLogicDone(false)
		, myPhysicsDone(false)
		, mySwapDone(false)
		, myTimerManager(new CU::TimerManager())
		, myPhysicsThread(nullptr)
#endif
		, myInitDone(false)
		, myCurrentIndex(0)
		, myIsSwapping(false)
		, myIsReading(false)
		, myIsServer(aIsServer)
		, myIsOverheated(false)
		, mySprintEnergy(0.0f)
	{
		myRaycastJobs[0].Init(64);
		myRaycastJobs[1].Init(64);
		myRaycastResults[0].Init(64);
		myRaycastResults[1].Init(64);
		myForceJobs[0].Init(64);
		myForceJobs[1].Init(64);
		myVelocityJobs[0].Init(64);
		myVelocityJobs[1].Init(64);
		myPositionJobs[0].Init(256);
		myPositionJobs[1].Init(256);
		myOnTriggerResults[0].Init(256);
		myOnTriggerResults[1].Init(256);
		myActorsToAdd[0].Init(256);
		myActorsToAdd[1].Init(256);
		myActorsToRemove[0].Init(16384);
		myActorsToRemove[1].Init(16384);
		myCapsulesToAdd[0].Init(256);
		myCapsulesToAdd[1].Init(256);
		myActorsToSleep[0].Init(256);
		myActorsToSleep[1].Init(256);
		myActorsToWakeUp[0].Init(256);
		myActorsToWakeUp[1].Init(256);
		myMoveJobs[0].Init(256);
		myMoveJobs[1].Init(256);
		myControllerPositions[0].Init(256);
		myControllerPositions[1].Init(256);
		myTimestep = 1.f / 60.f;

		myFoundation = PxCreateFoundation(0x03030300, myDefaultAllocatorCallback, myDefaultErrorCallback);

		myProfileZoneManager = nullptr;
		//myProfileZoneManager = &physx::PxProfileZoneManager::createProfileZoneManager(myFoundation);

		bool recordMemoryAllocations = true;
		myPhysicsSDK = PxCreatePhysics(0x03030300, *myFoundation, physx::PxTolerancesScale(),
			recordMemoryAllocations, nullptr);

		PxInitExtensions(*myPhysicsSDK);

		physx::PxCookingParams params(myPhysicsSDK->getTolerancesScale());
		params.meshWeldTolerance = 0.001f;
		params.meshPreprocessParams = physx::PxMeshPreprocessingFlags(
			physx::PxMeshPreprocessingFlag::eWELD_VERTICES | physx::PxMeshPreprocessingFlag::eREMOVE_UNREFERENCED_VERTICES | physx::PxMeshPreprocessingFlag::eREMOVE_DUPLICATED_TRIANGLES);

		myCooker = nullptr;
		myCooker = PxCreateCooking(0x03030300, *myFoundation, params);


		DL_ASSERT_EXP(myCooker != nullptr, "Failed to create cooker");

		physx::PxSceneDesc sceneDesc(myPhysicsSDK->getTolerancesScale());
		sceneDesc.gravity = physx::PxVec3(0.f, -9.82f, 0.f);

		myCpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);

		if (!sceneDesc.cpuDispatcher)
		{
			if (!myCpuDispatcher)
			{
				DL_ASSERT("Failed to PxDefaultCpuDispatcherCreate");
			}

			sceneDesc.cpuDispatcher = myCpuDispatcher;
		}

		if (!sceneDesc.filterShader)
		{
			//sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
			sceneDesc.filterShader = SampleSubmarineFilterShader;
		}

		myScene = myPhysicsSDK->createScene(sceneDesc);
		if (!myScene)
		{
			DL_ASSERT("Failed to createScene");
		}
#ifdef _DEBUG
		myScene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.f);
		myScene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.f);
#endif
		myScene->setFlag(physx::PxSceneFlag::eENABLE_KINEMATIC_PAIRS, true);
		myScene->setFlag(physx::PxSceneFlag::eENABLE_KINEMATIC_STATIC_PAIRS, true);
		myScene->setSimulationEventCallback(this);

#ifdef _DEBUG
		myScene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.f);
		myScene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.f);
		if (aIsServer == true && SERVER_CONNECT_TO_DEBUGGER == true
			|| aIsServer == false && SERVER_CONNECT_TO_DEBUGGER == false)
		{
			if (myPhysicsSDK->getPvdConnectionManager())
			{
				myPhysicsSDK->getPvdConnectionManager()->addHandler(*this);
			}

			const char* pvdHostIp = "127.0.0.1";
			int port = 5425;
			unsigned int timeout = 100;
			physx::debugger::PxVisualDebuggerConnectionFlags connectionFlags = physx::debugger::PxVisualDebuggerExt::getAllConnectionFlags();

			myDebugConnection = physx::debugger::PxVisualDebuggerExt::createConnection(myPhysicsSDK->getPvdConnectionManager()
				, pvdHostIp, port, timeout, connectionFlags);

			myPhysicsSDK->getVisualDebugger()->setVisualDebuggerFlag(physx::PxVisualDebuggerFlag::eTRANSMIT_SCENEQUERIES, true);
		}
#endif

		myDefaultMaterial = myPhysicsSDK->createMaterial(0.5, 0.5, 0.5);
		//physx::PxReal d = 0.0f;
		physx::PxTransform pose = physx::PxTransform(physx::PxVec3(0.f, -10.f, 0.f), physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.f, 0.f, 1.f)));
		physx::PxRigidStatic* plane = myPhysicsSDK->createRigidStatic(pose);
		if (!plane)
		{
			DL_ASSERT("Failed to create plane");
		}

		physx::PxShape* shape = plane->createShape(physx::PxPlaneGeometry(), *myDefaultMaterial);
		if (!shape)
		{
			DL_ASSERT("Failed to create shape");
		}

		myScene->addActor(*plane);
		myControllerManager = PxCreateControllerManager(*myScene);

		myControllerManager->setOverlapRecoveryModule(true);		
	}

	PhysicsManager::~PhysicsManager()
	{
#ifdef THREAD_PHYSICS
		SAFE_DELETE(myPhysicsThread);
		SAFE_DELETE(myTimerManager);
#endif
		if (myDebugConnection != nullptr)
		{
			//myDebugConnection->release();
		}
		myControllerManager->release();
		myScene->release();
		myCpuDispatcher->release();
		myCooker->release();
		PxCloseExtensions();
		myPhysicsSDK->release();
		myFoundation->release();
	}


#ifdef THREAD_PHYSICS
	void PhysicsManager::InitThread()
	{
		myPhysicsThread = new std::thread(&PhysicsManager::ThreadUpdate, this);

		CU::SetThreadName(myPhysicsThread->get_id(), "Physics thread");
	}

	void PhysicsManager::ShutdownThread()
	{
		myQuit = true;
		myLogicDone = true;
		mySwapDone = true;
		myIsSwapping = false;
		myIsReading = false;
		myPhysicsThread->join();
	}

	void PhysicsManager::ThreadUpdate()
	{
		float totalTime = 0;
		while (myQuit == false)
		{

			myTimerManager->Update();
			totalTime += myTimerManager->GetMasterTimer().GetTime().GetFrameTime();
			if (totalTime >= (1.0f / 60.0f))
			{
				totalTime = 0;
				Update();


				Swap();
				if (myLogicDone == true)
				{
					SetPhysicsDone();
					//WaitForSwap();
				}
			}

			std::this_thread::yield();
			//::Sleep(16);
		}
	}
#endif

	void PhysicsManager::Add(const PhysicsCallbackStruct& aCallbackStruct)
	{
		if (aCallbackStruct.myData->myPhysicsType == ePhysics::DYNAMIC)
		{
			myPhysicsComponentCallbacks.Add(aCallbackStruct);
		}
	}

	void PhysicsManager::Swap()
	{
		while (myIsReading == true)
		{
		}

		myIsSwapping = true;
		for each (const PhysicsCallbackStruct& obj in myPhysicsComponentCallbacks)
		{
			obj.mySwapOrientationCallback();
		}

		myCurrentIndex ^= 1;

		//std::swap(myRaycastJobs[0], myRaycastJobs[1]);
		//std::swap(myRaycastResults[0], myRaycastResults[1]);
		//std::swap(myMoveJobs[0], myMoveJobs[1]);
		//std::swap(myForceJobs[0], myForceJobs[1]);
		//std::swap(myVelocityJobs[0], myVelocityJobs[1]);
		//std::swap(myPositionJobs[0], myPositionJobs[1]);
		//std::swap(myOnTriggerResults[0], myOnTriggerResults[1]);
		//std::swap(myActorsToAdd[0], myActorsToAdd[1]);
		//std::swap(myActorsToRemove[0], myActorsToRemove[1]);
		//std::swap(myActorsToSleep[0], myActorsToSleep[1]);
		//std::swap(myActorsToWakeUp[0], myActorsToWakeUp[1]);

		//myMoveJobs[myCurrentIndex].myId = -1;
		myIsSwapping = false;
	}

	void PhysicsManager::Update()
	{
		if (myIsClientSide == true && GC::PlayerAlive == true)
		{
			CU::InputWrapper::GetInstance()->PhysicsUpdate();

			if (CU::InputWrapper::GetInstance()->KeyDown(DIK_SPACE, CU::InputWrapper::eType::PHYSICS))
			{
				if (GetAllowedToJump(myPlayerCapsule) == true)
				{
					myVerticalSpeed = 0.25f;
				}
			}

			myVerticalSpeed -= myTimestep;
			myVerticalSpeed = fmaxf(myVerticalSpeed, -0.5f);


			CU::Vector3<float> movement;
			float magnitude = 0.f;
			int count = 0;
			if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_S, CU::InputWrapper::eType::PHYSICS))
			{
				movement.z -= 1.f;
				magnitude += myPlayerInputData->myBackwardMultiplier;
				++count;
			}
			if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_W, CU::InputWrapper::eType::PHYSICS))
			{
				movement.z += 1.f;
				magnitude += myPlayerInputData->myForwardMultiplier;
				++count;
			}
			if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_A, CU::InputWrapper::eType::PHYSICS))
			{
				movement.x -= 1.f;
				magnitude += myPlayerInputData->mySidewaysMultiplier;
				++count;
			}
			if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_D, CU::InputWrapper::eType::PHYSICS))
			{
				movement.x += 1.f;
				magnitude += myPlayerInputData->mySidewaysMultiplier;
				++count;
			}


			if (count > 0)
			{
				magnitude /= count;
			}

			if (CU::Length(movement) < 0.02f)
			{
				if (myState != eEntityState::IDLE)
				{
					myState = eEntityState::IDLE;
					SharedNetworkManager::GetInstance()->AddMessage<NetMessageEntityState>(NetMessageEntityState(myState, myPlayerGID));
				}
			}
			else if (myState != eEntityState::WALK)
			{
				myState = eEntityState::WALK;
				SharedNetworkManager::GetInstance()->AddMessage<NetMessageEntityState>(NetMessageEntityState(myState, myPlayerGID));
			}			

			bool isSprinting = false;
			bool shouldDecreaseEnergy = true;
			bool previousOverheat = myIsOverheated;
			if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_LSHIFT, CU::InputWrapper::eType::PHYSICS))
			{
				if (mySprintEnergy < myPlayerInputData->myMaxSprintEnergy && myIsOverheated == false)
				{
					mySprintEnergy += myPlayerInputData->mySprintIncrease * myTimestep;
					if (mySprintEnergy >= myPlayerInputData->myMaxSprintEnergy)
					{
						myIsOverheated = true;
					}

					if (movement.z > 0.f)
					{
						movement.z *= myPlayerInputData->mySprintMultiplier;
						isSprinting = true;
					}

					shouldDecreaseEnergy = false;
				}
			}


			if (CU::InputWrapper::GetInstance()->KeyDown(DIK_LSHIFT, CU::InputWrapper::eType::PHYSICS) == true)
			{
				if (GC::PlayerShouldPlaySprintErrorSound != true && myIsOverheated == true)
				{
					GC::PlayerShouldPlaySprintErrorSound = true;
				}
			}

			if (CU::InputWrapper::GetInstance()->KeyDown(DIK_LSHIFT, CU::InputWrapper::eType::PHYSICS) == true)
			{
				if (myIsOverheated == false)
				{
					GC::PlayerShouldPlaySprintSound = true;
				}
			}
			if (CU::InputWrapper::GetInstance()->KeyUp(DIK_LSHIFT, CU::InputWrapper::eType::PHYSICS) == true
				|| myIsOverheated == true && previousOverheat == false)
			{
				GC::PlayerShouldStopSprintSound = true;
			}

			if (shouldDecreaseEnergy == true && CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_LSHIFT, CU::InputWrapper::eType::PHYSICS) == false)
			{
				mySprintEnergy -= myPlayerInputData->mySprintDecrease * myTimestep;
				mySprintEnergy = fmaxf(mySprintEnergy, 0.f);
			}

			if (myIsOverheated == true && mySprintEnergy <= 0.f)
			{
				myIsOverheated = false;
			}

			/*if (CU::InputWrapper::GetInstance()->KeyIsPressed(DIK_LSHIFT))
			{
			movement *= myPlayerInputData->mySprintMultiplier;
			}*/

			movement = movement * (*myPlayerOrientation);
			movement.y = 0.f;
			CU::Normalize(movement);
			movement *= myPlayerInputData->mySpeed * magnitude;

			if (isSprinting == true)
			{
				movement *= myPlayerInputData->mySprintMultiplier;
			}
			movement.y = myVerticalSpeed;
			Move(myPlayerCapsule, movement, 0.05f, 1.f / 60.f);

		}


		if (!myScene)
		{
			DL_ASSERT("no scene in PhysicsManager");
		}

		myScene->simulate(myTimestep);

		while (!myScene->fetchResults())
		{
			// do something useful..
		}

		for (int i = 0; i < myActorsToAdd[myCurrentIndex ^ 1].Size(); ++i)
		{
			if (myActorsToAdd[myCurrentIndex ^ 1][i]->getScene() == nullptr)
			{
				GetScene()->addActor(*myActorsToAdd[myCurrentIndex ^ 1][i]);
			}
		}
		myActorsToAdd[myCurrentIndex ^ 1].RemoveAll();


		for (int i = 0; i < myCapsulesToAdd[myCurrentIndex ^ 1].Size(); ++i)
		{
			myScene->addActor(*myControllerManager->getController(myCapsulesToAdd[myCurrentIndex ^ 1][i])->getActor());
		}
		myCapsulesToAdd[myCurrentIndex ^ 1].RemoveAll();

		myInitDone = true;

		for (int i = 0; i < myActorsToWakeUp[myCurrentIndex ^ 1].Size(); ++i)
		{
			myActorsToWakeUp[myCurrentIndex ^ 1][i]->setActorFlag(physx::PxActorFlag::Enum::eDISABLE_SIMULATION, false);
		}
		myActorsToWakeUp[myCurrentIndex ^ 1].RemoveAll();

		for (int i = 0; i < myMoveJobs[myCurrentIndex ^ 1].Size(); ++i)
		{
			if (myMoveJobs[myCurrentIndex ^ 1][i].myId > -1)
			{
				Move(myMoveJobs[myCurrentIndex ^ 1][i]);

				const physx::PxExtendedVec3& pos = myControllerManager->getController(myMoveJobs[myCurrentIndex ^ 1][i].myId)->getFootPosition();
				myControllerPositions[myCurrentIndex ^ 1][i].x = float(pos.x);
				myControllerPositions[myCurrentIndex ^ 1][i].y = float(pos.y);
				myControllerPositions[myCurrentIndex ^ 1][i].z = float(pos.z);

			}
		}

		for (int i = 0; i < myForceJobs[myCurrentIndex ^ 1].Size(); ++i)
		{
			AddForce(myForceJobs[myCurrentIndex ^ 1][i]);
		}
		myForceJobs[myCurrentIndex ^ 1].RemoveAll();

		for (int i = 0; i < myVelocityJobs[myCurrentIndex ^ 1].Size(); ++i)
		{
			SetVelocity(myVelocityJobs[myCurrentIndex ^ 1][i]);
		}
		myVelocityJobs[myCurrentIndex ^ 1].RemoveAll();

		for (int i = 0; i < myPositionJobs[myCurrentIndex ^ 1].Size(); ++i)
		{
			if (myPositionJobs[myCurrentIndex ^ 1][i].myID > -1)
			{
				int capID = myPositionJobs[myCurrentIndex ^ 1][i].myID;
				physx::PxExtendedVec3 pos;
				pos.x = myPositionJobs[myCurrentIndex ^ 1][i].myPosition.x;
				pos.y = myPositionJobs[myCurrentIndex ^ 1][i].myPosition.y;
				pos.z = myPositionJobs[myCurrentIndex ^ 1][i].myPosition.z;
				myControllerManager->getController(myPositionJobs[myCurrentIndex ^ 1][i].myID)->setPosition(pos);
				myControllerPositions[myCurrentIndex ^ 1][capID].x = float(myPositionJobs[myCurrentIndex ^ 1][i].myPosition.x);
				myControllerPositions[myCurrentIndex ^ 1][capID].y = float(myPositionJobs[myCurrentIndex ^ 1][i].myPosition.y);
				myControllerPositions[myCurrentIndex ^ 1][capID].z = float(myPositionJobs[myCurrentIndex ^ 1][i].myPosition.z);
			}
			else
			{
				SetPosition(myPositionJobs[myCurrentIndex ^ 1][i]);
			}
		}
		myPositionJobs[myCurrentIndex ^ 1].RemoveAll();


		for (int i = 0; i < myPhysicsComponentCallbacks.Size(); ++i)
		{
			if (myPhysicsComponentCallbacks[i].myData->myPhysicsType == ePhysics::DYNAMIC)
			{
				myPhysicsComponentCallbacks[i].myUpdateCallback();
			}
		}

		for (int i = 0; i < myRaycastJobs[myCurrentIndex ^ 1].Size(); ++i)
		{
			RayCast(myRaycastJobs[myCurrentIndex ^ 1][i]);
		}
		myRaycastJobs[myCurrentIndex ^ 1].RemoveAll();

		for (int i = 0; i < myOnTriggerResults[myCurrentIndex ^ 1].Size(); ++i)
		{

			myOnTriggerCallback(myOnTriggerResults[myCurrentIndex ^ 1][i].myFirstPhysicsComponent, myOnTriggerResults[myCurrentIndex ^ 1][i].mySecondPhysicsComponent, myOnTriggerResults[myCurrentIndex ^ 1][i].myHasEntered);
		}
		myOnTriggerResults[myCurrentIndex ^ 1].RemoveAll();

		for (int i = 0; i < myActorsToSleep[myCurrentIndex ^ 1].Size(); ++i)
		{
			physx::PxTransform pose = physx::PxTransform(
				ConvertVector({ 0.f, -1.f, 0.f }), physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.f, 0.f, 1.f)));
			static_cast<physx::PxRigidDynamic*>(myActorsToSleep[myCurrentIndex ^ 1][i])->setGlobalPose(pose);
			myActorsToSleep[myCurrentIndex ^ 1][i]->setActorFlag(physx::PxActorFlag::Enum::eDISABLE_SIMULATION, true);
		}
		myActorsToSleep[myCurrentIndex ^ 1].RemoveAll();



		for (int i = 0; i < myActorsToRemove[myCurrentIndex ^ 1].Size(); ++i)
		{
			if (myActorsToRemove[myCurrentIndex ^ 1][i]->getScene() != nullptr)
			{
				GetScene()->removeActor(*myActorsToRemove[myCurrentIndex ^ 1][i]);
			}
		}
		myActorsToRemove[myCurrentIndex ^ 1].RemoveAll();
	}

	void PhysicsManager::RayCast(const CU::Vector3<float>& aOrigin, const CU::Vector3<float>& aNormalizedDirection
		, float aMaxRayDistance, std::function < void(PhysicsComponent*, const CU::Vector3<float>&
		, const CU::Vector3<float>&, const CU::Vector3<float>&) > aFunctionToCall, const PhysicsComponent* aComponent)
	{
		myRaycastJobs[myCurrentIndex].Add(RaycastJob(aOrigin, aNormalizedDirection, aMaxRayDistance, aFunctionToCall, aComponent));
	}

	void PhysicsManager::onContact(const physx::PxContactPairHeader& aHeader, const physx::PxContactPair* somePairs, physx::PxU32 aCount)
	{
		for (physx::PxU32 i = 0; i < aCount; i++)
		{
			const physx::PxContactPair& pairs = somePairs[i];


			if (pairs.events == physx::PxPairFlag::Enum::eNOTIFY_TOUCH_FOUND)
			{
				if (myIsServer == false)
				{
					myOnContactCallback(static_cast<PhysicsComponent*>(aHeader.actors[0]->userData)
						, static_cast<PhysicsComponent*>(aHeader.actors[1]->userData));
				}
			}


		}
	}

	void PhysicsManager::onTrigger(physx::PxTriggerPair* somePairs, physx::PxU32 aCount)
	{
		for (physx::PxU32 i = 0; i < aCount; i++)
		{
			const physx::PxTriggerPair& pairs = somePairs[i];

			SET_RUNTIME(false);
			if (pairs.status == physx::PxPairFlag::Enum::eNOTIFY_TOUCH_FOUND)
			{
				myOnTriggerResults[myCurrentIndex].Add(OnTriggerResult(static_cast<PhysicsComponent*>(pairs.triggerActor->userData)
					, static_cast<PhysicsComponent*>(pairs.otherActor->userData), true));
			}
			else if (pairs.status == physx::PxPairFlag::Enum::eNOTIFY_TOUCH_LOST)
			{
				myOnTriggerResults[myCurrentIndex].Add(OnTriggerResult(static_cast<PhysicsComponent*>(pairs.triggerActor->userData)
					, static_cast<PhysicsComponent*>(pairs.otherActor->userData), false));
			}
			RESET_RUNTIME;
		}
	}

	void PhysicsManager::RayCast(const RaycastJob& aRaycastJob)
	{
		bool returnValue = false;
		physx::PxVec3 origin(ConvertVector(aRaycastJob.myOrigin));
		physx::PxVec3 unitDirection(ConvertVector(aRaycastJob.myNormalizedDirection));
		physx::PxReal maxDistance = aRaycastJob.myMaxRayDistance;

		physx::PxRaycastHit touches[32];
		physx::PxRaycastBuffer buffer(touches, 32);

		returnValue = myScene->raycast(origin, unitDirection, maxDistance, buffer);
		CU::Vector3<float> hitPosition;
		CU::Vector3<float> hitNormal;

		PhysicsComponent* ent = nullptr;//static_cast<PhysEntity*>(buffer.touches[myCurrentIndex].actor->userData);
		if (returnValue == true)
		{
			float closestDist = FLT_MAX;
			for (int i = 0; i < int(buffer.nbTouches); ++i)
			{
				if (buffer.touches[i].distance < closestDist)
				{
					if (buffer.touches[i].distance == 0.f
						|| buffer.touches[i].actor->userData == aRaycastJob.myRaycasterComponent)
					{
						continue;
					}

					physx::PxShapeFlags& flags = buffer.touches[i].shape->getFlags();
					if (flags.isSet(physx::PxShapeFlag::eTRIGGER_SHAPE) == true)
					{
						continue;
					}

					closestDist = buffer.touches[i].distance;
					ent = static_cast<PhysicsComponent*>(buffer.touches[i].actor->userData);
					hitPosition.x = buffer.touches[i].position.x;
					hitPosition.y = buffer.touches[i].position.y;
					hitPosition.z = buffer.touches[i].position.z;

					hitNormal.x = buffer.touches[i].normal.x;
					hitNormal.y = buffer.touches[i].normal.y;
					hitNormal.z = buffer.touches[i].normal.z;
				}
			}
		}
		myRaycastResults[myCurrentIndex ^ 1].Add(RaycastResult(ent, aRaycastJob.myNormalizedDirection, hitPosition, hitNormal, aRaycastJob.myFunctionToCall));
	}

	void PhysicsManager::AddForce(physx::PxRigidDynamic* aDynamicBody, const CU::Vector3<float>& aDirection, float aMagnitude)
	{
		myForceJobs[myCurrentIndex].Add(ForceJob(aDynamicBody, aDirection, aMagnitude));
	}

	void PhysicsManager::AddForce(const ForceJob& aForceJob)
	{
		aForceJob.myDynamicBody->addForce(ConvertVector(aForceJob.myDirection) * aForceJob.myMagnitude, physx::PxForceMode::eVELOCITY_CHANGE);
	}

	void PhysicsManager::SetVelocity(physx::PxRigidDynamic* aDynamicBody, const CU::Vector3<float>& aVelocity)
	{
		myVelocityJobs[myCurrentIndex].Add(VelocityJob(aDynamicBody, aVelocity));
	}

	void PhysicsManager::SetVelocity(const VelocityJob& aVelocityJob)
	{
		aVelocityJob.myDynamicBody->setLinearVelocity(ConvertVector(aVelocityJob.myVelocity));
	}

	void PhysicsManager::TeleportToPosition(physx::PxRigidDynamic* aDynamicBody, const CU::Vector3<float>& aPosition)
	{
		myPositionJobs[myCurrentIndex].Add(PositionJob(aDynamicBody, aPosition, PositionJobType::TELEPORT));
	}

	void PhysicsManager::TeleportToPosition(physx::PxRigidStatic * aStaticBody, const CU::Vector3<float>& aPosition)
	{
		myPositionJobs[myCurrentIndex].Add(PositionJob(aStaticBody, aPosition, PositionJobType::TELEPORT));
	}

	void PhysicsManager::TeleportToPosition(int aID, const CU::Vector3<float>& aPosition)
	{
		myPositionJobs[myCurrentIndex].Add(PositionJob(aID, aPosition, PositionJobType::TELEPORT));
	}

	void PhysicsManager::MoveToPosition(physx::PxRigidDynamic* aDynamicBody, const CU::Vector3<float>& aPosition)
	{
		myPositionJobs[myCurrentIndex].Add(PositionJob(aDynamicBody, aPosition, PositionJobType::MOVE));
	}

	void PhysicsManager::SetPosition(const PositionJob& aPositionJob)
	{
		physx::PxTransform pose = physx::PxTransform(
			ConvertVector(aPositionJob.myPosition), physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.f, 0.f, 1.f)));

		if (aPositionJob.myType == PositionJobType::TELEPORT)
		{
			aPositionJob.myRigidBody->setGlobalPose(pose);
		}
		else if (aPositionJob.myType == PositionJobType::MOVE)
		{
			static_cast<physx::PxRigidDynamic*>(aPositionJob.myRigidBody)->setKinematicTarget(pose);
		}
		else
		{
			//DL_ASSERT("Unknown position job type.");
		}
	}

	void PhysicsManager::onPvdConnected(physx::debugger::comm::PvdConnection&)
	{
#ifdef _DEBUG
		myPhysicsSDK->getVisualDebugger()->setVisualizeConstraints(true);
		myPhysicsSDK->getVisualDebugger()->setVisualDebuggerFlag(physx::PxVisualDebuggerFlag::eTRANSMIT_CONTACTS, true);
		myPhysicsSDK->getVisualDebugger()->setVisualDebuggerFlag(physx::PxVisualDebuggerFlag::eTRANSMIT_SCENEQUERIES, true);
#endif
	}

	void PhysicsManager::onPvdDisconnected(physx::debugger::comm::PvdConnection&)
	{
		myDebugConnection->release();
	}

	int PhysicsManager::CreatePlayerController(const CU::Vector3<float>& aStartPosition, PhysicsComponent* aComponent, bool aShouldAddToScene)
	{
		physx::PxCapsuleControllerDesc controllerDesc;

		//controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eEASY;
		controllerDesc.height = 1.5f;
		controllerDesc.radius = 0.25f;
		controllerDesc.material = myDefaultMaterial;
		controllerDesc.position = physx::PxExtendedVec3(aStartPosition.x, aStartPosition.y, aStartPosition.z);//fix
		controllerDesc.userData = aComponent;

		myControllerManager->createController(controllerDesc);
		myControllerManager->getController(myControllerManager->getNbControllers() - 1)->getActor()->userData = aComponent;

		myControllerPositions[0].Add(aStartPosition);
		myControllerPositions[1].Add(aStartPosition);
		myMoveJobs[0].Add(MoveJob());
		myMoveJobs[1].Add(MoveJob());

		if (aShouldAddToScene == false)
		{
			myScene->removeActor(*myControllerManager->getController(myControllerManager->getNbControllers() - 1)->getActor());
		}
		return myControllerManager->getNbControllers() - 1;
	}

	void PhysicsManager::Move(int aId, const CU::Vector3<float>& aDirection, float aMinDisplacement, float aDeltaTime)
	{
		myMoveJobs[myCurrentIndex][aId] = MoveJob(aId, aDirection, aMinDisplacement, myTimestep);
	}

	void PhysicsManager::Move(const MoveJob& aMoveJob)
	{
		/*std::stringstream ss;
		ss << "X : " << aMoveJob.myDirection.x << "\n"
		<< "Y : " << aMoveJob.myDirection.y << "\n"
		<< "Z : " << aMoveJob.myDirection.z << "\n";
		OutputDebugStringA(ss.str().c_str());*/

		physx::PxControllerFilters filter;
		myControllerManager->getController(aMoveJob.myId)->move(
			ConvertVector(aMoveJob.myDirection), aMoveJob.myMinDisplacement, aMoveJob.myDeltaTime, filter, nullptr);
	}

	void PhysicsManager::UpdateOrientation(physx::PxRigidDynamic* aDynamicBody, physx::PxShape** aShape, float* aThread4x4)
	{
		physx::PxU32 nShapes = aDynamicBody->getNbShapes();
		aDynamicBody->getShapes(aShape, nShapes);

		physx::PxTransform pT = physx::PxShapeExt::getGlobalPose(*aShape[0], *aDynamicBody);
		physx::PxTransform graphicsTransform(pT.p, pT.q);
		physx::PxMat33 m = physx::PxMat33(graphicsTransform.q);

		aThread4x4[0] = m(0, 0);
		aThread4x4[1] = m(1, 0);
		aThread4x4[2] = m(2, 0);
		aThread4x4[3] = 0;

		aThread4x4[4] = m(0, 1);
		aThread4x4[5] = m(1, 1);
		aThread4x4[6] = m(2, 1);
		aThread4x4[7] = 0;

		aThread4x4[8] = m(0, 2);
		aThread4x4[9] = m(1, 2);
		aThread4x4[10] = m(2, 2);
		aThread4x4[11] = 0;

		aThread4x4[12] = graphicsTransform.p.x;
		aThread4x4[13] = graphicsTransform.p.y;
		aThread4x4[14] = graphicsTransform.p.z;
		aThread4x4[15] = 1;
	}

	void PhysicsManager::UpdateOrientation(physx::PxRigidStatic* aStaticBody, physx::PxShape** aShape, float* aThread4x4)
	{
		physx::PxU32 nShapes = aStaticBody->getNbShapes();
		aStaticBody->getShapes(aShape, nShapes);

		physx::PxTransform pT = physx::PxShapeExt::getGlobalPose(*aShape[0], *aStaticBody);
		physx::PxTransform graphicsTransform(pT.p, pT.q);
		physx::PxMat33 m = physx::PxMat33(graphicsTransform.q);

		aThread4x4[0] = m(0, 0);
		aThread4x4[1] = m(1, 0);
		aThread4x4[2] = m(2, 0);
		aThread4x4[3] = 0;

		aThread4x4[4] = m(0, 1);
		aThread4x4[5] = m(1, 1);
		aThread4x4[6] = m(2, 1);
		aThread4x4[7] = 0;

		aThread4x4[8] = m(0, 2);
		aThread4x4[9] = m(1, 2);
		aThread4x4[10] = m(2, 2);
		aThread4x4[11] = 0;

		aThread4x4[12] = graphicsTransform.p.x;
		aThread4x4[13] = graphicsTransform.p.y;
		aThread4x4[14] = graphicsTransform.p.z;
		aThread4x4[15] = 1;
	}

	bool PhysicsManager::GetAllowedToJump(int aId)
	{
		physx::PxControllerState state;
		myControllerManager->getController(aId)->getState(state);
		return state.touchedActor != nullptr;
	}

	void PhysicsManager::SetPosition(int aId, const CU::Vector3<float>& aPosition)
	{
		//DL_ASSERT("Not impl. yet");
		physx::PxControllerFilters filter;
		myControllerManager->getController(aId)->setFootPosition(physx::PxExtendedVec3(aPosition.x, aPosition.y, aPosition.z));
	}

	void PhysicsManager::GetPosition(int aId, CU::Vector3<float>& aPositionOut)
	{
		aPositionOut = myControllerPositions[myCurrentIndex ^ 1][aId];
	}

	void PhysicsManager::Create(PhysicsComponent* aComponent, const PhysicsCallbackStruct& aPhysData
		, float* aOrientation, const std::string& aFBXPath
		, physx::PxRigidDynamic** aDynamicBodyOut, physx::PxRigidStatic** aStaticBodyOut
		, physx::PxShape*** someShapesOut, bool aShouldAddToScene, bool aShouldBeSphere)
	{
		if (myPhysicsThread != nullptr)
		{
			bool changeLaterToAssert = true;
		}


		physx::PxPhysics* core = GetCore();

		physx::PxReal density = 1.f;

		//Use 4x4float here instead of orientation
		physx::PxMat44 matrix(aOrientation);
		physx::PxTransform transform(matrix);

		*aStaticBodyOut = nullptr;
		*aDynamicBodyOut = nullptr;

		if (aPhysData.myData->myPhysicsType == ePhysics::STATIC)
		{
			physx::PxTriangleMesh* mesh = GetPhysMesh(aFBXPath);

			*aStaticBodyOut = core->createRigidStatic(transform);
			(*aStaticBodyOut)->createShape(physx::PxTriangleMeshGeometry(mesh), *myDefaultMaterial);
			(*aStaticBodyOut)->setName(aFBXPath.c_str());
			(*aStaticBodyOut)->userData = aComponent;
			if (aShouldAddToScene == true)
			{
				GetScene()->addActor(*(*aStaticBodyOut));
			}
			
			physx::PxU32 nShapes = (*aStaticBodyOut)->getNbShapes();

			*someShapesOut = new physx::PxShape*[nShapes];

			physx::PxShape** statShape = new physx::PxShape*;
			(*aStaticBodyOut)->getShapes(statShape, nShapes);

			physx::PxFilterData fd = (*statShape)->getSimulationFilterData();
			fd.word0 = physx::PxU32(DETACHABLE_FLAG);
			fd.word1 = physx::PxU32(SNOWBALL_FLAG);
			(*statShape)->setSimulationFilterData(fd);
		}
		else if (aPhysData.myData->myPhysicsType == ePhysics::DYNAMIC)
		{
			if (aShouldBeSphere == true)
			{
				physx::PxSphereGeometry geometry;
				geometry.radius = aPhysData.myData->myPhysicsMax.x;
				*aDynamicBodyOut = physx::PxCreateDynamic(*core, transform, geometry, *myDefaultMaterial, density);
			}
			else
			{
				physx::PxVec3 dimensions(
					aPhysData.myData->myPhysicsMax.x - aPhysData.myData->myPhysicsMin.x
					, aPhysData.myData->myPhysicsMax.y - aPhysData.myData->myPhysicsMin.y
					, aPhysData.myData->myPhysicsMax.z - aPhysData.myData->myPhysicsMin.z);
				physx::PxBoxGeometry geometry(dimensions / 2.f);
				*aDynamicBodyOut = physx::PxCreateDynamic(*core, transform, geometry, *myDefaultMaterial, density);
			}
			(*aDynamicBodyOut)->setAngularDamping(0.75);
			(*aDynamicBodyOut)->setLinearVelocity(physx::PxVec3(0, 0, 0));
			(*aDynamicBodyOut)->setName(aFBXPath.c_str());
			(*aDynamicBodyOut)->userData = aComponent;
			if (aShouldAddToScene == true)
			{
				GetScene()->addActor(*(*aDynamicBodyOut));
			}
			physx::PxU32 nShapes = (*aDynamicBodyOut)->getNbShapes();

			*someShapesOut = new physx::PxShape*[nShapes];


			

			physx::PxShape** dynShape = new physx::PxShape*;
			(*aDynamicBodyOut)->getShapes(dynShape, nShapes);
			
			physx::PxFilterData fd = (*dynShape)->getSimulationFilterData();
			fd.word0 = physx::PxU32(SNOWBALL_FLAG);
			fd.word1 = physx::PxU32(DETACHABLE_FLAG);
			(*dynShape)->setSimulationFilterData(fd);

		}
		else if (aPhysData.myData->myPhysicsType == ePhysics::KINEMATIC)
		{
			//insane flip to make enemy kinematic objects correct on client side (flip X and Y values)
			physx::PxVec3 dimensions(
				aPhysData.myData->myPhysicsMax.y - aPhysData.myData->myPhysicsMin.y
				, aPhysData.myData->myPhysicsMax.x - aPhysData.myData->myPhysicsMin.x
				, aPhysData.myData->myPhysicsMax.z - aPhysData.myData->myPhysicsMin.z);
			physx::PxBoxGeometry geometry(dimensions / 2.f);
			*aDynamicBodyOut = physx::PxCreateDynamic(*core, transform, geometry, *myDefaultMaterial, density);
			(*aDynamicBodyOut)->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
			(*aDynamicBodyOut)->setGlobalPose(transform);
			//(*aDynamicBodyOut)->setAngularDamping(0.75);
			//(*aDynamicBodyOut)->setLinearVelocity(physx::PxVec3(0, 0, 0));
			(*aDynamicBodyOut)->setName(aFBXPath.c_str());
			(*aDynamicBodyOut)->userData = aComponent;
			if (aShouldAddToScene == true)
			{
				GetScene()->addActor(*(*aDynamicBodyOut));
			}
			physx::PxU32 nShapes = (*aDynamicBodyOut)->getNbShapes();

			*someShapesOut = new physx::PxShape*[nShapes];
		}
		else if (aPhysData.myData->myPhysicsType == ePhysics::PHANTOM)
		{
			if (aShouldAddToScene == true)
			{
				physx::PxVec3 dimensions(
					aPhysData.myData->myPhysicsMax.x - aPhysData.myData->myPhysicsMin.x
					, aPhysData.myData->myPhysicsMax.y - aPhysData.myData->myPhysicsMin.y
					, aPhysData.myData->myPhysicsMax.z - aPhysData.myData->myPhysicsMin.z);
				physx::PxBoxGeometry geometry(dimensions / 2.f);
				*aStaticBodyOut = physx::PxCreateStatic(*core, transform, geometry, *myDefaultMaterial);
			}
			else
			{
				physx::PxSphereGeometry geometry;
				geometry.radius = aPhysData.myData->myPhysicsMax.x;
				*aStaticBodyOut = physx::PxCreateStatic(*core, transform, geometry, *myDefaultMaterial);
			}

			(*aStaticBodyOut)->userData = aComponent;
			(*aStaticBodyOut)->setName("Phantom");

			physx::PxU32 nShapes = (*aStaticBodyOut)->getNbShapes();
			*someShapesOut = new physx::PxShape*[nShapes];

			physx::PxShape* treasureShape;
			(*aStaticBodyOut)->getShapes(&treasureShape, 1);

			treasureShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
			treasureShape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
			if (aShouldAddToScene == true)
			{
				GetScene()->addActor(*(*aStaticBodyOut));
			}
		}

		if (aPhysData.myData->myPhysicsType != ePhysics::STATIC)
		{
			myPhysicsComponentCallbacks.Add(aPhysData);
		}
	}
	
	void PhysicsManager::Add(physx::PxRigidDynamic* aDynamic)
	{
		myActorsToAdd[myCurrentIndex].Add(aDynamic);
	}

	void PhysicsManager::Add(physx::PxRigidStatic* aStatic)
	{
		myActorsToAdd[myCurrentIndex].Add(aStatic);
	}

	void PhysicsManager::Add(int aCapsuleID)
	{
		myCapsulesToAdd[myCurrentIndex].Add(aCapsuleID);
		//myScene->addActor(*myControllerManager->getController(aCapsuleID)->getActor());
	}

	void PhysicsManager::Remove(physx::PxRigidDynamic* aDynamic, const PhysicsComponentData& aData)
	{
		if (aDynamic != nullptr && aDynamic->getScene() != nullptr)
		{
			//GetScene()->removeActor(*aDynamic);
			myActorsToRemove[myCurrentIndex].Add(aDynamic);
			for (int i = 0; i < myPhysicsComponentCallbacks.Size(); ++i)
			{
				if (myPhysicsComponentCallbacks[i].myData == &aData)
				{
					myPhysicsComponentCallbacks.RemoveCyclicAtIndex(i);
					break;
				}
			}
		}
	}

	void PhysicsManager::Remove(physx::PxRigidStatic* aStatic, const PhysicsComponentData& aData)
	{
		if (aStatic != nullptr && aStatic->getScene() != nullptr)
		{
			//GetScene()->removeActor(*aStatic);
			myActorsToRemove[myCurrentIndex].Add(aStatic);
			for (int i = 0; i < myPhysicsComponentCallbacks.Size(); ++i)
			{
				if (myPhysicsComponentCallbacks[i].myData == &aData)
				{
					myPhysicsComponentCallbacks.RemoveCyclicAtIndex(i);
					break;
				}
			}
		}
	}

	void PhysicsManager::Remove(int aCapsuleID)
	{
		//myControllerManager->getController(aCapsuleID)->release();
		myActorsToRemove[myCurrentIndex].Add(myControllerManager->getController(aCapsuleID)->getActor());
	}

	void PhysicsManager::Sleep(physx::PxRigidDynamic* aDynamic)
	{
		myActorsToSleep[myCurrentIndex].Add(aDynamic);
	}

	void PhysicsManager::Sleep(int aCapsuleID)
	{
		myActorsToSleep[myCurrentIndex].Add(myControllerManager->getController(aCapsuleID)->getActor());
	}

	void PhysicsManager::Wake(physx::PxRigidDynamic* aDynamic)
	{
		myActorsToWakeUp[myCurrentIndex].Add(aDynamic);
	}

	void PhysicsManager::Wake(int aCapsuleID)
	{
		myActorsToWakeUp[myCurrentIndex].Add(myControllerManager->getController(aCapsuleID)->getActor());
	}

	physx::PxTriangleMesh* PhysicsManager::GetPhysMesh(const std::string& aFBXPath)
	{
		std::string objPath(aFBXPath);
		std::string cowPath(aFBXPath);

		objPath[aFBXPath.size() - 3] = 'o';
		objPath[aFBXPath.size() - 2] = 'b';
		objPath[aFBXPath.size() - 1] = 'j';

		if (myIsServer == true)
		{
			//cowPath = CU::GetGeneratedDataFolderFilePath(aFBXPath, "cos");

			cowPath = CU::GetMyDocumentsDataPath(aFBXPath, "cos");
		}
		else
		{
			//cowPath = CU::GetGeneratedDataFolderFilePath(aFBXPath, "cow");

			cowPath = CU::GetMyDocumentsDataPath(aFBXPath, "cow");
		}

		physx::PxTriangleMesh* mesh = nullptr;
		WavefrontObj wfo;

		bool ok = true;

#ifdef GENERATE_COW
		if (CU::FileExists(cowPath) == false)
		{

			if (!wfo.loadObj(objPath.c_str(), false))
			{
				DL_ASSERT(CU::Concatenate("Error loading file: %s", objPath.c_str()));
			}

			physx::PxTriangleMeshDesc meshDesc;
			meshDesc.points.count = wfo.mVertexCount;
			meshDesc.triangles.count = wfo.mTriCount;
			meshDesc.points.stride = sizeof(float) * 3;
			meshDesc.triangles.stride = sizeof(int) * 3;
			meshDesc.points.data = wfo.mVertices;
			meshDesc.triangles.data = wfo.mIndices;

			{
				physx::PxDefaultFileOutputStream stream(cowPath.c_str());
				ok = GetCooker()->cookTriangleMesh(meshDesc, stream);
			}
		}
#endif

		if (ok)
		{
			physx::PxDefaultFileInputData stream(cowPath.c_str());
			mesh = GetCore()->createTriangleMesh(stream);
		}

		return mesh;
	}

	void PhysicsManager::EndFrame()
	{
		while (myIsSwapping == true)
		{
		}
		myIsReading = true;

		for (int i = 0; i < myRaycastResults[myCurrentIndex].Size(); ++i)
		{
			myRaycastResults[myCurrentIndex][i].myFunctionToCall(myRaycastResults[myCurrentIndex][i].myPhysicsComponent
				, myRaycastResults[myCurrentIndex][i].myDirection, myRaycastResults[myCurrentIndex][i].myHitPosition
				, myRaycastResults[myCurrentIndex][i].myHitNormal);
		}
		myRaycastResults[myCurrentIndex].RemoveAll();
		//myMoveJobs[myCurrentIndex].RemoveAll();

		myIsReading = false;
	}

	void PhysicsManager::SetPlayerOrientation(CU::Matrix44<float>* aPlayerOrientation)
	{
		myPlayerOrientation = aPlayerOrientation;
	}

	void PhysicsManager::SetIsClientSide(bool aIsClientSide)
	{
		myIsClientSide = aIsClientSide;
	}

	void PhysicsManager::SetPlayerCapsule(int anID)
	{
		myPlayerCapsule = anID;
	}

	void PhysicsManager::SetInputComponentData(const InputComponentData& aPlayerInputData)
	{
		myPlayerInputData = &aPlayerInputData;
	}

	void PhysicsManager::SetPlayerGID(int anID)
	{
		myPlayerGID = anID;
	}

}