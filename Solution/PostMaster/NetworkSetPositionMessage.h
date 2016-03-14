#pragma once
#include "Message.h"
#include <Vector.h>
struct NetworkSetPositionMessage : public Message
{
	NetworkSetPositionMessage(const CU::Vector3<float>& aPosition, float aRotationY, unsigned int aGID);


	CU::Vector3<float> myPosition;
	float myRotationY;
	unsigned int myGID;
};

inline NetworkSetPositionMessage::NetworkSetPositionMessage(const CU::Vector3<float>& aPosition, float aRotationY, unsigned int aGID)
	: Message(eMessageType::NETWORK_SET_POSITION)
	, myPosition(aPosition)
	, myRotationY(aRotationY)
	, myGID(aGID)
{
}
