﻿#include "rtc6.h"
#include "rtc6expl.h"
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>

namespace sepwind
{

using namespace rtc6;


Rtc6::Rtc6()
{
	_kfactor = 0.0;
}

Rtc6::~Rtc6()
{

}

bool	__stdcall	Rtc6::initialize(double kfactor, char* ct5FileName)
{
	int error = RTC6open();
	if (0 != error)
	{
		fprintf(stderr, "fail to initialize the rtc6 library. error code = %d", error);
		return false;
	}

	init_rtc6_dll();

	error = get_last_error();
	if (0 != error)
	{
		// 에러 리셋
		reset_error(error);
	}

	// program file load
	error = load_program_file(NULL);
	if (0 != error)
	{
		fprintf(stderr, "fail to load the rtc6 program file :  error code = %d", error);
		return false;
	}

	// Rtc6는 laser 및 gate신호 레벨을 설정할수가 있다
	// active high 로 설정
	int sigLevel = (0x01 << 3) | (0x01 << 4);
	set_laser_control(sigLevel);

	_kfactor = kfactor;

	error = load_correction_file(
		ct5FileName,		// ctb
		1,	// table no (1 ~ 4)
		2	// 2d
	);

	if (0 != error)
	{
		fprintf(stderr, "fail to load the correction file :  error code = %d", error);
		return false;
	}

	select_cor_table(1, 0);	//1 correction file at primary head

	set_standby(0, 0);

	set_laser_mode(0);	//co2 mode

	short ctrlMode = \
		0x01 << 0 +	//ext start enabled
		0x01 << 1; // ext stop enabled
	set_control_mode(ctrlMode);

	config_list(4000, 4000);

	return true;
}

bool __stdcall	Rtc6::listBegin()
{
	_list = 1;
	_listcnt = 0;
	set_start_list(1);
	return true;
}

bool __stdcall	Rtc6::listTiming(double frequency, double pulsewidth)
{
	double period = 1.0f / frequency * (double)1.0e6;	//usec
	double halfperiod = period / 2.0f;

	set_laser_timing(
		halfperiod * 64,	//half period (us)
		pulsewidth * 64,
		0,
		0);	// timebase 1/64 usec	
	return true;
}

bool __stdcall	Rtc6::listDelay(double on, double off, double jump, double mark, double polygon)
{
	set_scanner_delays(
		(jump / 10.0f),
		(mark / 10.0f),
		(polygon / 10.0f)
	);
	// unit: 10 usec

	return true;
}

bool __stdcall	Rtc6::listSpeed(double jump, double mark)
{
	double jump_bitpermsec = (double)(jump / 1.0e3 * _kfactor);
	double mark_bitpermsec = (double)(mark / 1.0e3 * _kfactor);

	set_jump_speed(jump_bitpermsec);
	set_mark_speed(mark_bitpermsec);
	return true;
}

bool __stdcall	Rtc6::listJump(double x, double y)
{
	int xbits = x * _kfactor;
	int ybits = y * _kfactor;
	jump_abs(xbits, ybits);
	return true;
}

bool __stdcall	Rtc6::listMark(double x, double y)
{
	int xbits = x * _kfactor;
	int ybits = y * _kfactor;
	mark_abs(xbits, ybits);
	return true;
}

bool __stdcall	Rtc6::listArc(double cx, double cy, double sweepAngle)
{
	int cxbits = cx * _kfactor;
	int cybits = cy * _kfactor;
	arc_abs(cxbits, cybits, -sweepAngle);
	return true;
}

bool	__stdcall Rtc6::listOn(double msec)
{
	double remind_msec = msec;
	while (remind_msec > 1000)
	{
		laser_on_list(1000 * 1000 / 10);
		remind_msec -= 1000;
	}

	laser_on_list(remind_msec * 1000 / 10);
	return TRUE;
}

bool	__stdcall	Rtc6::listOff()
{
	laser_signal_off_list();
	return true;
}


bool __stdcall	Rtc6::listEnd()
{
	set_end_of_list();
	return TRUE;
}

bool __stdcall Rtc6::listExecute(bool wait)
{
	execute_list(1);	//list 1

	if (wait)
	{
		unsigned int busy(0), position(0);
		do {
			::Sleep(50);
			get_status(&busy, &position);
		} while (busy);
	}
	return true;
}


typedef union
{
	UINT32 value;
	struct
	{
		UINT32	load1 : 1;		
		UINT32	load2 : 1;
		UINT32	ready1 : 1;		
		UINT32	ready2 : 1;
		UINT32	busy1 : 1;		
		UINT32	busy2 : 1;
		UINT32	used1 : 1;
		UINT32	used2 : 1;
		UINT32	reserved : 24;
	};
}READ_STATUS;


bool Rtc6::isBufferReady(UINT count)
{
	if ((_listcnt + count) >= 8000)
	{
		UINT busy(0), position(0);
		get_status(&busy, &position);
		if (!busy)
		{
			set_end_of_list();        
			execute_list(_list);
			_list = _list ^ 0x03;
			set_start_list(_list);			
		}
		else
		{
			set_end_of_list();
			auto_change();			
			READ_STATUS s;
			switch (_list)
			{
			case 1:
				do
				{
					s.value = read_status();
					::Sleep(10);
				} while (s.busy2);
				break;

			case 2:
				do
				{
					s.value = read_status();
					::Sleep(10);
				} while (s.busy1);
				break;
			}
			_list = _list ^ 0x03;        
			set_start_list(_list);			
		}

		_listcnt = count;
	}
	_listcnt += count;
	return TRUE;
}


}//namespace