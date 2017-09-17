/**
 * Copyright (c) 2017 Reiji Nishiyama. All rights reserved.
 * Licensed under the MIT license.
 * See LICENSE file in the repo for full license information.
 */

#ifndef IM920_H
#define IM920_H

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include <inttypes.h>

#define FRAME_PAYLOAD_SIZE	64
#define IM920_PACKET_HEADER_SIZE	3

#define IM920_PACKET_PAYLOAD_SIZE	(FRAME_PAYLOAD_SIZE - IM920_PACKET_HEADER_SIZE)
#define IM920_PACKET_DATA		0
#define IM920_PACKET_COMMAND	1
#define IM920_PACKET_ACK		2
#define IM920_PACKET_NOTICE		3
#define IM920_PACKET_TYPE		4

#define COMMAND_IM920_CMD	1

class IM920Frame
{
private:
	uint8_t _nodeID;

	uint16_t _moduleID;

	int8_t _rssi;

	uint8_t _payload[FRAME_PAYLOAD_SIZE];

	size_t _p;

	size_t _rp;


public:
	IM920Frame();

	~IM920Frame();

	size_t put(uint8_t data);

	uint8_t getNextByte();

	uint8_t* getArray() { return _payload; };

	const uint8_t* getArray() const { return _payload; };

	uint8_t* getTerminator() { return _payload + _p; };

	const uint8_t* getTerminator() const { return _payload + _p; };

	void clear();

	size_t getFrameLength() const { return _p; };

	uint8_t getNodeID() const { return _nodeID; };

	uint16_t getModuleID() const { return _moduleID; };

	uint8_t getRSSI() const { return _rssi; };

	size_t resetFrameLength(size_t length);

	void setNodeID(uint8_t nodeID) { _nodeID = nodeID; };

	void setModuleID(uint16_t moduleID) { _moduleID = moduleID; };

	void setRSSI(int8_t rssi) { _rssi = rssi; };

};

class PacketOperator
{
protected:
	PacketOperator() {};

	virtual ~PacketOperator() {};

public:
	static PacketOperator& refInstance(int type);

	static PacketOperator& refInstance(const IM920Frame& frame);

	virtual void reset(IM920Frame& frame, size_t size=0) const;

	void resetPayloadLength(IM920Frame& frame, size_t size=0) const;

	size_t getPayloadLength(const IM920Frame& frame) const;

	uint8_t* getPayloadArray(IM920Frame& frame) const;

	const uint8_t* getPayloadArray(const IM920Frame& frame) const;

	uint8_t* getPayloadTerminator(IM920Frame& frame) const;

	const uint8_t* getPayloadTerminator(const IM920Frame& frame) const;

	size_t getPacketHeaderLength(const IM920Frame& frame) const;

	size_t getPacketLength(const IM920Frame& frame) const;

	int getPacketType(const IM920Frame& frame) const;

	bool isEncoded(const IM920Frame& frame) const;

	bool isFragmented(const IM920Frame& frame) const;

	bool isAckRequested(const IM920Frame& frame) const;

	uint8_t getFrameID(const IM920Frame& frame) const;

	void setPacketLength(IM920Frame& frame, size_t length) const;

	void setPacketType(IM920Frame& frame, uint8_t type) const;

	void setEncode(IM920Frame& frame, bool encode) const;

	void setFragment(IM920Frame& frame, bool fragment) const;

	void setAckRequest(IM920Frame& frame, bool request) const;

	void setFrameID(IM920Frame& frame, uint8_t num) const;

	void updatePacketLength(IM920Frame& frame);

};

class AckPacket : public PacketOperator
{
private:
	static AckPacket* _instance;


protected:
	AckPacket();

	virtual ~AckPacket();

public:
	static AckPacket& Instance();

	void reset(IM920Frame& frame, size_t size=0) const;

	uint8_t getCommand(const IM920Frame& frame) const;

	void setCommand(IM920Frame& frame, uint8_t cmd) const;

	size_t getResponseLength(const IM920Frame& frame) const;

	size_t getResponse(const IM920Frame& frame, char buf[]) const;

	const char* getResponse(const IM920Frame& frame) const;

	size_t setResponse(IM920Frame& frame, const char response[]) const;

};

