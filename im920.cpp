#include "im920.h"

#define __ASSERT_USE_STDERR
#include <assert.h>
#include <DebugUtils.h>
#include <base64.hpp>

#define IM920_PACKET_LENGTH_I		0
#define IM920_PACKET_LENGTH_MASK	(0x3F)
#define IM920_PACKET_FLAG_I			1
#define IM920_PACKET_FLAG_MASK		(0x18)
#define IM920_PACKET_FLAG_MASK_ENC	(0x20)
#define IM920_PACKET_FLAG_MASK_FRAG	(0x10)
#define IM920_PACKET_FLAG_MASK_ACK	(0x08)
#define IM920_PACKET_TYPE_I			1
#define IM920_PACKET_TYPE_MASK		(0x07)
#define IM920_PACKET_FRAMEID_I		2
#define IM920_PACKET_PAYLOAD_I		IM920_PACKET_HEADER_SIZE

#define IM920_PACKET_ACK_CMD_I		0
#define IM920_PACKET_ACK_PARAM_I	1
#define ACK_COMMAND_SIZE	1
#define ACK_PARAM_LEN	( IM920_PACKET_PAYLOAD_SIZE - ACK_COMMAND_SIZE )

#define IM920_PACKET_COMMAND_CMD_I		0
#define IM920_PACKET_COMMAND_PARAM_I	1
#define COMMAND_SIZE	1
#define COMMAND_PARAM_LEN	( IM920_PACKET_PAYLOAD_SIZE - COMMAND_SIZE )

#define DATA_PACKET_PAYLOAD_SIZE	IM920_PACKET_PAYLOAD_SIZE
#define BIN_DATA_MAX_LENGTH		45
#define STRING_DATA_MAX_LENGTH	(IM920_PACKET_PAYLOAD_SIZE - 1)

#define NOTICE_MAX_LEN	IM920_PACKET_PAYLOAD_SIZE

AckPacket* AckPacket::_instance = nullptr;
CommandPacket* CommandPacket::_instance = nullptr;
DataPacket* DataPacket::_instance = nullptr;
NoticePacket* NoticePacket::_instance = nullptr;

static const PROGMEM char* const IM920_RESPONSE_OK = "OK";
static const PROGMEM char* const IM920_COMMAND_TERM = "\r\n";

// handle diagnostic informations given by assertion and abort program execution:
void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp) {
    // transmit diagnostic informations through serial link. 
    Serial.println(__func);
    Serial.println(__file);
    Serial.println(__lineno, DEC);
    Serial.println(__sexp);
    Serial.flush();
    // abort program execution.
    abort();
}

static long _tick(long& count, unsigned long& previous)
{
	unsigned long current = millis();
	long tick;
	
	if (count < 0) return 1;
	
	if (current >= previous) {
		tick = current - previous;
	} else {
		tick = (unsigned long)(-1) - previous + current;
	}
	previous = current;
	
	if (count > tick) count -= tick;
	else count = 0;	
	
	return count;
}

IM920& IM920::Instance()
{
	static IM920 _instance;
	
	return _instance;
};

