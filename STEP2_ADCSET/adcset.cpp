//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   находящего указанную службу АЦП,
//   программирующего его параметрами из указанного файла,
//   печатающего установленные парамтры на экране
//
// (C) InSys, 2007-2015
//
//********************************************************

#include	"brd.h"
#include	"extn.h"
#include	"ctrladc.h"
#include	"gipcy.h"

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV
#define		MAX_CHAN	32		// считаем, что каналов может быть не больше MAX_CHAN

BRDCHAR g_AdcSrvName[64] = _BRDC("FM212x1G0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM816x250M0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X400M0"); // с номером службы
S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx);

BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM212x1G.ini");
//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM816x250M.ini");
//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_214x400M.ini");


//=************************* main *************************
//=********************************************************
int BRDC_main( int argc, BRDCHAR *argv[] )
{
	S32		status;

	// чтобы все печати сразу выводились на экран
	fflush(stdout);
	setbuf(stdout, NULL);

	BRD_displayMode(BRDdm_VISIBLE | BRDdm_CONSOLE); // режим вывода информационных сообщений : отображать все уровни на консоле
	
	S32	DevNum;
	//status = BRD_initEx(BRDinit_AUTOINIT, "brd.ini", NULL, &DevNum); // инициализировать библиотеку - автоинициализация
	status = BRD_init(_BRDC("brd.ini"), &DevNum); // инициализировать библиотеку
	if(!BRD_errcmp(status, BRDerr_OK))
	{
		BRDC_printf( _BRDC("ERROR: BARDY Initialization = 0x%X\n"), status );
		BRDC_printf( _BRDC("Press any key for leaving program...\n"));
		return -1;
	}
	BRDC_printf(_BRDC("BRD_init: OK. Number of devices = %d\n"), DevNum);

	// получить список LID (каждая запись соответствует устройству)
	BRD_LidList lidList;
	lidList.item = MAX_DEV; // считаем, что устройств может быть не больше 10
	lidList.pLID = new U32[MAX_DEV];
	status = BRD_lidList(lidList.pLID, lidList.item, &lidList.itemReal);


	BRD_Info	info;
	info.size = sizeof(info);
	BRD_Handle handle[MAX_DEV];

	ULONG iDev = 0;
	// отображаем информацию об всех устройствах, указанных в ini-файле
	for(iDev = 0; iDev < lidList.itemReal; iDev++)
	{
		BRDC_printf(_BRDC("\n"));

		BRD_getInfo(lidList.pLID[iDev], &info); // получить информацию об устройстве
		if(info.busType == BRDbus_ETHERNET)
			BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, IP %u.%u.%u.%u, Port %u, PID = %d.\n"), 
				info.name, info.boardType >> 16, info.boardType & 0xff,
				(UCHAR)info.bus, (UCHAR)(info.bus >> 8), (UCHAR)(info.bus >> 16), (UCHAR)(info.bus >> 24),
													info.dev, info.pid);
		else
		{
			ULONG dev_id = info.boardType >> 16;
			if( dev_id == 0x53B1 || dev_id == 0x53B3) // FMC115cP or FMC117cP
				BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, Bus = %d, Dev = %d, G.adr = %d, Order = %d, PID = %d.\n"), 
								info.name, info.boardType >> 16, info.boardType & 0xff, info.bus, info.dev, 
										info.slot & 0xffff, info.pid >> 28, info.pid & 0xfffffff);
			else
				BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, Bus = %d, Dev = %d, Slot = %d, PID = %d.\n"), 
					info.name, info.boardType >> 16, info.boardType & 0xff, info.bus, info.dev, info.slot, info.pid);
		}

