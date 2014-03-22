/*
    Hitachi HD63450 DMA Controller

    Largely based on documentation of the Sharp X68000
*/

#include "hd63450.h"

const device_type HD63450 = &device_creator<hd63450_device>;

hd63450_device::hd63450_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: device_t(mconfig, HD63450, "Hitachi HD63450", tag, owner, clock, "hd63450", __FILE__),
	m_cpu(NULL)
{
	for (int i = 0; i < 4; i++)
		{
			memset(&m_reg[i], 0, sizeof(m_reg[i]));
			m_timer[i] = NULL;
			m_in_progress[i] = 0;
			m_transfer_size[i] = 0;
			m_halted[i] = 0;
		}
}

//-------------------------------------------------
//  device_config_complete - perform any
//  operations now that the configuration is
//  complete
//-------------------------------------------------

void hd63450_device::device_config_complete()
{
		// inherit a copy of the static data
	const hd63450_interface *intf = reinterpret_cast<const hd63450_interface *>(static_config());
	if (intf != NULL)
		*static_cast<hd63450_interface *>(this) = *intf;

	// or initialize to defaults if none provided
	else
	{
		m_cpu_tag = "";
	}
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void hd63450_device::device_start()
{
	// get the CPU device
	m_cpu = machine().device<cpu_device>(m_cpu_tag);
	assert(m_cpu != NULL);
	
	// Initialise timers and registers
	for (int x = 0; x < 4 ; x++)
	{
		m_timer[x] = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(hd63450_device::dma_transfer_timer), this));
		m_reg[x].niv = 0x0f;  // defaults?
		m_reg[x].eiv = 0x0f;
	}
}

READ16_MEMBER(hd63450_device::read)
{
	int channel,reg;
	
	channel = (offset & 0x60) >> 5;
	reg = offset & 0x1f;

	switch(reg)
	{
	case 0x00:  // CSR / CER
		return (m_reg[channel].csr << 8) | m_reg[channel].cer;
	case 0x02:  // DCR / OCR
		return (m_reg[channel].dcr << 8) | m_reg[channel].ocr;
	case 0x03:  // SCR / CCR
		return (m_reg[channel].scr << 8) | m_reg[channel].ccr;
	case 0x05:  // MTC
		return m_reg[channel].mtc;
	case 0x06:  // MAR (high)
		return (m_reg[channel].mar & 0xffff0000) >> 16;
	case 0x07:  // MAR (low)
		return (m_reg[channel].mar & 0x0000ffff);
	case 0x0a:  // DAR (high)
		return (m_reg[channel].dar & 0xffff0000) >> 16;
	case 0x0b:  // DAR (low)
		return (m_reg[channel].dar & 0x0000ffff);
	case 0x0d:  // BTC
		return m_reg[channel].btc;
	case 0x0e:  // BAR (high)
		return (m_reg[channel].bar & 0xffff0000) >> 16;
	case 0x0f:  // BAR (low)
		return (m_reg[channel].bar & 0x0000ffff);
	case 0x12:  // NIV
		return m_reg[channel].niv;
	case 0x13:  // EIV
		return m_reg[channel].eiv;
	case 0x14:  // MFC
		return m_reg[channel].mfc;
	case 0x16:  // CPR
		return m_reg[channel].cpr;
	case 0x18:  // DFC
		return m_reg[channel].dfc;
	case 0x1c:  // BFC
		return m_reg[channel].bfc;
	case 0x1f:  // GCR
		return m_reg[channel].gcr;
	}
	return 0xff;
}