int IM920::listen(IM920Frame& frame, long timeout)
{
#define IM920_STATE_LISTEN	0x10
#define IM920_STATE_RECEIVING_HDR_NODEID	0x30
#define IM920_STATE_RECEIVING_HDR_MODULEID	0x31
#define IM920_STATE_RECEIVING_HDR_RSSI		0x32
#define IM920_STATE_RECEIVING_PACKET_HDR	0x33
#define IM920_STATE_RECEIVING_PACKET_PAYLOAD	0x34

	int ret = -1;
	int state = IM920_STATE_LISTEN;
	unsigned long previous = millis();
	
	frame.clear();
	
	while (_tick(timeout, previous))
	{
		if (state == IM920_STATE_LISTEN) {
			if (_im920.available()) {
				if (timeout >= 0) {
					// extend the timeout by some time needed to receive 64 bytes data.
					timeout += ((_im920.getTxTimePerByte() << 6) >> 10) + 1;
				}
				
				state = IM920_STATE_RECEIVING_HDR_NODEID;
			}
		} else if (state == IM920_STATE_RECEIVING_HDR_NODEID) {
			if (_im920.available() < 3) continue;

			frame.setNodeID(_im920.parseInt8());
			
			// discard ','
			_im920.read();

			state = IM920_STATE_RECEIVING_HDR_MODULEID;
		} else if (state == IM920_STATE_RECEIVING_HDR_MODULEID) {
			if (_im920.available() < 5) continue;

			frame.setModuleID(_im920.parseInt16());
			
			// discard ','
			_im920.read();

			state = IM920_STATE_RECEIVING_HDR_RSSI;
		} else if (state == IM920_STATE_RECEIVING_HDR_RSSI) {
			if (_im920.available() < 3) continue;

			frame.setRSSI(_im920.parseInt8());
			
			// discard ':'
			_im920.read();
			
			state = IM920_STATE_RECEIVING_PACKET_HDR;
		} else if (state == IM920_STATE_RECEIVING_PACKET_HDR) {
			if (_im920.available() < 3) continue;

			frame.put(_im920.read()); // read the frame length
			frame.put(_im920.read()); // read the flag
			frame.put(_im920.read()); // read the frame ID
			
			state = IM920_STATE_RECEIVING_PACKET_PAYLOAD;
		} else if (state == IM920_STATE_RECEIVING_PACKET_PAYLOAD) {
			PacketOperator& packet = PacketOperator::refInstance(frame);
			size_t len = packet.getPacketLength(frame);
			
			assert(len > 0 && len <= IM920_PACKET_PAYLOAD_SIZE);
			if (!(len > 0 && len <= IM920_PACKET_PAYLOAD_SIZE)) break;

			while (_im920.available() && packet.getPayloadLength(frame) < len)
			{
				frame.put(_im920.read());
			}
			
			if (packet.getPayloadLength(frame) == len) {
				// discard CR+LF
				while (_im920.read() != '\n');
				
				if (packet.getPacketType(frame) == IM920_PACKET_DATA && packet.isEncoded(frame)) {
					static_cast<DataPacket&>(packet).decodeData(frame);
				} else if (packet.getPacketType(frame) == IM920_PACKET_COMMAND) {
					CommandPacket& command = static_cast<CommandPacket&>(packet);
					uint8_t cmd = command.getCommand(frame);
					
					if (cmd == COMMAND_IM920_CMD) {
						char response[ACK_PARAM_LEN];
						_im920.execIM920Cmd(command.getCommandParam(frame), response, sizeof(response));
						
						if (command.isAckRequested(frame)) {
							AckPacket& ack = AckPacket::Instance();
							ack.reset(frame);
							ack.setCommand(frame, cmd);
							ack.setResponse(frame, response);
							send(frame);
						}
						
						frame.clear();
						state = IM920_STATE_LISTEN;
						continue;
					}
				}
				
				ret = 0;
				
				break;
			}
		} else {
			assert(false);
			break;
		}
	}
	
	return ret;
}

int IM920::send(IM920Frame& frame)
{
	size_t sentLen;
	
	sentLen = _send(frame);
	if (!(sentLen == frame.getFrameLength())) return -1;

	return 0;
}

size_t IM920::sendData(const uint8_t data[], size_t length, bool fragment)
{
	IM920Frame frame;
	DataPacket& packet = DataPacket::Instance();
	size_t sentLen = 0, ret = 0;

	while (length - sentLen > 0)
	{
		packet.reset(frame);
		
		// set the fragment flag as the application demand
		packet.setFragment(frame, fragment);
		
		// Encode the data then send it.
		// Encoding data inflates the volume of the data so that a portion of the data can be sent once.
		sentLen += packet.setDataWithEncode(frame, data + sentLen, length - sentLen);
		if (length - sentLen > 0) {
			packet.setFragment(frame, true);
		}
		
		ret = _send(frame);

		assert(ret == frame.getFrameLength());
	}
	
	return sentLen;
}

