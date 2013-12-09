#include "stdafx.h"
#include "Emu/SysCalls/SysCalls.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/event.h"

SysCallBase sys_event("sys_event");

int sys_event_flag_create(u32 eflag_id_addr, u32 attr_addr, u64 init)
{
	sys_event.Warning("sys_event_flag_create(eflag_id_addr=0x%x, attr_addr=0x%x, init=0x%llx)", eflag_id_addr, attr_addr, init);

	if(!Memory.IsGoodAddr(eflag_id_addr, 4) || !Memory.IsGoodAddr(attr_addr, sizeof(sys_event_flag_attr)))
	{
		return CELL_EFAULT;
	}

	sys_event_flag_attr attr = (sys_event_flag_attr&)Memory[attr_addr];
	attr.protocol = re(attr.protocol);
	attr.pshared = re(attr.pshared);
	attr.ipc_key = re(attr.ipc_key);
	attr.flags = re(attr.flags);
	attr.type = re(attr.type);

	sys_event.Warning("name = %s", attr.name);
	sys_event.Warning("type = %d", attr.type);

	Memory.Write32(eflag_id_addr, sys_event.GetNewId(new event_flag(init, attr)));

	return CELL_OK;
}

int sys_event_flag_destroy(u32 eflag_id)
{
	sys_event.Warning("sys_event_flag_destroy(eflag_id=0x%x)", eflag_id);

	if(!sys_event.CheckId(eflag_id)) return CELL_ESRCH;

	Emu.GetIdManager().RemoveID(eflag_id);

	return CELL_OK;
}

int sys_event_flag_wait(u32 eflag_id, u64 bitptn, u32 mode, u32 result_addr, u32 timeout)
{
	sys_event.Warning("Unimplemented function: sys_event_flag_wait(eflag_id=0x%x, bitptn=0x%llx, mode=0x%x, result_addr=0x%x, timeout=0x%x)"
		, eflag_id, bitptn, mode, result_addr, timeout);
	return CELL_OK;
}

int sys_event_flag_trywait(u32 eflag_id, u64 bitptn, u32 mode, u32 result_addr)
{
	sys_event.Warning("Unimplemented function: sys_event_flag_trywait(eflag_id=0x%x, bitptn=0x%llx, mode=0x%x, result_addr=0x%x)"
		, eflag_id, bitptn, mode, result_addr);
	return CELL_OK;
}

int sys_event_flag_set(u32 eflag_id, u64 bitptn)
{
	sys_event.Warning("sys_event_flag_set(eflag_id=0x%x, bitptn=0x%llx)", eflag_id, bitptn);

	event_flag* event_flag_data = nullptr;
	if(!sys_event.CheckId(eflag_id, event_flag_data)) return CELL_ESRCH;

	event_flag_data->pattern |= bitptn;

	return CELL_OK;
}

int sys_event_flag_clear(u32 eflag_id, u64 bitptn)
{
	sys_event.Warning("sys_event_flag_clear(eflag_id=0x%x, bitptn=0x%llx)", eflag_id, bitptn);

	event_flag* event_flag_data = nullptr;
	if(!sys_event.CheckId(eflag_id, event_flag_data)) return CELL_ESRCH;

	event_flag_data->pattern &= bitptn;

	return CELL_OK;
}

int sys_event_flag_cancel(u32 eflag_id, u32 num_addr)
{
	sys_event.Warning("Unimplemented function: sys_event_flag_cancel(eflag_id=0x%x, num_addr=0x%x)"
		, eflag_id, num_addr);
	return CELL_OK;
}

int sys_event_flag_get(u32 eflag_id, u32 flag_addr)
{
	sys_event.Warning("sys_event_flag_get(eflag_id=0x%x, flag_addr=0x%x)", eflag_id, flag_addr);
	
	if(!Memory.IsGoodAddr(flag_addr, 4))
	{
		return CELL_EFAULT;
	}
	
	event_flag* event_flag_data = nullptr;
	if(!sys_event.CheckId(eflag_id, event_flag_data)) return CELL_ESRCH;

	Memory.Write64(flag_addr, event_flag_data->pattern);

	return CELL_OK;
}

//128
int sys_event_queue_create(u32 equeue_id_addr, u32 attr_addr, u64 event_queue_key, int size)
{
	sys_event.Warning("sys_event_queue_create(equeue_id_addr=0x%x, attr_addr=0x%x, event_queue_key=0x%llx, size=%d)",
		equeue_id_addr, attr_addr, event_queue_key, size);

	if(size <= 0 || size > 127)
	{
		return CELL_EINVAL;
	}

	if(!Memory.IsGoodAddr(equeue_id_addr, 4) || !Memory.IsGoodAddr(attr_addr, sizeof(sys_event_queue_attr)))
	{
		return CELL_EFAULT;
	}

	auto& attr = (sys_event_queue_attr&)Memory[attr_addr];
	sys_event.Warning("name = %s", attr.name);
	sys_event.Warning("type = %d", re(attr.type));
	EventQueue* equeue = new EventQueue();
	equeue->size = size;
	equeue->pos = 0;
	equeue->type = re(attr.type);
	strncpy(equeue->name, attr.name, 8);
	Memory.Write32(equeue_id_addr, sys_event.GetNewId(equeue));

	return CELL_OK;
}

