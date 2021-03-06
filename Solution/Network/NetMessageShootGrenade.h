#pragma once
#include "NetImportantMessage.h"
#include "NetworkMessageTypes.h"

class NetMessageShootGrenade : public NetImportantMessage
{
public:
	NetMessageShootGrenade();
	NetMessageShootGrenade(int aForceStrength, int aPlayerGID, const CU::Vector3<float>& aForwardVector);
	~NetMessageShootGrenade();

	int myForceStrength;
	int myPlayerGID;
	CU::Vector3<float> myForwardVector;
private:
	void DoSerialize(StreamType& aStream) override;
	void DoDeSerialize(StreamType& aStream) override;
};

inline NetMessageShootGrenade::NetMessageShootGrenade()
	: NetImportantMessage(eNetMessageType::SHOOT_GRENADE)
{
}

inline NetMessageShootGrenade::NetMessageShootGrenade(int aForceStrength, int aPlayerGID, const CU::Vector3<float>& aForwardVector)
	: NetImportantMessage(eNetMessageType::SHOOT_GRENADE)
	, myForceStrength(aForceStrength)
	, myPlayerGID(aPlayerGID)
	, myForwardVector(aForwardVector)
{
}

inline NetMessageShootGrenade::~NetMessageShootGrenade()
{
}

inline void NetMessageShootGrenade::DoSerialize(StreamType& aStream)
{
	__super::DoSerialize(aStream);
	SERIALIZE(aStream, myForceStrength);
	SERIALIZE(aStream, myPlayerGID);
	SERIALIZE(aStream, myForwardVector);
}

inline void NetMessageShootGrenade::DoDeSerialize(StreamType& aStream)
{
	__super::DoDeSerialize(aStream);
	DESERIALIZE(aStream, myForceStrength);
	DESERIALIZE(aStream, myPlayerGID);
	DESERIALIZE(aStream, myForwardVector);
}