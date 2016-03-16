#pragma once
#include <GrowingArray.h>
#include <Matrix.h>
#include <NetworkMessageTypes.h>
#include <SharedLevel.h>

namespace Prism
{
	class Camera;
	class DeferredRenderer;
	class Scene;
	class Instance;
	class Room;
	class PointLight;
}

class EmitterManager;

class ClientLevel : public SharedLevel
{
public:
	ClientLevel();
	~ClientLevel();

	void Update(const float aDeltaTime) override;
	void Render();
	bool connected;
	Prism::Scene* GetScene();

	void ReceiveNetworkMessage(const NetMessageOnJoin& aMessage, const sockaddr_in& aSenderAddress) override;
	void ReceiveNetworkMessage(const NetMessageConnectMessage& aMessage, const sockaddr_in& aSenderAddress) override;
	void ReceiveNetworkMessage(const NetMessageDisconnect& aMessage, const sockaddr_in& aSenderAddress) override;
	void ReceiveNetworkMessage(const NetMessageAddEnemy& aMessage, const sockaddr_in& aSenderAddress) override;
	void ReceiveNetworkMessage(const NetMessageOnDeath& aMessage, const sockaddr_in& aSenderAddress) override;

	void AddLight(Prism::PointLight* aLight);

	void DebugMusic();

private:
	Prism::Scene* myScene;
	Prism::DeferredRenderer* myDeferredRenderer;

	CU::GrowingArray<Entity*> myInstances;
	CU::GrowingArray<CU::Matrix44f> myInstanceOrientations;
	CU::GrowingArray<Prism::PointLight*> myPointLights;

	Entity* myPlayer;
	EmitterManager* myEmitterManager;
	bool myInitDone;
};

inline Prism::Scene* ClientLevel::GetScene()
{
	return myScene;
}
