#include "stdafx.h"

#include <CommonHelper.h>
#include "ComponentLoader.h"
#include "EntityEnumConverter.h"

void ComponentLoader::LoadAnimationComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, AnimationComponentData& aOutputData)
{
	aOutputData.myExistsInEntity = true;

	for (tinyxml2::XMLElement* e = aDocument.FindFirstChild(aSourceElement); e != nullptr; e = aDocument.FindNextElement(e))
	{
		std::string elementName = CU::ToLower(e->Name());
		if (elementName == CU::ToLower("Model"))
		{
			aDocument.ForceReadAttribute(e, "modelPath", aOutputData.myModelPath);
			aDocument.ForceReadAttribute(e, "shaderPath", aOutputData.myEffectPath);
		}
		else if (elementName == CU::ToLower("Animation"))
		{
			AnimationLoadData newAnimation;
			int animState;

			aDocument.ForceReadAttribute(e, "path", newAnimation.myAnimationPath);
			aDocument.ForceReadAttribute(e, "state", animState);
			aDocument.ForceReadAttribute(e, "loop", newAnimation.myLoopFlag);
			aDocument.ForceReadAttribute(e, "resetTime", newAnimation.myResetTimeOnRestart);
			newAnimation.myEntityState = static_cast<eEntityState>(animState);

			aOutputData.myAnimations.Insert(static_cast<int>(newAnimation.myEntityState), newAnimation);
		}
		else
		{
			FailedToReadChildElementMessage(e->Name(), aSourceElement->Name());
		}
	}
}

void ComponentLoader::LoadGraphicsComponent(XMLReader& aDocument, tinyxml2::XMLElement* aSourceElement, GraphicsComponentData& aOutputData)
{
	aOutputData.myExistsInEntity = true;
	aOutputData.myAlwaysRender = false;

	for (tinyxml2::XMLElement* e = aDocument.FindFirstChild(aSourceElement); e != nullptr; e = aDocument.FindNextElement(e))
	{
		std::string elementName = CU::ToLower(e->Name());
		if (elementName == CU::ToLower("Model"))
		{
			aDocument.ForceReadAttribute(e, "modelPath", aOutputData.myModelPath);
			aDocument.ForceReadAttribute(e, "shaderPath", aOutputData.myEffectPath);
		}
		else if (elementName == CU::ToLower("AlwaysRender"))
		{
			aOutputData.myAlwaysRender = true;
		}
		else
		{
			FailedToReadChildElementMessage(e->Name(), aSourceElement->Name());
		}
	}
}