int IM920::sendCommand(uint8_t cmd, const char param[])
{
	IM920Frame frame;
	CommandPacket& packet = CommandPacket::Instance();
	size_t sentLen;
	
	packet.reset(frame);

	packet.setCommand(frame, cmd);
	packet.setCommandParam(frame, param);

	packet.updatePacketLength(frame);
	
	sentLen = _send(frame);
	if (!(sentLen == frame.getFrameLength())) return -1;
	
	return 0;
}

int IM920::sendCommandWithAck(uint8_t cmd, const char param[])
{
	IM920Frame frame;
	CommandPacket& packet = CommandPacket::Instance();
	size_t sentLen;
	
	packet.reset(frame);

	packet.setCommand(frame, cmd);
	packet.setCommandParam(frame, param);
	packet.setAckRequest(frame, true);

	packet.updatePacketLength(frame);
	
	sentLen = _send(frame);
	if (!(sentLen == frame.getFrameLength())) return -1;
	
	return 0;
}

int IM920::sendAck(uint8_t cmd, const char response[])
{
	IM920Frame frame;
	AckPacket& packet = AckPacket::Instance();
	size_t sentLen;
	
	packet.reset(frame);
	
	packet.setCommand(frame, cmd);
	packet.setResponse(frame, response);
	
	packet.updatePacketLength(frame);
	
	sentLen = _send(frame);
	if (!(sentLen == frame.getFrameLength())) return -1;
	
	return 0;
}

int IM920::sendNotice(const char notice[])
{
	IM920Frame frame;
	NoticePacket& packet = NoticePacket::Instance();
	size_t sentLen;
	
	packet.reset(frame);
	
	sentLen = packet.setNotice(frame, notice);
	if (sentLen != strlen(notice)) return -1;

	sentLen = _send(frame);
	if (!(sentLen == frame.getFrameLength())) return -1;
	
	return 0;
}

uint8_t IM920::_getNextFrameID()
{
	static uint8_t _sequence = 0;
	return _sequence++;
}

size_t IM920::_send(IM920Frame& frame)
{
	size_t ret;
	PacketOperator& packet = PacketOperator::refInstance(frame);

	packet.setFrameID(frame, _getNextFrameID());
	
	ret = _im920.sendBytes(frame.getArray(), frame.getFrameLength());
		
	return ret;
}

PacketOperator& PacketOperator::refInstance(int type)
{
	switch(type)
	{
		case IM920_PACKET_DATA:
			return DataPacket::Instance();

		case IM920_PACKET_COMMAND:
			return CommandPacket::Instance();

		case IM920_PACKET_ACK:
			return AckPacket::Instance();

		case IM920_PACKET_NOTICE:
			return NoticePacket::Instance();

		default:
			assert(false);
			return;
	}
}

PacketOperator& PacketOperator::refInstance(const IM920Frame& frame)
{
	int type;
	
	type = frame.getArray()[IM920_PACKET_TYPE_I] & IM920_PACKET_TYPE_MASK;
	
	return refInstance(type);
}

void PacketOperator::reset(IM920Frame& frame, size_t size) const
{
	frame.clear();
	
	frame.resetFrameLength(IM920_PACKET_HEADER_SIZE + size);
}

void PacketOperator::resetPayloadLength(IM920Frame& frame, size_t size) const
{
	frame.resetFrameLength(IM920_PACKET_HEADER_SIZE + size);
}

size_t PacketOperator::getPayloadLength(const IM920Frame& frame) const
{
	if (frame.getFrameLength() < IM920_PACKET_HEADER_SIZE) return 0;

	return frame.getFrameLength() - IM920_PACKET_HEADER_SIZE;
}

uint8_t* PacketOperator::getPayloadArray(IM920Frame& frame) const
{
	return frame.getArray() + IM920_PACKET_PAYLOAD_I;
}

const uint8_t* PacketOperator::getPayloadArray(const IM920Frame& frame) const
{
	return frame.getArray() + IM920_PACKET_PAYLOAD_I;
}

uint8_t* PacketOperator::getPayloadTerminator(IM920Frame& frame) const
{
	return frame.getTerminator();
}

const uint8_t* PacketOperator::getPayloadTerminator(const IM920Frame& frame) const
{
	return frame.getTerminator();
}