class CommandPacket : public PacketOperator
{
private:
	static CommandPacket* _instance;


protected:
	CommandPacket();

	virtual ~CommandPacket();

public:
	static CommandPacket& Instance();

	void reset(IM920Frame& frame, size_t size=0) const;

	uint8_t getCommand(const IM920Frame& frame) const;

	void setCommand(IM920Frame& frame, uint8_t cmd) const;

	size_t getCommandParamLength(const IM920Frame& frame) const;

	size_t getCommandParam(const IM920Frame& frame, char buf[]) const;

	const char* getCommandParam(const IM920Frame& frame) const;

	size_t setCommandParam(IM920Frame& frame, const char param[]) const;

};

class DataPacket : public PacketOperator
{
private:
	static DataPacket* _instance;


protected:
	DataPacket();

	virtual ~DataPacket();

public:
	static DataPacket& Instance();

	void reset(IM920Frame& frame, size_t size=0) const;

	int decodeData(IM920Frame& frame) const;

	int encodeData(IM920Frame& frame) const;

	size_t getDataLength(const IM920Frame& frame) const;

	size_t getData(const IM920Frame& frame, uint8_t buf[]) const;

	const uint8_t* getData(const IM920Frame& frame) const;

	size_t getDataWithDecode(IM920Frame& frame, uint8_t buf[]) const;

	size_t getDecodeLength(IM920Frame& frame) const;

	size_t getEncodeLength(IM920Frame& frame) const;

	size_t setData(IM920Frame& frame, const uint8_t data[], size_t length) const;

	size_t setDataWithEncode(IM920Frame& frame, uint8_t data[], size_t length) const;

};

class NoticePacket : public PacketOperator
{
private:
	static NoticePacket* _instance;


protected:
	NoticePacket();

	virtual ~NoticePacket();

public:
	static NoticePacket& Instance();

	void reset(IM920Frame& frame, size_t size=0) const;

	size_t getNoticeLength(const IM920Frame& frame) const;

	size_t getNotice(const IM920Frame& frame, char buf[]) const;

	const char* getNotice(const IM920Frame& frame) const;

	size_t setNotice(IM920Frame& frame, char notice[]) const;

};

class IM920Interface
{
private:
	int _resetPin;

	int _busyPin;

	uint16_t _activeTime;

	uint16_t _sleepTime;

	unsigned long _usTxTimePerByte;

	bool _initialized;

	unsigned long _timeout;

	Stream* _serial;

private:
	int _exec(const char cmd[], const char search[]);

	bool _isBusy();

	size_t _getResponse(char buf[], size_t length);

public:
	IM920Interface();

	~IM920Interface();

	void begin(Stream& serial, int resetPin, int busyPin, long baud = 19200);

	void end();

	size_t available();

	uint8_t read();

	size_t readBytesUntil(char character, uint8_t buf[], size_t length);

	size_t sendBytes(const uint8_t data[], size_t length);

	void setTimeout(unsigned long timeout);

	int8_t parseInt8();

	int16_t parseInt16();

	int32_t parseInt32();

	size_t execIM920Cmd(const char command[], char response[], size_t length);

	unsigned long getTxTimePerByte();

	int enableSleep();

	int disableSleep();

	uint16_t getActiveDuration();

	uint16_t getSleepDuration();

	void setActiveDuration(uint16_t activeTime);

	void setSleepDuration(uint16_t sleepTime);

	int resetInterface();

};

class IM920
{
private:
	IM920Interface _im920;

private:
	IM920() {};

	~IM920() { _im920.end(); };

	uint8_t _getNextFrameID();

	size_t _send(IM920Frame& frame);

public:
	static IM920& Instance();

	void begin(Stream& serial, int resetPin, int busyPin, long baud) { _im920.begin(serial, resetPin, busyPin, baud); };

	void end() { _im920.end(); };

	int listen(IM920Frame& frame, long timeout);

	int send(IM920Frame& frame);

	size_t sendData(const uint8_t data[], size_t length, bool fragment);

	int sendCommand(uint8_t cmd, const char param[]);

	int sendCommandWithAck(uint8_t cmd, const char param[]);

	int sendAck(uint8_t cmd, const char response[]);

	int sendNotice(const char notice[]);

};

#endif /* IM920_H */