//		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_EXCLUSIVE, NULL); // открыть устройство в монопольном режиме
		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_SHARED, NULL); // открыть устройство в разделяемом режиме
		if(handle[iDev] > 0)
		{
			U32 ItemReal;
			// получить список служб
			BRD_ServList srvList[MAX_SRV];
			status = BRD_serviceList(handle[iDev], 0, srvList, MAX_SRV, &ItemReal);
			if(ItemReal <= MAX_SRV)
			{
				for(U32 j = 0; j < ItemReal; j++)
				{
					BRDC_printf(_BRDC("Service %d: %s\n"), j, srvList[j].name);
				}
				U32 iSrv;
				for(iSrv = 0; iSrv < ItemReal; iSrv++)
				{
					if(!BRDC_stricmp(srvList[iSrv].name, g_AdcSrvName))
					{
						status = SetParamSrv(handle[iDev], &srvList[iSrv], iDev);
						if(BRD_errcmp(status, BRDerr_OK))
							break;
					}
				}
				if(iSrv ==ItemReal)
					BRDC_printf(_BRDC("NO Service %s\n"), g_AdcSrvName);
			}
			else
				BRDC_printf(_BRDC("BRD_serviceList: Real Items = %d (> 16 - ERROR!!!)\n"), ItemReal);

			status = BRD_close(handle[iDev]); // закрыть устройство 
		}
	}
	delete lidList.pLID;

	BRDC_printf(_BRDC("\n"));

	status = BRD_cleanup();
	BRDC_printf(_BRDC("BRD_cleanup: OK\n"));

 	return 0;
}

void DisplayError(S32 status, const char* funcName, const BRDCHAR* cmd_str)
{
	S32	real_status = BRD_errext(status);
	BRDCHAR func_name[MAX_PATH];
#ifdef _WIN64
	mbstowcs(func_name, funcName, MAX_PATH);
#else
	BRDC_strcpy(func_name, funcName);
#endif 
	BRDCHAR msg[255];
	switch(real_status)
	{
	case BRDerr_OK:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_OK\n"), func_name, cmd_str);
		break;
	case BRDerr_BAD_MODE:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_BAD_MODE\n"), func_name, cmd_str);
		break;
	case BRDerr_INSUFFICIENT_SERVICES:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_INSUFFICIENT_SERVICES\n"), func_name, cmd_str);
		break;
	case BRDerr_BAD_PARAMETER:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_BAD_PARAMETER\n"), func_name, cmd_str);
		break;
	case BRDerr_BUFFER_TOO_SMALL:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_BUFFER_TOO_SMALL\n"), func_name, cmd_str);
		break;
	case BRDerr_WAIT_TIMEOUT:
		BRDC_sprintf(msg, _BRDC("%s - %s: BRDerr_WAIT_TIMEOUT\n"), func_name, cmd_str);
		break;
	default:
		BRDC_sprintf(msg, _BRDC("%s - %s: Unknown error, status = %8X\n"), func_name, cmd_str, real_status);
		break;
	}
    BRDC_printf(_BRDC("%s"), msg);
}