size_t PacketOperator::getPacketHeaderLength(const IM920Frame& frame) const
{
	return IM920_PACKET_HEADER_SIZE;
}

size_t PacketOperator::getPacketLength(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);

	return frame.getArray()[IM920_PACKET_LENGTH_I] & IM920_PACKET_LENGTH_MASK;
}

int PacketOperator::getPacketType(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);
	
	return frame.getArray()[IM920_PACKET_TYPE_I] & IM920_PACKET_TYPE_MASK;
}

bool PacketOperator::isEncoded(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);
	
	return (frame.getArray()[IM920_PACKET_FLAG_I] & IM920_PACKET_FLAG_MASK_ENC) != 0 ? true : false;
}

bool PacketOperator::isFragmented(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);
	
	return (frame.getArray()[IM920_PACKET_FLAG_I] & IM920_PACKET_FLAG_MASK_FRAG) != 0 ? true : false;
}

bool PacketOperator::isAckRequested(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);

	return (frame.getArray()[IM920_PACKET_FLAG_I] & IM920_PACKET_FLAG_MASK_ACK) != 0 ? true : false;
}

uint8_t PacketOperator::getFrameID(const IM920Frame& frame) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);

	return frame.getArray()[IM920_PACKET_FRAMEID_I];
}

void PacketOperator::setPacketLength(IM920Frame& frame, size_t length) const
{
	assert(frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);
	
	frame.getArray()[IM920_PACKET_LENGTH_I] = length & IM920_PACKET_LENGTH_MASK;
}

void PacketOperator::setPacketType(IM920Frame& frame, uint8_t type) const
{
	assert (frame.getFrameLength() >= IM920_PACKET_HEADER_SIZE);

	frame.getArray()[IM920_PACKET_TYPE_I] |= (frame.getArray()[IM920_PACKET_TYPE_I] & ~IM920_PACKET_TYPE_MASK) | type;
}

void PacketOperator::setEncode(IM920Frame& frame, bool encode) const
{
	if (encode) frame.getArray()[IM920_PACKET_FLAG_I] |= IM920_PACKET_FLAG_MASK_ENC;
	else frame.getArray()[IM920_PACKET_FLAG_I] &= ~IM920_PACKET_FLAG_MASK_ENC;
}

void PacketOperator::setFragment(IM920Frame& frame, bool fragment) const
{
	if (fragment) frame.getArray()[IM920_PACKET_FLAG_I] |= IM920_PACKET_FLAG_MASK_FRAG;
	else frame.getArray()[IM920_PACKET_FLAG_I] &= ~IM920_PACKET_FLAG_MASK_FRAG;
}

void PacketOperator::setAckRequest(IM920Frame& frame, bool request) const
{
	if (request) frame.getArray()[IM920_PACKET_FLAG_I] |= IM920_PACKET_FLAG_MASK_ACK;
	else frame.getArray()[IM920_PACKET_FLAG_I] &= ~IM920_PACKET_FLAG_MASK_ACK;
}

void PacketOperator::setFrameID(IM920Frame& frame, uint8_t num) const
{
	frame.getArray()[IM920_PACKET_FRAMEID_I] = num;
}

void PacketOperator::updatePacketLength(IM920Frame& frame)
{
	size_t length;
	
	length = getPayloadLength(frame);
	
	setPacketLength(frame, length);
}

AckPacket::AckPacket()
{
}

AckPacket::~AckPacket()
{
}

AckPacket& AckPacket::Instance()
{
	if (_instance == nullptr) {
		_instance = new AckPacket();
	}

	return *_instance;
}

void AckPacket::reset(IM920Frame& frame, size_t size) const
{
	PacketOperator::reset(frame, size);

	setPacketType(frame, IM920_PACKET_ACK);
}

uint8_t AckPacket::getCommand(const IM920Frame& frame) const
{
	return getPayloadArray(frame)[IM920_PACKET_ACK_CMD_I];
}

void AckPacket::setCommand(IM920Frame& frame, uint8_t cmd) const
{
	getPayloadArray(frame)[IM920_PACKET_ACK_CMD_I] = cmd;
}

