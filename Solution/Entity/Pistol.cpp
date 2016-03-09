#include "stdafx.h"

#include "DamageNote.h"
#include "Entity.h"
#include <EmitterMessage.h>
#include <PostMaster.h>
#include <PhysEntity.h>
#include <PhysicsInterface.h>
#include "Pistol.h"
#include <XMLReader.h>

Pistol::Pistol()
	: Weapon(eWeaponType::PISTOL)
{
	XMLReader reader;
	reader.OpenDocument("Data/Setting/SET_weapons.xml");
	tinyxml2::XMLElement* root = reader.ForceFindFirstChild("root");

	tinyxml2::XMLElement* pistolElement = reader.ForceFindFirstChild(root, "pistol");

	reader.ForceReadAttribute(reader.ForceFindFirstChild(pistolElement, "clipsize"), "value", myClipSize);
	reader.ForceReadAttribute(reader.ForceFindFirstChild(pistolElement, "damage"), "value", myDamage);

	myAmmoInClip = myClipSize;
	myAmmoTotal = INT_MAX;

	reader.CloseDocument();

	myRaycastHandler = [=](Entity* anEntity, const CU::Vector3<float>& aDirection, const CU::Vector3<float>& aHitPosition)
	{
		this->HandleRaycast(anEntity, aDirection, aHitPosition);
	};
}

Pistol::~Pistol()
{
}


void Pistol::Shoot(const CU::Matrix44<float>& aOrientation)
{
	if (myAmmoInClip > 0)
	{
		Prism::PhysicsInterface::GetInstance()->RayCast(aOrientation.GetPos()
			, aOrientation.GetForward(), 500.f, myRaycastHandler);
		myAmmoInClip -= 1;
	}
}

void Pistol::Reload()
{
	myAmmoInClip = myClipSize;
}

void Pistol::HandleRaycast(Entity* anEntity, const CU::Vector3<float>& aDirection, const CU::Vector3<float>& aHitPosition)
{
	if (anEntity != nullptr)
	{
		if (anEntity->GetPhysEntity()->GetPhysicsType() == ePhysics::DYNAMIC)
		{
			anEntity->GetPhysEntity()->AddForce(aDirection, 25.f);
		}
		PostMaster::GetInstance()->SendMessage(EmitterMessage("Shotgun", aHitPosition));
		anEntity->SendNote<DamageNote>(DamageNote(myDamage));

	}
}