WRITE16_MEMBER(hd63450_device::write)
{
	int channel,reg;

	channel = (offset & 0x60) >> 5;
	reg = offset & 0x1f;
	switch(reg)
	{
	case 0x00:  // CSR / CER
		if(ACCESSING_BITS_8_15)
		{
//          m_reg[channel].csr = (data & 0xff00) >> 8;
//          logerror("DMA#%i: Channel status write : %02x\n",channel,dmac.reg[channel].csr);
		}
		// CER is read-only, so no action needed there.
		break;
	case 0x02:  // DCR / OCR
		if(ACCESSING_BITS_8_15)
		{
			m_reg[channel].dcr = (data & 0xff00) >> 8;
			logerror("DMA#%i: Device Control write : %02x\n",channel,m_reg[channel].dcr);
		}
		if(ACCESSING_BITS_0_7)
		{
			m_reg[channel].ocr = data & 0x00ff;
			logerror("DMA#%i: Operation Control write : %02x\n",channel,m_reg[channel].ocr);
		}
		break;
	case 0x03:  // SCR / CCR
		if(ACCESSING_BITS_8_15)
		{
			m_reg[channel].scr = (data & 0xff00) >> 8;
			logerror("DMA#%i: Sequence Control write : %02x\n",channel,m_reg[channel].scr);
		}
		if(ACCESSING_BITS_0_7)
		{
			m_reg[channel].ccr = data & 0x00ff;
			if((data & 0x0080))// && !dma_read[channel] && !dma_write[channel])
				dma_transfer_start(channel,0);
			if(data & 0x0010)  // software abort
				dma_transfer_abort(channel);
			if(data & 0x0020)  // halt operation
				dma_transfer_halt(channel);
			if(data & 0x0040)  // continure operation
				dma_transfer_continue(channel);
			logerror("DMA#%i: Channel Control write : %02x\n",channel,m_reg[channel].ccr);
		}
		break;
	case 0x05:  // MTC
		m_reg[channel].mtc = data;
		logerror("DMA#%i:  Memory Transfer Counter write : %04x\n",channel,m_reg[channel].mtc);
		break;
	case 0x06:  // MAR (high)
		m_reg[channel].mar = (m_reg[channel].mar & 0x0000ffff) | (data << 16);
		logerror("DMA#%i:  Memory Address write : %08lx\n",channel,m_reg[channel].mar);
		break;
	case 0x07:  // MAR (low)
		m_reg[channel].mar = (m_reg[channel].mar & 0xffff0000) | (data & 0x0000ffff);
		logerror("DMA#%i:  Memory Address write : %08lx\n",channel,m_reg[channel].mar);
		break;
	case 0x0a:  // DAR (high)
		m_reg[channel].dar = (m_reg[channel].dar & 0x0000ffff) | (data << 16);
		logerror("DMA#%i:  Device Address write : %08lx\n",channel,m_reg[channel].dar);
		break;
	case 0x0b:  // DAR (low)
		m_reg[channel].dar = (m_reg[channel].dar & 0xffff0000) | (data & 0x0000ffff);
		logerror("DMA#%i:  Device Address write : %08lx\n",channel,m_reg[channel].dar);
		break;
	case 0x0d:  // BTC
		m_reg[channel].btc = data;
		logerror("DMA#%i:  Base Transfer Counter write : %04x\n",channel,m_reg[channel].btc);
		break;
	case 0x0e:  // BAR (high)
		m_reg[channel].bar = (m_reg[channel].bar & 0x0000ffff) | (data << 16);
		logerror("DMA#%i:  Base Address write : %08lx\n",channel,m_reg[channel].bar);
		break;
	case 0x0f:  // BAR (low)
		m_reg[channel].bar = (m_reg[channel].bar & 0xffff0000) | (data & 0x0000ffff);
		logerror("DMA#%i:  Base Address write : %08lx\n",channel,m_reg[channel].bar);
		break;
	case 0x12:  // NIV
		m_reg[channel].niv = data & 0xff;
		logerror("DMA#%i:  Normal IRQ Vector write : %02x\n",channel,m_reg[channel].niv);
		break;
	case 0x13:  // EIV
		m_reg[channel].eiv = data & 0xff;
		logerror("DMA#%i:  Error IRQ Vector write : %02x\n",channel,m_reg[channel].eiv);
		break;
	case 0x14:  // MFC
		m_reg[channel].mfc = data & 0xff;
		logerror("DMA#%i:  Memory Function Code write : %02x\n",channel,m_reg[channel].mfc);
		break;
	case 0x16:  // CPR
		m_reg[channel].cpr = data & 0xff;
		logerror("DMA#%i:  Channel Priority write : %02x\n",channel,m_reg[channel].cpr);
		break;
	case 0x18:  // DFC
		m_reg[channel].dfc = data & 0xff;
		logerror("DMA#%i:  Device Function Code write : %02x\n",channel,m_reg[channel].dfc);
		break;
	case 0x1c:  // BFC
		m_reg[channel].bfc = data & 0xff;
		logerror("DMA#%i:  Base Function Code write : %02x\n",channel,m_reg[channel].bfc);
		break;
	case 0x1f:
		m_reg[channel].gcr = data & 0xff;
		logerror("DMA#%i:  General Control write : %02x\n",channel,m_reg[channel].gcr);
		break;
	}
}

