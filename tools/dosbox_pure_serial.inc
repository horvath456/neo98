// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98: a 16550 of the DOS machine whose line is the host's.
// tools/build_dosbox_pure.py copies this file into the patched clone of the
// core as neo98_serial.inc, and the patched serialport.cpp includes it. The
// registers, the FIFO and the interrupts are CSerial's; this class is the
// cable: what the program shifts out goes to the host, what the host has
// arrives a byte at a time at the rate the program set.
// The machine takes the port from the host when the program first writes one
// of its registers, the way VCD of Windows 98 gives a COM port to the DOS box
// that touches it, and keeps it until the machine ends. While a Windows
// program holds the port the UART is one with no cable; the next write asks
// again.
#include "neo98_dbp.h"

class CSerialNeo98;
static CSerialNeo98* neo98_serial[4] = { 0, 0, 0, 0 };

class CSerialNeo98 : public CSerial {
public:
	CSerialNeo98(Bitu id, CommandLine* cmd) : CSerial(id, cmd), port((unsigned)id), built(false), held(false),
		receiving(false), refused(false), rx_head(0), rx_count(0), tx_count(0), lines(~0u),
		line_dtr(false), line_rts(false), line_break(false) {
		CSerial::Init_Registers();
		setRI(false); setDSR(false); setCD(false); setCTS(false);
		lines = 0;
		built = true;
		neo98_serial[port & 3] = this;
		InstallationSuccessful = true;
	}
	~CSerialNeo98() {
		neo98_serial[port & 3] = 0;
		removeEvent(SERIAL_TX_EVENT);
		removeEvent(SERIAL_THR_EVENT);
		removeEvent(SERIAL_RX_EVENT);
		removeEvent(SERIAL_POLLING_EVENT);
		if (held) {
			Flush();
			Neo98DbpState* s = DBP_Neo98State();
			if (s->serial_release) s->serial_release(s->serial_owner, port);
		}
	}

	// A register of the UART was written: the program uses the port.
	void Touched() {
		if (!built || held) return;
		Neo98DbpState* s = DBP_Neo98State();
		if (!s->serial_claim || !s->serial_claim(s->serial_owner, port)) {
			if (!refused) LOG_MSG("NEO98: COM%d is held by a Windows program", (int)port + 1);
			refused = true;
			return;
		}
		held = true;
		refused = false;
		if (s->serial_configure) s->serial_configure(s->serial_owner, port, baud, data_bits, parity, stop_bits);
		if (s->serial_control) s->serial_control(s->serial_owner, port, line_dtr, line_rts, line_break);
		setEvent(SERIAL_POLLING_EVENT, 1.0f);
	}

	void handleUpperEvent(Bit16u type) {
		switch (type) {
			case SERIAL_POLLING_EVENT:
				// Once a millisecond: what the program sent leaves in one
				// piece, the input lines follow the host, and a byte that
				// waits starts the receiver.
				setEvent(SERIAL_POLLING_EVENT, 1.0f);
				Flush();
				Lines();
				if (!receiving) Receive();
				break;
			case SERIAL_RX_EVENT:
				receiving = false;
				Receive();
				break;
			case SERIAL_TX_EVENT:
				ByteTransmitted();
				break;
			case SERIAL_THR_EVENT:
				ByteTransmitting();
				setEvent(SERIAL_TX_EVENT, bytetime);
				break;
		}
	}

	void updatePortConfig(Bit16u divider, Bit8u lcr) {
		baud = divider ? 115200u / divider : 115200u;
		data_bits = 5u + (lcr & 3);
		// LCR bits 3 to 5: none, odd, even, mark, space, as the DCB counts them.
		parity = !(lcr & 0x08) ? 0u : (lcr & 0x20) ? ((lcr & 0x10) ? 4u : 3u) : ((lcr & 0x10) ? 2u : 1u);
		stop_bits = (lcr & 0x04) ? (data_bits == 5 ? 1u : 2u) : 0u;
		if (!held) return;
		Neo98DbpState* s = DBP_Neo98State();
		if (s->serial_configure) s->serial_configure(s->serial_owner, port, baud, data_bits, parity, stop_bits);
	}
	void updateMSR() {
		if (held) Lines();
	}
	void transmitByte(Bit8u val, bool first) {
		if (held) {
			tx[tx_count++] = val;
			if (tx_count == sizeof(tx)) Flush();
		}
		// The time a byte takes on the wire, as on the unconnected port.
		if (first) setEvent(SERIAL_THR_EVENT, bytetime / 10);
		else setEvent(SERIAL_TX_EVENT, bytetime);
	}
	void setBreak(bool value) { line_break = value; Control(); }
	void setRTSDTR(bool rts, bool dtr) { line_rts = rts; line_dtr = dtr; Control(); }
	void setRTS(bool val) { line_rts = val; Control(); }
	void setDTR(bool val) { line_dtr = val; Control(); }

private:
	void Control() {
		if (!held) return;
		Neo98DbpState* s = DBP_Neo98State();
		if (s->serial_control) s->serial_control(s->serial_owner, port, line_dtr, line_rts, line_break);
	}
	void Flush() {
		if (!tx_count) return;
		Neo98DbpState* s = DBP_Neo98State();
		if (held && s->serial_write) s->serial_write(s->serial_owner, port, tx, tx_count);
		tx_count = 0;
	}
	void Lines() {
		Neo98DbpState* s = DBP_Neo98State();
		const unsigned now = s->serial_lines ? s->serial_lines(s->serial_owner, port) : 0;
		if (now == lines) return;
		lines = now;
		setCTS((now & 0x10) != 0);
		setDSR((now & 0x20) != 0);
		setRI((now & 0x40) != 0);
		setCD((now & 0x80) != 0);
	}
	// One byte into the receiver when it has room, and the next one a byte
	// time later. A program that does not read leaves the bytes with the
	// host; nothing overruns.
	void Receive() {
		if (!held) return;
		if (!rx_count) {
			Neo98DbpState* s = DBP_Neo98State();
			rx_head = 0;
			rx_count = s->serial_read ? s->serial_read(s->serial_owner, port, rx, sizeof(rx)) : 0;
			if (!rx_count) return;
		}
		// A full receiver is asked again with the next millisecond.
		if (!CanReceiveByte()) return;
		receiveByte(rx[rx_head++]);
		--rx_count;
		receiving = true;
		setEvent(SERIAL_RX_EVENT, bytetime);
	}

	unsigned port;
	bool built, held, receiving, refused;
	Bit8u rx[256], tx[256];
	unsigned rx_head, rx_count, tx_count, lines;
	bool line_dtr, line_rts, line_break;
	unsigned baud = 9600, data_bits = 8, parity = 0, stop_bits = 0;
};

// Called by the patched SERIAL_Write for every register write of a port.
static void DBP_Neo98SerialTouched(Bitu i) {
	if (i < 4 && neo98_serial[i]) neo98_serial[i]->Touched();
}
