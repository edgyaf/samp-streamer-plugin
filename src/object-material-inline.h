/*
 * Copyright (C) 2017 Incognito
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OBJECT_MATERIAL_INLINE_H
#define OBJECT_MATERIAL_INLINE_H

#include <cstdint>
#include <vector>

// Sends an object's materials inside its CreateObject RPC instead of one SetObjectMaterial RPC
// per slot. omp still receives every setMaterial call, so its object state stays complete; only
// the packets it would send are held back and merged.
class ObjectMaterialInliner
{
public:
	struct SentRpcs
	{
		int bytes = 0;
		int messages = 0;
	};

	ObjectMaterialInliner();

	inline bool getEnabled()
	{
		return enabled;
	}

	inline void setEnabled(bool value)
	{
		enabled = value;
	}

	void begin(int playerId);
	SentRpcs finish();

	bool onCreateObject(IPlayer *peer, NetworkBitStream &bs);
	bool onSetObjectMaterial(IPlayer *peer, NetworkBitStream &bs);

private:
	struct CapturedRpc
	{
		std::vector<uint8_t> data;
		int bits;
	};

	bool isMergeable() const;
	SentRpcs sendMerged(INetwork *network);
	SentRpcs sendSeparately(INetwork *network);
	void reset();

	bool enabled;
	int capturePlayerId;
	IPlayer *capturePeer;
	CapturedRpc createObject;
	std::vector<CapturedRpc> materials;
};

// Lives in the omp component rather than in Core, because network dispatchers keep these
// pointers after Core is reset on Pawn unload
struct ObjectMaterialInlineHooks
{
	struct CreateObjectHook : public SingleNetworkOutEventHandler
	{
		bool onSend(IPlayer *peer, NetworkBitStream &bs) override;
	};

	struct SetObjectMaterialHook : public SingleNetworkOutEventHandler
	{
		bool onSend(IPlayer *peer, NetworkBitStream &bs) override;
	};

	void registerOn(INetwork *network);

	CreateObjectHook createObjectHook;
	SetObjectMaterialHook setObjectMaterialHook;
};

#endif