void hd63450_device::dma_transfer_start(int channel, int dir)
{
	address_space &space = m_cpu->space(AS_PROGRAM);
	m_in_progress[channel] = 1;
	m_reg[channel].csr &= ~0xe0;
	m_reg[channel].csr |= 0x08;  // Channel active
	m_reg[channel].csr &= ~0x30;  // Reset Error and Normal termination bits
	if((m_reg[channel].ocr & 0x0c) != 0x00)  // Array chain or Link array chain
	{
		m_reg[channel].mar = space.read_word(m_reg[channel].bar) << 16;
		m_reg[channel].mar |= space.read_word(m_reg[channel].bar+2);
		m_reg[channel].mtc = space.read_word(m_reg[channel].bar+4);
		if(m_reg[channel].btc > 0)
			m_reg[channel].btc--;
	}

	// Burst transfers will halt the CPU until the transfer is complete
	if((m_reg[channel].dcr & 0xc0) == 0x00)  // Burst transfer
	{
		m_cpu->set_input_line(INPUT_LINE_HALT, ASSERT_LINE);
		m_timer[channel]->adjust(attotime::zero, channel, m_burst_clock[channel]);
	}
	else if(!(m_reg[channel].ocr & 2))
		m_timer[channel]->adjust(attotime::from_usec(500), channel, m_our_clock[channel]);
	else if((m_reg[channel].ocr & 3) == 3)
		m_timer[channel]->adjust(attotime::from_usec(500), channel, attotime::never);

	m_transfer_size[channel] = m_reg[channel].mtc;

	logerror("DMA: Transfer begins: size=0x%08x\n",m_transfer_size[channel]);
}

void hd63450_device::set_timer(int channel, attotime tm)
{
	m_our_clock[channel] = tm;
	if(m_in_progress[channel] != 0)
		m_timer[channel]->adjust(attotime::zero, channel, m_our_clock[channel]);
}

TIMER_CALLBACK_MEMBER(hd63450_device::dma_transfer_timer)
{
	single_transfer(param);
}

void hd63450_device::dma_transfer_abort(int channel)
{
	logerror("DMA#%i: Transfer aborted\n",channel);
	m_timer[channel]->adjust(attotime::zero);
	m_in_progress[channel] = 0;
	m_reg[channel].mtc = m_transfer_size[channel];
	m_reg[channel].csr |= 0xe0;  // channel operation complete, block transfer complete
	m_reg[channel].csr &= ~0x08;  // channel no longer active
}

void hd63450_device::dma_transfer_halt(int channel)
{
	m_halted[channel] = 1;
	m_timer[channel]->adjust(attotime::zero);
}

void hd63450_device::dma_transfer_continue(int channel)
{
	if(m_halted[channel] != 0)
	{
		m_halted[channel] = 0;
		m_timer[channel]->adjust(attotime::zero, channel, m_our_clock[channel]);
	}
}