size_t AckPacket::getResponseLength(const IM920Frame& frame) const
{
	assert( strlen( reinterpret_cast<const char*>(getPayloadArray(frame) + IM920_PACKET_ACK_PARAM_I)) == (getPacketLength(frame) - ACK_COMMAND_SIZE - 1 ));

	return getPacketLength(frame) - ACK_COMMAND_SIZE - 1 /* '\0' */ ;
}

size_t AckPacket::getResponse(const IM920Frame& frame, char buf[]) const
{
	const char* param = reinterpret_cast<const char*>( getPayloadArray(frame) + IM920_PACKET_ACK_PARAM_I);
	size_t len = getResponseLength(frame);
	
	strncpy(buf, param, len + 1);
	
	assert(buf[len] == '\0');

	return len;
}

const char* AckPacket::getResponse(const IM920Frame& frame) const
{
	return reinterpret_cast<const char*>(getPayloadArray(frame) + IM920_PACKET_ACK_PARAM_I);
}

size_t AckPacket::setResponse(IM920Frame& frame, const char response[]) const
{
	size_t responseLen = strlen(response) + 1;

	if (responseLen > ACK_PARAM_LEN) responseLen = ACK_PARAM_LEN;
	
	resetPayloadLength(frame, responseLen + ACK_COMMAND_SIZE);
	
	char* buf = reinterpret_cast<char*>(getPayloadArray(frame) + IM920_PACKET_ACK_PARAM_I);
	strncpy(buf, response, responseLen);
	buf[responseLen - 1] = '\0';
	
	updatePacketLength(frame);
	
	return responseLen - 1;
}

CommandPacket::CommandPacket()
{
}

CommandPacket::~CommandPacket()
{
}

CommandPacket& CommandPacket::Instance()
{
	if (_instance == nullptr) {
		_instance = new CommandPacket();
	}

	return *_instance;
}

void CommandPacket::reset(IM920Frame& frame, size_t size) const
{
	PacketOperator::reset(frame, size);

	setPacketType(frame, IM920_PACKET_COMMAND);
}

uint8_t CommandPacket::getCommand(const IM920Frame& frame) const
{
	return getPayloadArray(frame)[IM920_PACKET_COMMAND_CMD_I];
}

void CommandPacket::setCommand(IM920Frame& frame, uint8_t cmd) const
{
	getPayloadArray(frame)[IM920_PACKET_COMMAND_CMD_I] = cmd;
}

size_t CommandPacket::getCommandParamLength(const IM920Frame& frame) const
{
	assert(strlen(reinterpret_cast<const char*>(getPayloadArray(frame) + IM920_PACKET_COMMAND_PARAM_I)) == (getPacketLength(frame) - COMMAND_SIZE - 1));

	return getPacketLength(frame) - COMMAND_SIZE - 1 /* '\0' */ ;
}

size_t CommandPacket::getCommandParam(const IM920Frame& frame, char buf[]) const
{
	const char* param = reinterpret_cast<const char*>(getPayloadArray(frame) + IM920_PACKET_COMMAND_PARAM_I);
	size_t len = getCommandParamLength(frame);
	
	strncpy(buf, param, len);
	buf[len] = '\0';

	return len;
}

const char* CommandPacket::getCommandParam(const IM920Frame& frame) const
{
	return reinterpret_cast<const char*>(getPayloadArray(frame) + IM920_PACKET_COMMAND_PARAM_I);
}

size_t CommandPacket::setCommandParam(IM920Frame& frame, const char param[]) const
{
	size_t paramLen = strlen(param) + 1;

	if (paramLen > COMMAND_PARAM_LEN) paramLen = COMMAND_PARAM_LEN;
	
	resetPayloadLength(frame, paramLen + COMMAND_SIZE);
	
	char* buf = reinterpret_cast<char*>(getPayloadArray(frame) + IM920_PACKET_COMMAND_PARAM_I);
	strncpy(buf, param, paramLen);
	buf[paramLen - 1] = '\0';
	
	updatePacketLength(frame);
	
	return paramLen - 1;
}