int sys_event_queue_receive(u32 equeue_id, u32 event_addr, u32 timeout)
{
	sys_event.Warning("sys_event_queue_receive(equeue_id=0x%x, event_addr=0x%x, timeout=0x%x)",
		equeue_id, event_addr, timeout);

	if(!sys_event.CheckId(equeue_id))
	{
		return CELL_ESRCH;
	}

	int result;
	auto queue_receive = [&](int status) -> bool
	{
		if(status == CPUThread_Stopped)
		{
			result = CELL_ECANCELED;
			return false;
		}

		EventQueue* equeue = (EventQueue*)Emu.GetIdManager().GetIDData(equeue_id).m_data;
		for(int i=0; i<equeue->pos; ++i)
		{
			if(!equeue->ports[i]->has_data && equeue->ports[i]->thread)
			{
				SPUThread* thr = (SPUThread*)equeue->ports[i]->thread;
				if(thr->SPU.OutIntr_Mbox.GetCount())
				{
					u32 val;
					thr->SPU.OutIntr_Mbox.Pop(val);
					if(!thr->SPU.Out_MBox.Pop(val)) val = 0;
					equeue->ports[i]->data1 = val;
					equeue->ports[i]->data2 = 0;
					equeue->ports[i]->data3 = 0;
					equeue->ports[i]->has_data = true;
				}
			}
		}

		for(int i=0; i<equeue->pos; i++)
		{
			if(equeue->ports[i]->has_data)
			{
				auto dst = (sys_event_data&)Memory[event_addr];

				re(dst.source, equeue->ports[i]->name);
				re(dst.data1, equeue->ports[i]->data1);
				re(dst.data2, equeue->ports[i]->data2);
				re(dst.data3, equeue->ports[i]->data3);

				equeue->ports[i]->has_data = false;

				result = CELL_OK;
				return false;
			}
		}

		return true;
	};

	GetCurrentPPUThread().WaitFor(queue_receive);

	return result;
}

int sys_event_port_create(u32 eport_id_addr, int port_type, u64 name)
{
	sys_event.Warning("sys_event_port_create(eport_id_addr=0x%x, port_type=0x%x, name=0x%llx)",
		eport_id_addr, port_type, name);

	if(!Memory.IsGoodAddr(eport_id_addr, 4))
	{
		return CELL_EFAULT;
	}

	EventPort* eport = new EventPort();
	u32 id = sys_event.GetNewId(eport);
	eport->pos = 0;
	eport->has_data = false;
	eport->name = name ? name : id;
	Memory.Write32(eport_id_addr, id);

	return CELL_OK;
}

int sys_event_port_connect_local(u32 event_port_id, u32 event_queue_id)
{
	sys_event.Warning("sys_event_port_connect_local(event_port_id=0x%x, event_queue_id=0x%x)",
		event_port_id, event_queue_id);

	if(!sys_event.CheckId(event_port_id) || !sys_event.CheckId(event_queue_id))
	{
		return CELL_ESRCH;
	}

	EventPort* eport = (EventPort*)Emu.GetIdManager().GetIDData(event_port_id).m_data;
	EventQueue* equeue = (EventQueue*)Emu.GetIdManager().GetIDData(event_queue_id).m_data;
	equeue->ports[equeue->pos++] = eport;
	eport->queue[eport->pos++] = equeue;

	return CELL_OK;
}

int sys_event_port_send(u32 event_port_id, u64 data1, u64 data2, u64 data3)
{
	sys_event.Warning("sys_event_port_send(event_port_id=0x%x, data1=0x%llx, data2=0x%llx, data3=0x%llx)",
		event_port_id, data1, data2, data3);

	if(!sys_event.CheckId(event_port_id))
	{
		return CELL_ESRCH;
	}

	EventPort* eport = (EventPort*)Emu.GetIdManager().GetIDData(event_port_id).m_data;
	
	if(!eport->pos)
	{
		return CELL_ENOTCONN;
	}

	if(eport->has_data)
	{
		return CELL_EBUSY;
	}
	
	eport->has_data = true;
	eport->data1 = data1;
	eport->data2 = data2;
	eport->data3 = data3;

	return CELL_OK;
}