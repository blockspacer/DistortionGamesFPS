#pragma once
#include "NetImportantMessage.h"
#include <Vector3.h>
class NetMessageAddEnemy : public NetImportantMessage
{
public:
	NetMessageAddEnemy(const CU::Vector3<float> &aPosition, unsigned int aGID);
	NetMessageAddEnemy();
	~NetMessageAddEnemy();


	CU::Vector3<float> myPosition;
	unsigned int myGID;
protected:

	void DoSerialize(StreamType& aStream) override;
	void DoDeSerialize(StreamType& aStream) override;

};