DataPacket::DataPacket()
{
}

DataPacket::~DataPacket()
{
}

DataPacket& DataPacket::Instance()
{
	if (_instance == nullptr) {
		_instance = new DataPacket();
	}
	
	return *_instance;
}

void DataPacket::reset(IM920Frame& frame, size_t size) const
{
	PacketOperator::reset(frame, size);

	setPacketType(frame, IM920_PACKET_DATA);
}

int DataPacket::decodeData(IM920Frame& frame) const
{
	if (!isEncoded(frame)) return 0;
	
	size_t decodeLen = getDecodeLength(frame);
	uint8_t decoded[decodeLen];

	getDataWithDecode(frame, decoded);
	
	setData(frame, decoded, decodeLen);
	
	setEncode(frame, false);
	
	return 0;
}

int DataPacket::encodeData(IM920Frame& frame) const
{
	if (isEncoded(frame)) return 0;
	
	size_t dataLen = getPayloadLength(frame);
	uint8_t data[dataLen];
	
	getData(frame, data);
	
	size_t encodeLen = getEncodeLength(frame) + 1;
	resetPayloadLength(frame, encodeLen);
	
	setDataWithEncode(frame, data, dataLen);
	
	setEncode(frame, true);
	
	return 0;
}

size_t DataPacket::getDataLength(const IM920Frame& frame) const
{
	assert(getPacketLength(frame) == (getPayloadTerminator(frame) - getPayloadArray(frame)));

	return getPacketLength(frame);
}

size_t DataPacket::getData(const IM920Frame& frame, uint8_t buf[]) const
{
	size_t length = getDataLength(frame);

	memcpy(buf, getPayloadArray(frame), length);
	
	return length;
}

const uint8_t* DataPacket::getData(const IM920Frame& frame) const
{
	return getPayloadArray(frame);
}

size_t DataPacket::getDataWithDecode(IM920Frame& frame, uint8_t buf[]) const
{
	size_t decodeLen;
	const char* base64String = reinterpret_cast<const char*>(getPayloadArray(frame));
	
	decodeLen = decode_base64(base64String, buf);
	
	return decodeLen;
}

size_t DataPacket::getDecodeLength(IM920Frame& frame) const
{
	const char* base64String = reinterpret_cast<const char*>(getPayloadArray(frame));
	size_t decodeLen = decode_base64_length(base64String);
	
	assert(!(decodeLen > BIN_DATA_MAX_LENGTH));
	
	return decodeLen;
}

size_t DataPacket::getEncodeLength(IM920Frame& frame) const
{
	size_t dataLen = getPayloadLength(frame);
	size_t encodeLen = encode_base64_length(dataLen);
	
	assert(!(encodeLen > DATA_PACKET_PAYLOAD_SIZE));

	return encodeLen;
}

size_t DataPacket::setData(IM920Frame& frame, const uint8_t data[], size_t length) const
{
	uint8_t* buf = getPayloadArray(frame);
	
	if (length > DATA_PACKET_PAYLOAD_SIZE) length = DATA_PACKET_PAYLOAD_SIZE;
	
	resetPayloadLength(frame, length);
	
	memcpy(buf, data, length);
	
	updatePacketLength(frame);
	
	return length;
}

size_t DataPacket::setDataWithEncode(IM920Frame& frame, uint8_t data[], size_t length) const
{
	size_t encodeLen;
	char* base64String = reinterpret_cast<char*>(getPayloadArray(frame));
	
	if (length > BIN_DATA_MAX_LENGTH) length = BIN_DATA_MAX_LENGTH;
	
	setEncode(frame, true);
	
	encodeLen = encode_base64_length(length) + 1;
	assert(!(encodeLen > DATA_PACKET_PAYLOAD_SIZE));
	
	resetPayloadLength(frame, encodeLen);
	
	// store base64 encoded data to the payload of a data packet.
	encode_base64(data, length,  base64String);
	
	updatePacketLength(frame);
	
	return length;
}

NoticePacket::NoticePacket()
{
}

