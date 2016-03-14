#pragma once
#include "Message.h"
#include <Vector3.h>
struct NetworkAddEnemyMessage : public Message
{
	NetworkAddEnemyMessage(const CU::Vector3<float>& aPosition, unsigned int aGID, const sockaddr_in& anAddress);
	NetworkAddEnemyMessage(const CU::Vector3<float>& aPosition, unsigned int aGID);

	CU::Vector3<float> myPosition;
	unsigned int myGID;
	sockaddr_in myAddress;
};

inline NetworkAddEnemyMessage::NetworkAddEnemyMessage(const CU::Vector3<float>& aPosition, unsigned int aGID, const sockaddr_in& anAddress)
	: Message(eMessageType::NETWORK_ADD_ENEMY)
	, myPosition(aPosition)
	, myAddress(anAddress)
	, myGID(aGID)
{
}

inline NetworkAddEnemyMessage::NetworkAddEnemyMessage(const CU::Vector3<float>& aPositon, unsigned int aGID)
	: Message(eMessageType::NETWORK_ADD_ENEMY)
	, myPosition(aPositon)
	, myGID(aGID)
{
}
