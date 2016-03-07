#include "stdafx.h"

#include <CommonHelper.h>
#include <Entity.h>
#include <EntityFactory.h>
#include <GameEnum.h>
#include <MathHelper.h>
#include "SharedLevel.h"
#include "SharedLevelFactory.h"
#include <XMLReader.h>

SharedLevelFactory::SharedLevelFactory(const std::string& aLevelListPath)
	: myLevelPaths(8)
	, myCurrentID(0)
{
	EntityFactory::GetInstance()->LoadEntities("Data/Resource/Entity/LI_Entity.xml");
	if (aLevelListPath != "")
	{
		ReadLeveList(aLevelListPath);
	}
}

SharedLevelFactory::~SharedLevelFactory()
{
}

SharedLevel* SharedLevelFactory::LoadLevel(const int& aID)
{
	DL_ASSERT_EXP(aID <= myLevelPaths.Size(), "[LevelFactory] Trying to load a non-existing level! Check so the ID: "
		+ std::to_string(aID) + " are a valid id in LI_level.xml");
	myCurrentID = aID;

	return LoadCurrentLevel();
}


SharedLevel* SharedLevelFactory::LoadNextLevel()
{
	if (IsLastLevel() == true)
	{
		return myCurrentLevel;
	}
	return LoadLevel(myCurrentID + 1);
}

void SharedLevelFactory::ReadLeveList(const std::string& aLevelListPath)
{
	myLevelPaths.RemoveAll();
	XMLReader reader;
	reader.OpenDocument(aLevelListPath);
	std::string levelPath;

	int ID = -1;
	int lastID = ID - 1;

	tinyxml2::XMLElement* rootElement = reader.ForceFindFirstChild("root");
	for (tinyxml2::XMLElement* element = reader.FindFirstChild(rootElement); element != nullptr; element = reader.FindNextElement(element))
	{
		lastID = ID;
		reader.ForceReadAttribute(element, "ID", ID);
		reader.ForceReadAttribute(element, "path", levelPath);
		myLevelPaths.Add(LevelPathInformation(ID, levelPath));

		DL_ASSERT_EXP(ID > lastID, "[LevelFactory] Wrong ID-number in LI_level.xml! The numbers should be counting up, in order.");
		DL_ASSERT_EXP(myCurrentID < 10, "[LevelFactory] Can't handle level ID with two digits.");
	}
	reader.CloseDocument();
}
void SharedLevelFactory::ReadLevel(const std::string& aLevelPath)
{
	XMLReader reader;
	reader.OpenDocument(aLevelPath);
	tinyxml2::XMLElement* levelElement = reader.ForceFindFirstChild("root");
	levelElement = reader.ForceFindFirstChild(levelElement, "scene");

	LoadRooms(reader, levelElement);
	LoadProps(reader, levelElement);
	LoadUnits(reader, levelElement);

	reader.CloseDocument();
}

void SharedLevelFactory::ReadOrientation(XMLReader& aReader, tinyxml2::XMLElement* aElement,
	CU::Vector3f& aOutPosition, CU::Vector3f& aOutRotation, CU::Vector3f& aOutScale)
{
	tinyxml2::XMLElement* propElement = aReader.ForceFindFirstChild(aElement, "position");
	aReader.ForceReadAttribute(propElement, "X", "Y", "Z", aOutPosition);

	propElement = aReader.ForceFindFirstChild(aElement, "rotation");
	aReader.ForceReadAttribute(propElement, "X", "Y", "Z", aOutRotation);

	propElement = aReader.ForceFindFirstChild(aElement, "scale");
	aReader.ForceReadAttribute(propElement, "X", "Y", "Z", aOutScale);
}