NoticePacket::~NoticePacket()
{
}

NoticePacket& NoticePacket::Instance()
{
	if (_instance == nullptr) {
		_instance = new NoticePacket();
	}

	return *_instance;
}

void NoticePacket::reset(IM920Frame& frame, size_t size) const
{
	PacketOperator::reset(frame, size);

	setPacketType(frame, IM920_PACKET_NOTICE);
}

size_t NoticePacket::getNoticeLength(const IM920Frame& frame) const
{
	assert(strlen(reinterpret_cast<const char*>(getPayloadArray(frame))) == (getPacketLength(frame) - 1));

	return getPacketLength(frame) - 1 /* '\0' */ ;
}

size_t NoticePacket::getNotice(const IM920Frame& frame, char buf[]) const
{
	const char* notice = reinterpret_cast<const char*>(getPayloadArray(frame));
	size_t len = getNoticeLength(frame);
	
	strncpy(buf, notice, len);
	buf[len] = '\0';

	return len;
}

const char* NoticePacket::getNotice(const IM920Frame& frame) const
{
	return reinterpret_cast<const char*>(getPayloadArray(frame));
}

size_t NoticePacket::setNotice(IM920Frame& frame, char notice[]) const
{
	size_t noticeLen = strlen(notice) + 1;

	if (noticeLen > NOTICE_MAX_LEN) noticeLen = NOTICE_MAX_LEN;
	
	resetPayloadLength(frame, noticeLen);
	
	char* buf = reinterpret_cast<char*>(getPayloadArray(frame));
	strncpy(buf, notice, noticeLen);
	buf[noticeLen - 1] = '\0';
	
	updatePacketLength(frame);
	
	return noticeLen - 1;
}

IM920Frame::IM920Frame()
	: _p(0), _rp(0)
{
	for (int i = 0; i < FRAME_PAYLOAD_SIZE; i++) _payload[i] = 0;
}

IM920Frame::~IM920Frame()
{
}

size_t IM920Frame::put(uint8_t data)
{
	if (!(_p < FRAME_PAYLOAD_SIZE)) return 0;

	_payload[_p++] = data;

	return _p;
}

uint8_t IM920Frame::getNextByte()
{
	if (!(_rp < _p)) return 0;

	return _payload[_rp++];
}

void IM920Frame::clear()
{
	_p = 0;
	_rp = 0;
	for (int i = 0; i < FRAME_PAYLOAD_SIZE; i++) _payload[i] = 0;
}

size_t IM920Frame::resetFrameLength(size_t length)
{
	assert(length <= FRAME_PAYLOAD_SIZE);

	if (length > FRAME_PAYLOAD_SIZE) {
		_p = FRAME_PAYLOAD_SIZE;
		return _p;
	}
	
	_p = length;

	if (_p < _rp) _rp = _p;

	return _p;
}

IM920Interface::IM920Interface()
	: _resetPin(0), _busyPin(0), _activeTime(0), _sleepTime(0), _usTxTimePerByte(0), _initialized(false), _timeout(1000)
{
}

IM920Interface::~IM920Interface()
{
}

void IM920Interface::begin(Stream& serial, int resetPin, int busyPin, long baud)
{
	_resetPin = resetPin;
	_busyPin = busyPin;
	pinMode(resetPin, OUTPUT);
	pinMode(busyPin, INPUT);

	// release reset signal to enable the interface
	digitalWrite(resetPin, HIGH);

	_serial = &serial;

	_usTxTimePerByte = (1000000 / baud + 1) << 3;
	
	_serial->setTimeout(_timeout);
	
	// discard the interface start log
	char buf[32];
	_serial->readBytesUntil( '\n', buf, sizeof(buf) - 1 );
	
	_initialized = true;
}

void IM920Interface::end()
{
	_initialized = false;
}

size_t IM920Interface::available()
{
	return _serial->available();
}

uint8_t IM920Interface::read()
{
	return _serial->read();
}

size_t IM920Interface::readBytesUntil(char character, uint8_t buf[], size_t length)
{
	return _serial->readBytesUntil( character, buf, length );
}

