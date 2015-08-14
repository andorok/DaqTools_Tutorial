//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   находящего указанную службу АЦП,
//   программирующего его параметрами из указанного файла,
//   выполняющего сбор данных программным методом (не DMA)
//
// (C) InSys, 2007-2015
//
//********************************************************

#include	"brd.h"
#include	"extn.h"
#include	"ctrladc.h"
#include	"ctrlsdram.h"
#include	"gipcy.h"

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV
#define		MAX_CHAN	32		// считаем, что каналов может быть не больше MAX_CHAN

//BRDCHAR g_AdcSrvName[64] = _BRDC("FM816x250M0"); // с номером службы
BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X400M0"); // с номером службы
S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx);
S32 DaqIntoFifo(BRD_Handle hADC, PVOID pSig, ULONG bBufSize);

//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM816x250M.ini");
BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_214x400M.ini");

U32 g_bBufSize; // размер собираемых данных (в байтах)
U32 g_MemAsFifo = 1; // 1 - использовать динамическую память на модуле в качестве FIFO

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
				U32 iSrv;
				BRD_Handle hADC;
				for(U32 j = 0; j < ItemReal; j++)
				{
					BRDC_printf(_BRDC("Service %d: %s\n"), j, srvList[j].name);
				}
				for(iSrv = 0; iSrv < ItemReal; iSrv++)
				{
					if(!BRDC_stricmp(srvList[iSrv].name, g_AdcSrvName))
					{
						hADC = SetParamSrv(handle[iDev], &srvList[iSrv], iDev);
						break;
					}
				}
				if(iSrv ==ItemReal)
					BRDC_printf(_BRDC("NO Service %s\n"), g_AdcSrvName);
				else
					if(hADC > 0)
					{
						void* pSig = new char[g_bBufSize];
						DaqIntoFifo(hADC, pSig, g_bBufSize);

						IPC_handle hfile = 0;
						hfile = IPC_openFile(_BRDC("data.bin"), IPC_CREATE_FILE | IPC_FILE_WRONLY);
						IPC_writeFile(hfile, pSig, g_bBufSize);
						IPC_closeFile(hfile);

						delete pSig;
					}
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

S32 AdcSettings(BRD_Handle hADC, int idx, BRDCHAR* srvName)
{
	S32		status;

	BRD_AdcCfg adc_cfg;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCFG, &adc_cfg);
	BRDC_printf(_BRDC("ADC Config: %d Bits, FIFO is %d kBytes, %d channels\n"), adc_cfg.Bits, adc_cfg.FifoSize / 1024, adc_cfg.NumChans);
	BRDC_printf(_BRDC("            Min rate = %d kHz, Max rate = %d MHz\n"), adc_cfg.MinRate / 1000, adc_cfg.MaxRate / 1000000);

	g_bBufSize = adc_cfg.FifoSize;

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

	if(g_MemAsFifo)
	{	
		// проверяем наличие динамической памяти
		BRD_SdramCfgEx SdramConfig;
		SdramConfig.Size = sizeof(BRD_SdramCfgEx);
		ULONG PhysMemSize;
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_GETCFGEX, &SdramConfig);
		if(status < 0)
		{
			BRDC_printf(_BRDC("Get SDRAM Config: Error!!!\n"));
			g_MemAsFifo = 0;
			return BRDerr_OK;
		}
		else
		{
			if(SdramConfig.MemType == 11) //DDR3
				PhysMemSize =	(ULONG)((
										(((__int64)SdramConfig.CapacityMbits * 1024 * 1024) >> 3) * 
										(__int64)SdramConfig.PrimWidth / SdramConfig.ChipWidth * SdramConfig.ModuleBanks *
										SdramConfig.ModuleCnt) >> 2); // в 32-битных словах
			else
				PhysMemSize =	(1 << SdramConfig.RowAddrBits) *
								(1 << SdramConfig.ColAddrBits) * 
								SdramConfig.ModuleBanks * 
								SdramConfig.ChipBanks *
								SdramConfig.ModuleCnt * 2; // в 32-битных словах
		}
		if(PhysMemSize)
		{ // динамическая память присутствует на модуле
			BRDC_printf(_BRDC("SDRAM Config: Memory size = %d MBytes\n"), (PhysMemSize / (1024 * 1024)) * 4);

			// установить параметры SDRAM
			ULONG target = 2; // будем осуществлять сбор данных в память
			status = BRD_ctrl(hADC, 0, BRDctrl_ADC_SETTARGET, &target);

			ULONG fifo_mode = 1; // память используется как FIFO
			status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_SETFIFOMODE, &fifo_mode);

			g_bBufSize = PhysMemSize; // собирать будем в 4 раза меньше, чем памяти на модуле
			if(PhysMemSize > 32 * 1024 * 1024)
				g_bBufSize = 32 * 1024 * 1024; // собирать будем 32 Мбайта
			BRDC_printf(_BRDC("SDRAM as a FIFO mode!!!\n"));

		}
		else
		{
			 // освободить службу SDRAM (она могла быть захвачена командой BRDctrl_SDRAM_GETCFG, если та отработала без ошибки)
			ULONG mem_size = 0;
			status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_SETMEMSIZE, &mem_size);
			BRDC_printf(_BRDC("No SDRAM on board!!!\n"));
			g_MemAsFifo = 0;
		}

	}
	return status;
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
		AdcSettings(hADC, idx, srv->name); // установить параметры АЦП
	}
	return hADC;
}

// выполнить сбор данных в FIFO с программным методом передачи в ПК
S32 DaqIntoFifo(BRD_Handle hADC, PVOID pSig, ULONG bBufSize)
{
	S32		status;
	ULONG Status = 0;
	ULONG Enable = 1;
	BRD_DataBuf data_buf;
	data_buf.pData = pSig;
	data_buf.size = bBufSize;

	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFORESET, NULL); // сброс FIFO АЦП
	if(g_MemAsFifo)
	{
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_FIFORESET, NULL); // сброс FIFO SDRAM
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_ENABLE, &Enable); // разрешение записи в SDRAM
	}
	BRDC_printf(_BRDC("ADC Start...     \r"));
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // разрешение работы АЦП

	if(g_MemAsFifo)
	{
		// дожидаемся заполнения половины выходного FIFO (а не самой SDRAM)
		do {
			status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_FIFOSTATUS, &Status);
		} while(!(Status & 0x10));
	}
	else
	{
		// дожидаемся заполнения FIFO
		do {
			status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFOSTATUS, &Status);
		} while(Status & 0x40);
	}

	if(g_MemAsFifo)
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_GETDATA, &data_buf);

	Enable = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // запрет работы АЦП
	BRDC_printf(_BRDC("ADC Stop         \r"));

	if(g_MemAsFifo)
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_ENABLE, &Enable); // запрет записи в SDRAM

	if(!g_MemAsFifo)
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETDATA, &data_buf);

	if(g_MemAsFifo)
		BRDC_printf(_BRDC("DAQ from SDRAM as FIFO is complete!!!\n"));
	else
		BRDC_printf(_BRDC("DAQ from FIFO is complete!!!\n"));
	return status;
}

//
// End of file
//
