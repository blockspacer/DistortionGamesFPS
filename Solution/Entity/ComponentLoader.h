#pragma once

struct AIComponentData;
struct AnimationComponentData;
struct FirstPersonRenderComponentData;
struct GraphicsComponentData;
struct HealthComponentData;
struct InputComponentData;
struct NetworkComponentData;
struct PhysicsComponentData;
struct GrenadeComponentData;
struct ShootingComponentData;
struct TriggerComponentData;
struct UpgradeComponentData;

class XMLReader;

namespace tinyxml2
{
	class XMLElement;
}

class ComponentLoader
{
public:
	void LoadAIComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, AIComponentData& aOutputData);
	void LoadAnimationComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, AnimationComponentData& aOutputData);
	void LoadFirstPersonRenderComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, FirstPersonRenderComponentData& aOutputData);
	void LoadGraphicsComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, GraphicsComponentData& aOutputData);
	void LoadHealthComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, HealthComponentData& aOutputData);
	void LoadNetworkComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, NetworkComponentData& aOutputData);
	void LoadPhysicsComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, PhysicsComponentData& aOutputData);
	void LoadGrenadeComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, GrenadeComponentData& aOutputData);
	void LoadTriggerComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, TriggerComponentData& aOutputData);
	void LoadInputComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, InputComponentData& aOutputData);
	void LoadShootingComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, ShootingComponentData& aOutputData);
	void LoadUpgradeComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, UpgradeComponentData& aOutputData);

private:

	int ConvertToTriggerEnum(std::string aName);
};