S32 AdcSettings(BRD_Handle hADC, int idx, BRDCHAR* srvName)
{
	S32		status;

	BRD_AdcCfg adc_cfg;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCFG, &adc_cfg);
	BRDC_printf(_BRDC("ADC Config: %d Bits, FIFO is %d kBytes, %d channels\n"), adc_cfg.Bits, adc_cfg.FifoSize / 1024, adc_cfg.NumChans);
	BRDC_printf(_BRDC("            Min rate = %d kHz, Max rate = %d MHz\n"), adc_cfg.MinRate / 1000, adc_cfg.MaxRate / 1000000);

	// задать параметры работы АЦП из секции ini-файла
	BRDCHAR iniFilePath[MAX_PATH];
	BRDCHAR iniSectionName[MAX_PATH];
	IPC_getCurrentDir(iniFilePath, sizeof(iniFilePath)/sizeof(BRDCHAR));
	BRDC_strcat(iniFilePath, g_iniFileName);
	BRDC_sprintf(iniSectionName, _BRDC("device%d_%s"), idx, srvName);

	BRD_IniFile ini_file;
	BRDC_strcpy(ini_file.fileName, iniFilePath);
	BRDC_strcpy(ini_file.sectionName, iniSectionName);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_READINIFILE, &ini_file);

	// получить источник и значение тактовой частоты, а также частоту дискретизации можно отдельной функцией
	BRD_SyncMode sync_mode;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETSYNCMODE, &sync_mode);
	if(BRD_errcmp(status, BRDerr_OK))
		BRDC_printf(_BRDC("BRDctrl_ADC_GETSYNCMODE: source = %d, value = %.2f MHz, rate = %.3f kHz\n"), 
										sync_mode.clkSrc, sync_mode.clkValue/1000000, sync_mode.rate/1000);
	else
		DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETSYNCMODE"));

	// получить параметры стартовой синхронизации
	// команда BRDctrl_ADC_GETSTARTMODE может получать 2 разные структуры
	// для определения какую из них использует данная служба применяем трюк с массивом )))
	U08 start_struct[40]; // наибольшая из структур имеет размер 40 байт
	memset(start_struct, 0x5A, 40);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETSTARTMODE, &start_struct);
	if(BRD_errcmp(status, BRDerr_OK))
	{
		if(start_struct[39] == 0x5A)
		{ // стартовая схема на базовом модуле (используется структура по-меньше)
			// службы: ADC414X65M, ADC216X100M, ADC1624X192, ADC818X800, ADC1612X1M
			BRD_StartMode* start = (BRD_StartMode*)start_struct;
			BRDC_printf(_BRDC("BRDctrl_ADC_GETSTARTMODE: start source = %d\n"), start->startSrc);
		}
		else
		{ // стартовая схема на субмодуле (используется большая структура)
			BRD_AdcStartMode* start = (BRD_AdcStartMode*)start_struct;
			// службы: ADC212X200M, ADC10X2G, ADC214X200M, ADC28X1G, ADC214X400M, ADC210X1G,
			// FM814X125M, FM214X250M, FM412X500M, FM212X1G
			BRDC_printf(_BRDC("BRDctrl_ADC_GETSTARTMODE: start source = %d\n"), start->src);
		}
	}
	else
		DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETSTARTMODE"));

	// получить маску включенных каналов
	ULONG chan_mask = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCHANMASK, &chan_mask);
	if(BRD_errcmp(status, BRDerr_OK))
		BRDC_printf(_BRDC("BRDctrl_ADC_GETCHANMASK: chan_mask = %0X\n"), chan_mask);
	else
		DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETCHANMASK"));
	U32 i = 0;
	S32 numChan  = 0;
	for(i = 0; i < MAX_CHAN; i++)
		numChan += (chan_mask >> i) & 0x1;

	// получить параметры включенных каналов
	BRD_ValChan value_chan;
	for(i = 0; i < adc_cfg.NumChans; i++)
	{
		if(((chan_mask >> i) & 1) == 0)
			continue;
		value_chan.chan = i;
		BRDC_printf(_BRDC("Channel %d:\n"), value_chan.chan);

		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETINPRANGE, &value_chan);
		if(BRD_errcmp(status, BRDerr_OK))
			BRDC_printf(_BRDC("Range = %f\n"), value_chan.value);
		else
			DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETINPRANGE"));

		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETBIAS, &value_chan);
		if(BRD_errcmp(status, BRDerr_OK))
			BRDC_printf(_BRDC("Bias = %f\n"), value_chan.value);
		else
			DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETBIAS"));

		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETINPRESIST, &value_chan);
		if(BRD_errcmp(status, BRDerr_OK))
		{
			if(value_chan.value)
					BRDC_printf(_BRDC("Input resist is 50 Om\n"));
			else
			{
				if(BRDC_strstr(srvName, _BRDC("ADC216X100M")))
					BRDC_printf(_BRDC("Input resist is 1 kOm\n"));
				else
					BRDC_printf(_BRDC("Input resist is 1 MOm\n"));
			}
		}
		else
			DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETINPRESIST"));

		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETDCCOUPLING, &value_chan);
		if(BRD_errcmp(status, BRDerr_OK))
		{
			if(value_chan.value)
				BRDC_printf(_BRDC("Input is OPENED\n"));
			else
				BRDC_printf(_BRDC("Input is CLOSED\n"));
		}
		else
			DisplayError(status, __FUNCTION__, _BRDC("BRDctrl_ADC_GETDCCOUPLING"));
	}

	return numChan;
}

// захватываем службу АЦП устанавливаем параметры АЦП
S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx)
{
//	S32		status;
	U32 mode = BRDcapt_EXCLUSIVE;
	BRD_Handle hADC = BRD_capture(handle, 0, &mode, srv->name, 10000);
	if(mode == BRDcapt_EXCLUSIVE) BRDC_printf(_BRDC("%s is captured in EXCLUSIVE mode!\n"), srv->name);
	if(mode == BRDcapt_SPY)	BRDC_printf(_BRDC("%s is captured in SPY mode!\n"), srv->name);
	if(hADC > 0)
	{
		S32 g_numChan = AdcSettings(hADC, idx, srv->name); // установить параметры АЦП
		return 0;
	}
	//return status;
	return -1;
}

//
// End of file
//
