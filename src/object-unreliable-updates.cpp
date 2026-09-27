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

#include "main.h"

#include "bitstream.hpp"

#include "object-unreliable-updates.h"
#include "core.h"

namespace
{
	constexpr int SET_OBJECT_POSITION_RPC = 45;
	constexpr int SET_OBJECT_ROTATION_RPC = 46;
	// ID_RPC in omp's RAKNET_LEGACY build; an RPC is sent as this byte, the RPC id, the compressed
	// payload bit length and the payload, which the client handles regardless of reliability
	constexpr uint8_t ID_RPC = 20;
}

ObjectUnreliableUpdates::ObjectUnreliableUpdates()
{
	capturePlayerId = INVALID_PLAYER_ID;
	capturePeer = nullptr;
	captureRpcId = 0;
	bits = 0;
}

void ObjectUnreliableUpdates::begin(int playerId)
{
	capturePlayerId = playerId;
	capturePeer = nullptr;
	bits = 0;
}

void ObjectUnreliableUpdates::finish(Player &player)
{
	capturePlayerId = INVALID_PLAYER_ID;
	if (!capturePeer || !bits)
	{
		return;
	}
	INetwork *network = capturePeer->getNetworkData().network;
	if (network && !(core->getNetworkPacer()->getEnabled() && core->getNetworkPacer()->isCongested(player)))
	{
		NetworkBitStream bs;
		bs.writeUINT8(ID_RPC);
		bs.writeUINT8(static_cast<uint8_t>(captureRpcId));
		bs.WriteCompressed(static_cast<unsigned int>(bits));
		bs.WriteBits(data.data(), bits, false);
		network->sendPacket(*capturePeer, Span<uint8_t>(bs.GetData(), bs.GetNumberOfBitsUsed()), OrderingChannel_Unordered, false);
	}
	capturePeer = nullptr;
	bits = 0;
}

bool ObjectUnreliableUpdates::onObjectUpdate(IPlayer *peer, NetworkBitStream &bs, int rpcId)
{
	if (capturePlayerId == INVALID_PLAYER_ID || !peer || peer->getID() != capturePlayerId || bits)
	{
		return true;
	}
	capturePeer = peer;
	captureRpcId = rpcId;
	bits = bs.GetNumberOfBitsUsed();
	data.assign(bs.GetData(), bs.GetData() + bitsToBytes(bits));
	return false;
}

bool ObjectUnreliableUpdateHooks::SetObjectPositionHook::onSend(IPlayer *peer, NetworkBitStream &bs)
{
	return !core || core->getUnreliableUpdates()->onObjectUpdate(peer, bs, SET_OBJECT_POSITION_RPC);
}

bool ObjectUnreliableUpdateHooks::SetObjectRotationHook::onSend(IPlayer *peer, NetworkBitStream &bs)
{
	return !core || core->getUnreliableUpdates()->onObjectUpdate(peer, bs, SET_OBJECT_ROTATION_RPC);
}

void ObjectUnreliableUpdateHooks::registerOn(INetwork *network)
{
	network->getPerRPCOutEventDispatcher().addEventHandler(&setObjectPositionHook, SET_OBJECT_POSITION_RPC);
	network->getPerRPCOutEventDispatcher().addEventHandler(&setObjectRotationHook, SET_OBJECT_ROTATION_RPC);
}
