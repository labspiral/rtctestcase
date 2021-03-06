﻿#include "rtc6.h"
#include "rtc6expl.h"
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>

namespace sepwind
{

using namespace rtc6;


Rtc6::Rtc6(double xCntPerMm, double yCntPerMm)
{
	_kfactor = 0.0;
	_xCntPerMm = xCntPerMm;
	_yCntPerMm = yCntPerMm;
}

Rtc6::~Rtc6()
{

}

bool	__stdcall	Rtc6::initialize(double kfactor, char* ct5FileName)
{
	int error = RTC6open();
	if (0 != error)
	{
		fprintf(stderr, "fail to initialize the rtc6 library. error code = %d\r\n", error);
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
		fprintf(stderr, "fail to load the rtc6 program file :  error code = %d\r\n", error);
		return false;
	}

	UINT32 rtcVersion = get_rtc_version();
	fprintf(stdout, "card count : %d. dll, hex, firmware version : %d, %d, %d\r\n", \
		rtc6_count_cards(), get_dll_version(), get_hex_version(), rtcVersion & 0x0F);

	if (rtcVersion & 0x100)
		fprintf(stdout, "processing on the fly option enabled\r\n");

	if (rtcVersion & 0x200)
		fprintf(stdout, ("2nd scanhead option enabled\r\n"));

	if (rtcVersion & 0x400)
	{
		fprintf(stdout, ("3d option (varioscan) enabled\r\n"));
		_3d = TRUE;
	}
	else
		_3d = FALSE;

	// Rtc6는 laser 및 gate신호 레벨을 설정할수가 있다
	// active high 로 설정
	int sigLevel = (0x01 << 3) | (0x01 << 4);
	set_laser_control(sigLevel);

	_kfactor = kfactor;

	if (_3d)
		error = load_correction_file(
			ct5FileName,		// ctb
			1,	// table no (1 ~ 4)
			3	// 3d
		);
	else
		error = load_correction_file(
			ct5FileName,		// ctb
			1,	// table no (1 ~ 4)
			2	// 2d
		);

	if (0 != error)
	{
		fprintf(stderr, "fail to load the correction file :  error code = %d\r\n", error);
		return false;
	}

	if (_3d)
		select_cor_table(1, 1);	//1 correction file at primary / secondary head	
	else
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

bool	__stdcall	Rtc6::ctrlGetGatherSize(unsigned int* pReturnSize)
{
	UINT busy = 0;
	UINT position = 0;
	measurement_status(&busy, &position);
	if (busy > 0)
		return false;
	*pReturnSize = position;
	return true;
}

bool	__stdcall	Rtc6::ctrlGetGatherData(int channel, long* pReturnData, unsigned int size)
{
	if (size <= 0)
		return false;

	get_waveform(channel, size, (LONG_PTR)pReturnData);
	return true;
}

bool	__stdcall	Rtc6::ctrlGetEncoder(int* encX, int* encY, double* mmX, double* mmY)
{
	LONG enc[2] = { 0, };
	get_encoder(&enc[0], &enc[1]);
	*encX = enc[0];
	*encX = enc[1];

	if (0.0 == _xCntPerMm || 0.0 == _yCntPerMm)
		return false;

	*mmX = (double)enc[0] / _xCntPerMm;
	*mmY = (double)enc[1] / _yCntPerMm;
	return true;
}

bool	__stdcall	Rtc6::ctrlEncoderReset()
{
	init_fly_2d(0, 0);
	return true;
}

bool __stdcall	Rtc6::listBegin()
{
	_list = 1;
	_listcnt = 0;
	set_start_list(1);
	_matrices.clear();
	return true;
}

bool __stdcall	Rtc6::listTiming(double frequency, double pulsewidth)
{
	double period = 1.0f / frequency * (double)1.0e6;	//usec
	double halfperiod = period / 2.0f;

	if (!this->isBufferReady(1))
		return false;

	set_laser_timing(
		halfperiod * 64,	//half period (us)
		pulsewidth * 64,
		0,
		0);	// timebase 1/64 usec	
	return true;
}

bool __stdcall	Rtc6::listDelay(double on, double off, double jump, double mark, double polygon)
{
	if (!this->isBufferReady(2))
		return false;
	set_laser_delays(
		(LONG)(on * 64.0), // 1/64usec
		(LONG)(off * 64.0)
	);
	set_scanner_delays(
		(jump / 10.0f),
		(mark / 10.0f),
		(polygon / 10.0f)
	);
	return true;
}

bool __stdcall	Rtc6::listSpeed(double jump, double mark)
{
	double jump_bitpermsec = (double)(jump / 1.0e3 * _kfactor);
	double mark_bitpermsec = (double)(mark / 1.0e3 * _kfactor);
	if (!this->isBufferReady(2))
		return false;
	set_jump_speed(jump_bitpermsec);
	set_mark_speed(mark_bitpermsec);
	return true;
}

bool	__stdcall	Rtc6::listMatrixLoadIdent()
{
	_matrices.clear();
	MATRIX3D ident;
	MAT_IDENT(&ident);
	_matrices.push_back(ident);

	if (!this->isBufferReady(2))
		return false;

	// 2*2 matrix
	set_matrix(1,
		1, 0,
		0, 1,
		1);
	// dx/dy
	set_offset(1,
		0, 0,
		1);
	return true;
}

bool	__stdcall	Rtc6::listMatrixPush(const MATRIX3D& m)
{
	_matrices.push_back(m);

	MATRIX3D mResult = _matrices[0];
	for (size_t i = 1; i < _matrices.size(); i++)
	{
		mResult = MAT_MULTI(&mResult, &_matrices[i]);
	}

	if (!this->isBufferReady(2))
		return false;

	// 2*2 matrix
	set_matrix(1,
		mResult.e[0], mResult.e[1],
		mResult.e[3], mResult.e[4],
		1);

	// dx/dy
	set_offset(1,
		mResult.e[2], mResult.e[5],
		1);
	return true;
}

bool	__stdcall	Rtc6::listMatrixPop()
{
	if (_matrices.size() <= 1)
	{
		fprintf(stderr, "mismatch push/pop to matrices");
		return false;
	}

	_matrices.pop_back();
	MATRIX3D mResult = _matrices[0];
	for (size_t i = 1; i < _matrices.size(); i++)
	{
		mResult = MAT_MULTI(&mResult, &_matrices[i]);
	}

	if (!this->isBufferReady(2))
		return false;

	// 2*2 matrix
	set_matrix(1,
		mResult.e[0], mResult.e[1],
		mResult.e[3], mResult.e[4],
		1);

	// dx/dy
	set_offset(1,
		mResult.e[2], mResult.e[5],
		1);
	return true;
}

bool __stdcall	Rtc6::listJump(double x, double y, double z)
{
	int xbits = x * _kfactor;
	int ybits = y * _kfactor;
	int zbits = z * _kfactor;
	if (!this->isBufferReady(1))
		return false;
	if (_3d)
		jump_abs_3d(xbits, ybits, zbits);
	else
		jump_abs(xbits, ybits);
	return true;
}

bool __stdcall	Rtc6::listMark(double x, double y, double z)
{
	int xbits = x * _kfactor;
	int ybits = y * _kfactor;
	int zbits = z * _kfactor;
	if (!this->isBufferReady(1))
		return false;
	if (_3d)
		mark_abs_3d(xbits, ybits, zbits);
	else
		mark_abs(xbits, ybits);
	return true;
}

bool __stdcall	Rtc6::listArc(double cx, double cy, double sweepAngle, double z)
{
	int cxbits = cx * _kfactor;
	int cybits = cy * _kfactor;
	int zbits = z * _kfactor;
	if (!this->isBufferReady(1))
		return false;
	if (_3d)
		arc_abs_3d(cxbits, cybits, zbits, -sweepAngle);
	else
		arc_abs(cxbits, cybits, -sweepAngle);
	return true;
}

bool	__stdcall Rtc6::listOn(double msec)
{
	double remind_msec = msec;
	while (remind_msec > 1000)
	{
		if (!this->isBufferReady(1))
			return false;
		laser_on_list(1000 * 1000 / 10);
		remind_msec -= 1000;
	}

	if (!this->isBufferReady(1))
		return false;
	laser_on_list(remind_msec * 1000 / 10);
	return true;
}

bool	__stdcall	Rtc6::listOff()
{
	if (!this->isBufferReady(1))
		return false;
	laser_signal_off_list();
	return true;
}

bool __stdcall	Rtc6::listEnd()
{
	set_end_of_list();
	return true;
}

bool	__stdcall	Rtc6::listGatherBegin(double usec, int channel1, int channel2)
{
	if (!this->isBufferReady(1))
		return false;

	set_trigger(usec / 10, channel1, channel2);
	return true;
}

bool	__stdcall	Rtc6::listGatherEnd()
{
	if (!this->isBufferReady(1))
		return false;

	set_trigger(0, 0, 0);
	return true;
}

bool	__stdcall	Rtc6::listOnTheFlyBegin(bool encoderReset)
{
	if (!this->isBufferReady(1))
		return false;

	if (0.0 == _xCntPerMm || 0.0 == _yCntPerMm)
	{
		return false;	/// invalid cnt/mm
	}

	if (!this->isBufferReady(1))
		return false;

	double scalingFactor[2] = { \
		_kfactor / _xCntPerMm,
		_kfactor / _yCntPerMm
	};

	if (encoderReset)
		set_fly_2d(scalingFactor[0], scalingFactor[1]);
	else
		activate_fly_2d(scalingFactor[0], scalingFactor[1]);

	return true;
}

bool	__stdcall	Rtc6::listOnTheFlyPosWait(bool xORy, double xyPos, int condition)
{
	if (!this->isBufferReady(1))
		return false;

	switch (xORy)
	{
	case 0:	//x
		wait_for_encoder_mode(xyPos * _xCntPerMm, 0, condition);
		break;
	case 1:	//y
		wait_for_encoder_mode(xyPos * _yCntPerMm, 1, condition);
		break;
	}

	return true;
}

bool	__stdcall	Rtc6::listOnTheFlyRangeWait(double x, double rangeX, double y, double rangeY)
{
	if (!this->isBufferReady(1))
		return false;

	wait_for_encoder_in_range(\
		_xCntPerMm * (x - rangeX), _xCntPerMm * (x + rangeX),
		_yCntPerMm * (y - rangeY), _yCntPerMm * (x + rangeY)
	);
	return true;
}

bool	__stdcall	Rtc6::listOnTheFlyEnd(double jumpTox, double jumpToy)
{
	if (!this->isBufferReady(1))
		return false;

	fly_return(jumpTox * _kfactor, jumpToy * _kfactor);
	return true;
}

bool __stdcall Rtc6::listExecute(bool wait)
{
	UINT busy(0), position(0);
	get_status(&busy, &position);
	if (busy)
		auto_change();
	else
	{
		execute_list(_list);
	}

	if (wait)
	{
		do
		{
			get_status(&busy, &position);
			::Sleep(10);
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
	if ((_listcnt + count) >= 3500)
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
	return true;
}


}//namespace