size_t IM920Interface::sendBytes(const uint8_t* data, size_t length)
{
	size_t ret;
	char res[5];

	if (length > FRAME_PAYLOAD_SIZE) length = FRAME_PAYLOAD_SIZE;
	
	// send data
	_serial->print( F("TXDA") );
	ret = _serial->write(data, length);
	_serial->print(IM920_COMMAND_TERM);
	_serial->flush();
	assert(ret == length);
	
	_getResponse(res, sizeof(res));
	
	if (strncmp(res, IM920_RESPONSE_OK, sizeof(IM920_RESPONSE_OK) - 1) != 0) return 0;
	
	return ret;
}

void IM920Interface::setTimeout(unsigned long timeout)
{
	_timeout = timeout;
	
	_serial->setTimeout(_timeout);
}

int8_t IM920Interface::parseInt8()
{
	char a[3];
	
	_serial->readBytes(a, 2);
	a[2] = '\0';
	
	return strtoul(a, nullptr, 16);
}

int16_t IM920Interface::parseInt16()
{
	char a[5];
	
	_serial->readBytes(a, 4);
	a[4] = '\0';
	
	return strtoul(a, nullptr, 16);
}

int32_t IM920Interface::parseInt32()
{
	char a[9];

	_serial->readBytes(a, 8);
	a[8] = '\0';
	
	return strtoul(a, nullptr, 16);
}

size_t IM920Interface::execIM920Cmd(const char command[], char response[], size_t length)
{
	size_t ret;
	
	_serial->print(command);
	_serial->print(F("\r\n"));
	_serial->flush();
	
	ret = _getResponse(response, length);
	
	return ret;
}

unsigned long IM920Interface::getTxTimePerByte()
{
	return _usTxTimePerByte;
}

int IM920Interface::enableSleep()
{
	int ret;
	
	ret = _exec("DSRX\r\n", IM920_RESPONSE_OK);

	return ret;
}

int IM920Interface::disableSleep()
{
	int ret;

	_serial->print(F("?"));
	_serial->flush();
	delayMicroseconds(_usTxTimePerByte);

	ret = _exec("ENRX\r\n", IM920_RESPONSE_OK);

	return ret;
}

uint16_t IM920Interface::getActiveDuration()
{
	return _activeTime;
}

uint16_t IM920Interface::getSleepDuration()
{
	return _sleepTime;
}

void IM920Interface::setActiveDuration(uint16_t activeTime)
{
	int ret = 0;
	char cmd[11];
	
	if (_activeTime != activeTime) {
		_activeTime = activeTime;

		snprintf(cmd, sizeof(cmd), "SWTM%04X\r\n", _activeTime);
		ret = _exec(cmd, IM920_RESPONSE_OK);
	}
	
	return ret;
}

void IM920Interface::setSleepDuration(uint16_t sleepTime)
{
	int ret = 0;
	char cmd[11];
	
	if (_sleepTime != sleepTime) {
		_sleepTime = sleepTime;
		
		snprintf(cmd, sizeof(cmd), "SSTM%04X\r\n", _sleepTime);
		ret = _exec(cmd, IM920_RESPONSE_OK);
	}
	
	return ret;
}

int IM920Interface::resetInterface()
{
	int ret;
	
	ret = _exec("SRST\r\n", "IM920 VER.");
	
	return ret;
}

int IM920Interface::_exec(const char cmd[], const char search[])
{
	int ret = 0;
	char buf[20];
	
	// send the command
	_serial->print(cmd);
	_serial->flush();
	
	// check the response
	_getResponse(buf, sizeof(buf));
	if (search != nullptr) strcmp(buf, search) == 0 ? ret = 0 : ret = -1;
	
	return ret;
}

bool IM920Interface::_isBusy()
{
	return digitalRead(_busyPin);
}

size_t IM920Interface::_getResponse(char buf[], size_t length)
{
	size_t ret;

	while(_isBusy());

	_serial->setTimeout(_timeout);
	ret = _serial->readBytesUntil('\n', buf, length);
	buf[ret - 1] = '\0';
	
	return ret - 1;
}
