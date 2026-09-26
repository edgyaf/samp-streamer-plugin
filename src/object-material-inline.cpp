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

#include "object-material-inline.h"
#include "core.h"

namespace
{
	constexpr int CREATE_OBJECT_RPC = 44;
	constexpr int SET_OBJECT_MATERIAL_RPC = 84;

	// Layout of omp's NetCode::RPC::CreateObject for an unattached object: id 16, model 32,
	// position 96, rotation 96, draw distance 32, camera collision 8, attached vehicle 16,
	// attached object 16, materials count 8. The client parses inline materials after the count.
	constexpr int CREATE_OBJECT_BITS = 320;
	constexpr int ATTACHED_VEHICLE_OFFSET = 280;
	constexpr int ATTACHED_OBJECT_OFFSET = 296;
	constexpr int MATERIALS_COUNT_OFFSET = 312;
	// SetObjectMaterial is the object id followed by exactly the entry CreateObject carries inline
	constexpr int MATERIAL_ENTRY_OFFSET = 16;

	void appendBits(NetworkBitStream &output, const std::vector<uint8_t> &data, int from, int to)
	{
		NetworkBitStream input(const_cast<unsigned char *>(data.data()), static_cast<unsigned int>(data.size()), false);
		input.SetReadOffset(from);
		std::vector<uint8_t> bits(bitsToBytes(to - from));
		input.ReadBits(bits.data(), to - from, false);
		output.WriteBits(bits.data(), to - from, false);
	}
}

ObjectMaterialInliner::ObjectMaterialInliner()
{
	enabled = true;
	capturePlayerId = INVALID_PLAYER_ID;
	capturePeer = nullptr;
	createObject.bits = 0;
}

void ObjectMaterialInliner::begin(int playerId)
{
	reset();
	capturePlayerId = playerId;
}

ObjectMaterialInliner::SentRpcs ObjectMaterialInliner::finish()
{
	SentRpcs sent;
	if (capturePeer && createObject.bits)
	{
		INetwork *network = capturePeer->getNetworkData().network;
		if (network)
		{
			sent = (!materials.empty() && isMergeable()) ? sendMerged(network) : sendSeparately(network);
		}
	}
	reset();
	return sent;
}

bool ObjectMaterialInliner::onCreateObject(IPlayer *peer, NetworkBitStream &bs)
{
	if (capturePlayerId == INVALID_PLAYER_ID || !peer || peer->getID() != capturePlayerId || createObject.bits)
	{
		return true;
	}
	capturePeer = peer;
	createObject.bits = bs.GetNumberOfBitsUsed();
	createObject.data.assign(bs.GetData(), bs.GetData() + bitsToBytes(createObject.bits));
	return false;
}

bool ObjectMaterialInliner::onSetObjectMaterial(IPlayer *peer, NetworkBitStream &bs)
{
	if (capturePlayerId == INVALID_PLAYER_ID || peer != capturePeer || !createObject.bits)
	{
		return true;
	}
	CapturedRpc material;
	material.bits = bs.GetNumberOfBitsUsed();
	material.data.assign(bs.GetData(), bs.GetData() + bitsToBytes(material.bits));
	materials.push_back(std::move(material));
	return false;
}

bool ObjectMaterialInliner::isMergeable() const
{
	if (createObject.bits != CREATE_OBJECT_BITS)
	{
		return false;
	}
	NetworkBitStream bs(const_cast<unsigned char *>(createObject.data.data()), static_cast<unsigned int>(createObject.data.size()), false);
	uint16_t attachedVehicle = 0, attachedObject = 0;
	uint8_t materialsCount = 1;
	bs.SetReadOffset(ATTACHED_VEHICLE_OFFSET);
	bs.readUINT16(attachedVehicle);
	bs.SetReadOffset(ATTACHED_OBJECT_OFFSET);
	bs.readUINT16(attachedObject);
	bs.SetReadOffset(MATERIALS_COUNT_OFFSET);
	bs.readUINT8(materialsCount);
	return attachedVehicle == INVALID_VEHICLE_ID && attachedObject == INVALID_OBJECT_ID && !materialsCount;
}

ObjectMaterialInliner::SentRpcs ObjectMaterialInliner::sendMerged(INetwork *network)
{
	NetworkBitStream bs;
	appendBits(bs, createObject.data, 0, MATERIALS_COUNT_OFFSET);
	bs.writeUINT8(static_cast<uint8_t>(materials.size()));
	for (const CapturedRpc &material : materials)
	{
		appendBits(bs, material.data, MATERIAL_ENTRY_OFFSET, material.bits);
	}
	network->sendRPC(*capturePeer, CREATE_OBJECT_RPC, Span<uint8_t>(bs.GetData(), bs.GetNumberOfBitsUsed()), OrderingChannel_SyncRPC, false);
	SentRpcs sent;
	sent.bytes = bitsToBytes(bs.GetNumberOfBitsUsed()) + StreamCost::MESSAGE_OVERHEAD_BYTES;
	sent.messages = 1;
	return sent;
}

ObjectMaterialInliner::SentRpcs ObjectMaterialInliner::sendSeparately(INetwork *network)
{
	SentRpcs sent;
	network->sendRPC(*capturePeer, CREATE_OBJECT_RPC, Span<uint8_t>(createObject.data.data(), createObject.bits), OrderingChannel_SyncRPC, false);
	sent.bytes = bitsToBytes(createObject.bits) + StreamCost::MESSAGE_OVERHEAD_BYTES;
	sent.messages = 1;
	for (CapturedRpc &material : materials)
	{
		network->sendRPC(*capturePeer, SET_OBJECT_MATERIAL_RPC, Span<uint8_t>(material.data.data(), material.bits), OrderingChannel_SyncRPC, false);
		sent.bytes += bitsToBytes(material.bits) + StreamCost::MESSAGE_OVERHEAD_BYTES;
		++sent.messages;
	}
	return sent;
}

void ObjectMaterialInliner::reset()
{
	capturePlayerId = INVALID_PLAYER_ID;
	capturePeer = nullptr;
	createObject.data.clear();
	createObject.bits = 0;
	materials.clear();
}

bool ObjectMaterialInlineHooks::CreateObjectHook::onSend(IPlayer *peer, NetworkBitStream &bs)
{
	return !core || core->getMaterialInliner()->onCreateObject(peer, bs);
}

bool ObjectMaterialInlineHooks::SetObjectMaterialHook::onSend(IPlayer *peer, NetworkBitStream &bs)
{
	return !core || core->getMaterialInliner()->onSetObjectMaterial(peer, bs);
}

void ObjectMaterialInlineHooks::registerOn(INetwork *network)
{
	network->getPerRPCOutEventDispatcher().addEventHandler(&createObjectHook, CREATE_OBJECT_RPC);
	network->getPerRPCOutEventDispatcher().addEventHandler(&setObjectMaterialHook, SET_OBJECT_MATERIAL_RPC);
}