void hd63450_device::single_transfer(int x)
{
	address_space &space = m_cpu->space(AS_PROGRAM);
	int data;
	int datasize = 1;
	
	if(m_in_progress[x] != 0)  // DMA in progress in channel x
		{
			if(m_reg[x].ocr & 0x80)  // direction: 1 = device -> memory
			{
				if(dma_read[x])
				{
					data = dma_read[x](machine(),m_reg[x].mar);
					if(data == -1)
						return;  // not ready to receive data
					space.write_byte(m_reg[x].mar,data);
					datasize = 1;
				}
				else
				{
					switch(m_reg[x].ocr & 0x30)  // operation size
					{
					case 0x00:  // 8 bit
						data = space.read_byte(m_reg[x].dar);  // read from device address
						space.write_byte(m_reg[x].mar, data);  // write to memory address
						datasize = 1;
						break;
					case 0x10:  // 16 bit
						data = space.read_word(m_reg[x].dar);  // read from device address
						space.write_word(m_reg[x].mar, data);  // write to memory address
						datasize = 2;
						break;
					case 0x20:  // 32 bit
						data = space.read_word(m_reg[x].dar) << 16;  // read from device address
						data |= space.read_word(m_reg[x].dar+2);
						space.write_word(m_reg[x].mar, (data & 0xffff0000) >> 16);  // write to memory address
						space.write_word(m_reg[x].mar+2, data & 0x0000ffff);
						datasize = 4;
						break;
					case 0x30:  // 8 bit packed (?)
						data = space.read_byte(m_reg[x].dar);  // read from device address
						space.write_byte(m_reg[x].mar, data);  // write to memory address
						datasize = 1;
						break;
					}
				}
//              logerror("DMA#%i: byte transfer %08lx -> %08lx  (byte = %02x)\n",x,dmac.reg[x].dar,dmac.reg[x].mar,data);
			}
			else  // memory -> device
			{
				if(dma_write[x])
				{
					data = space.read_byte(m_reg[x].mar);
					dma_write[x](machine(), m_reg[x].mar,data);
					datasize = 1;
				}
				else
				{
					switch(m_reg[x].ocr & 0x30)  // operation size
					{
					case 0x00:  // 8 bit
						data = space.read_byte(m_reg[x].mar);  // read from memory address
						space.write_byte(m_reg[x].dar, data);  // write to device address
						datasize = 1;
						break;
					case 0x10:  // 16 bit
						data = space.read_word(m_reg[x].mar);  // read from memory address
						space.write_word(m_reg[x].dar, data);  // write to device address
						datasize = 2;
						break;
					case 0x20:  // 32 bit
						data = space.read_word(m_reg[x].mar) << 16;  // read from memory address
						data |= space.read_word(m_reg[x].mar+2);  // read from memory address
						space.write_word(m_reg[x].dar, (data & 0xffff0000) >> 16);  // write to device address
						space.write_word(m_reg[x].dar+2, data & 0x0000ffff);  // write to device address
						datasize = 4;
						break;
					case 0x30:  // 8 bit packed (?)
						data = space.read_byte(m_reg[x].mar);  // read from memory address
						space.write_byte(m_reg[x].dar, data);  // write to device address
						datasize = 1;
						break;
					}
				}
//              logerror("DMA#%i: byte transfer %08lx -> %08lx\n",x,m_reg[x].mar,m_reg[x].dar);
			}


			// decrease memory transfer counter
			if(m_reg[x].mtc > 0)
				m_reg[x].mtc--;

			// handle change of memory and device addresses
			if((m_reg[x].scr & 0x03) == 0x01)
				m_reg[x].dar+=datasize;
			else if((m_reg[x].scr & 0x03) == 0x02)
				m_reg[x].dar-=datasize;

			if((m_reg[x].scr & 0x0c) == 0x04)
				m_reg[x].mar+=datasize;
			else if((m_reg[x].scr & 0x0c) == 0x08)
				m_reg[x].mar-=datasize;

			if(m_reg[x].mtc <= 0)
			{
				// End of transfer
				logerror("DMA#%i: End of transfer\n",x);
				if((m_reg[x].ocr & 0x0c) != 0 && m_reg[x].btc > 0)
				{
					m_reg[x].btc--;
					m_reg[x].bar+=6;
					m_reg[x].mar = space.read_word(m_reg[x].bar) << 16;
					m_reg[x].mar |= space.read_word(m_reg[x].bar+2);
					m_reg[x].mtc = space.read_word(m_reg[x].bar+4);
					return;
				}
				m_timer[x]->adjust(attotime::zero);
				m_in_progress[x] = 0;
				m_reg[x].csr |= 0xe0;  // channel operation complete, block transfer complete
				m_reg[x].csr &= ~0x08;  // channel no longer active

				// Burst transfer
				if((m_reg[x].dcr & 0xc0) == 0x00)
				{
					m_cpu->set_input_line(INPUT_LINE_HALT, CLEAR_LINE);
				}

				if(dma_end)
					dma_end(machine(),x,m_reg[x].ccr & 0x08);
			}
		}
}

int hd63450_device::get_vector(int channel)
{
	return m_reg[channel].niv;
}

int hd63450_device::get_error_vector(int channel)
{
	return m_reg[channel].eiv;
}