//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   находящего указанную службу АЦП,
//   программирующего его параметрами из указанного файла,
//   выполняющего сбор данных в память на модуле
//   с ожиданием окончания сбора по прерыванию
//   Этот метод НЕ является универсальным, 
//   он работает ТОЛЬКО с модулями на PCI/PCI-express и ТОЛЬКО под Windows
//
// (C) InSys, 2007-2016
//
//********************************************************

#include	"brd.h"
#include	"extn.h"
#include	"ctrladc.h"
#include	"ctrlsdram.h"
#include	"ctrlstrm.h"
#include	"gipcy.h"
//#include	<process.h> 

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV
#define		MAX_CHAN	32		// считаем, что каналов может быть не больше MAX_CHAN

BRDCHAR g_SdramSrv[64] = _BRDC("BASESDRAM"); // без номера службы

BRDCHAR g_AdcSrvName[64] = _BRDC("FM412x500M0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM212x1G0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X400M0"); // с номером службы

S32 SdramCapture(BRD_Handle handle, BRDCHAR* srvName);
S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx);
S32 AllocDaqBuf(BRD_Handle hADC, PVOID* &pSig, unsigned long long* pbytesBufSize, int memType, int* pBlkNum);
S32 FreeDaqBuf(BRD_Handle hADC, ULONG blkNum);
S32 StartDaqIntoSdramDMA(BRD_Handle hADC, BRD_Handle hMEM);
S32 CheckDaqIntoSdramDMA();
void BreakDaqIntoSdramDMA();
S32 EndDaqIntoSdramDMA();
//S32 DaqIntoSdramDMA(BRD_Handle hADC, BRD_Handle hMem);
S32 DataFromMem(BRD_Handle hADC);

BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//adc.ini");

BRDctrl_StreamCBufAlloc g_buf_dscr; // описание буфера стрима
unsigned long long g_bBufSize; // размер буфера данных для стрима (в байтах)
unsigned long long g_bMemSize; // размер собираемых в память на модуль данных (в байтах)
int g_MemType = 0; // тип памяти для стрима

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
				BRD_Handle hADC = -1, hMem = -1;
				for(U32 j = 0; j < ItemReal; j++)
				{
					BRDC_printf(_BRDC("Service %d: %s\n"), j, srvList[j].name);
				}
				for(iSrv = 0; iSrv < ItemReal; iSrv++)
				{
					if(BRDC_strstr(srvList[iSrv].name, g_SdramSrv))
					{
						hMem = SdramCapture(handle[iDev], srvList[iSrv].name);
					}
					if(!BRDC_stricmp(srvList[iSrv].name, g_AdcSrvName))
					{
						hADC = SetParamSrv(handle[iDev], &srvList[iSrv], iDev);
						break;
					}
				}
				if(hMem > 0 && hADC > 0)
				{
					int blk_num = 1;
					PVOID* pSig = NULL; // указатель на массив указателей на блоки памяти с сигналом
					g_bBufSize = 32* 1024 * 1024; // из памяти устройства будем получать данные блоками по 32 Мбайта
					AllocDaqBuf(hADC, pSig, &g_bBufSize, g_MemType, &blk_num);
					int fl_break = 0;
					status = StartDaqIntoSdramDMA(hADC, hMem); // стартует сбор данных в память с использованием прерывания по окончанию сбора
					while (!CheckDaqIntoSdramDMA()) // проверяет завершение сбора данных
					{
						if (IPC_kbhit()) // проверяет была ли нажата клавиша
						{
							int ch = IPC_getch(); // получает клавишу
							if (0x1B == ch) // если Esc
							{
								BreakDaqIntoSdramDMA(); // // прерывает сбор данных
								fl_break = 1;
							}
						}
					}
					status = EndDaqIntoSdramDMA(); // закрывает открытые описатели (Handles)

					if (BRD_errcmp(status, BRDerr_OK) && !fl_break)
					{
						IPC_handle hfile = 0;
						hfile = IPC_openFile(_BRDC("data.bin"), IPC_CREATE_FILE | IPC_FILE_WRONLY);
						int times = int(g_bMemSize / g_bBufSize);
						for (int i = 0; i < times; i++)
						{
							DataFromMem(hADC);
							for (int iBlk = 0; iBlk < blk_num; iBlk++)
								IPC_writeFile(hfile, pSig[iBlk], g_buf_dscr.blkSize);
						}
						IPC_closeFile(hfile);
					}
					else
					{
						BRDC_printf(_BRDC("Daq into board memory is NOT complete (0x%08X) - timeout 1 sec\n"), status);
					}
					FreeDaqBuf(hADC, blk_num);

					status = BRD_release(hMem, 0);
					status = BRD_release(hADC, 0);
				}
				else
					BRDC_printf(_BRDC("NO ADC (%s) or Memory Services \n"), g_AdcSrvName);
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

	_getch();

 	return 0;
}

// захватываем службу памяти 
S32 SdramCapture(BRD_Handle handle, BRDCHAR* srvName)
{
	S32		status;

	U32 mode = BRDcapt_EXCLUSIVE;
	BRD_Handle hMem = BRD_capture(handle, 0, &mode, srvName, 10000);
	if(mode == BRDcapt_EXCLUSIVE) BRDC_printf(_BRDC("%s is captured in EXCLUSIVE mode!\n"), srvName);
	if(mode == BRDcapt_SPY)	BRDC_printf(_BRDC("%s is captured in SPY mode!\n"), srvName);
	if(hMem > 0)
	{
		BRD_SdramCfgEx SdramConfig;
		SdramConfig.Size = sizeof(BRD_SdramCfgEx);
		ULONG PhysMemSize;
		status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_GETCFGEX, &SdramConfig);
		if(status < 0)
		{
			BRDC_printf(_BRDC("Get SDRAM Config: Error!!!\n"));
			BRD_release(hMem, 0); // освободить службу SDRAM 
			return status;
		}
		else
		{
			if(SdramConfig.MemType == 11) //DDR3
			{
				PhysMemSize =	(ULONG)((
										(((__int64)SdramConfig.CapacityMbits * 1024 * 1024) >> 3) * 
										(__int64)SdramConfig.PrimWidth / SdramConfig.ChipWidth * SdramConfig.ModuleBanks *
										SdramConfig.ModuleCnt) >> 2); // в 32-битных словах
				BRDC_printf(_BRDC("DDR3 SDRAM:\n"));
			}
			else
			{
				PhysMemSize =	(1 << SdramConfig.RowAddrBits) *
								(1 << SdramConfig.ColAddrBits) * 
								SdramConfig.ModuleBanks * 
								SdramConfig.ChipBanks *
								SdramConfig.ModuleCnt * 2; // в 32-битных словах
				BRDC_printf(_BRDC("DDR2 SDRAM:\n"));
			}
		}

		if(PhysMemSize)
		{ // динамическая память присутствует на модуле
			BRDC_printf(_BRDC("        Memory size = %d MBytes\n"), (PhysMemSize / (1024 * 1024)) * 4);
			BRDC_printf(_BRDC("        %d modules, %d banks\n"), SdramConfig.ModuleCnt, SdramConfig.ModuleBanks);

			// установить параметры SDRAM
			ULONG fifo_mode = 0; // память используется как память
			status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_SETFIFOMODE, &fifo_mode);

			//ULONG mem_size = PhysMemSize >> 3;  // собирать будем в 8 раза меньше, чем памяти на модуле
			ULONG mem_size = PhysMemSize;  // собирать будем в 4 раза меньше, чем памяти на модуле
			status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_SETMEMSIZE, &mem_size);
			g_bMemSize = __int64(mem_size) << 2; // получить фактический размер активной зоны в байтах
			BRDC_printf(_BRDC("SDRAM size for DAQ data = %d MBytes\n"), int(g_bMemSize / (1024 * 1024)));

		}
		else
		{	// освободить службу SDRAM 
			BRD_release(hMem, 0);
			return status;
		}

	}
	return hMem;
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

	ULONG target = 2; // будем осуществлять сбор данных в память
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_SETTARGET, &target);

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

#ifdef _WIN32
#define MAX_BLOCK_SIZE 1073741824		// максимальный размер блока = 1 Гбайт 
#else  // LINUX
#define MAX_BLOCK_SIZE 4194304			// максимальный размер блока = 4 Mбайтa 
#endif

// размещение буфера для получения данных с АЦП через Стрим
//	hADC - дескриптор службы АЦП (IN)
//	pSig - указатель на массив указателей (IN), каждый элемент массива является указателем на блок (OUT)
//	pbytesBufSize - общий размер данных (всех блоков составного буфера), которые должны быть выделены (IN/OUT - может меняться внутри функции)
//	memType - тип памяти для данных (IN): 
//		0 - пользовательская память выделяется в драйвере (точнее, в DLL базового модуля)
//		1 - системная память выделяется драйвере 0-го кольца
//		2 - пользовательская память выделяется в приложении
//	pBlkNum - число блоков составного буфера (OUT)
S32 AllocDaqBuf(BRD_Handle hADC, PVOID* &pSig, unsigned long long* pbytesBufSize, int memType, int* pBlkNum)
{
	S32		status;

	unsigned long long bBufSize = *pbytesBufSize;
	ULONG bBlkSize;
	ULONG blkNum = 1;
	// определяем число блоков составного буфера
	if(bBufSize > MAX_BLOCK_SIZE)
	{
		do {
			blkNum <<= 1;
			bBufSize >>= 1;
		}while(bBufSize > MAX_BLOCK_SIZE);
	}
	bBlkSize = (ULONG)bBufSize;

	void** pBuffer = NULL;
	if(2 == memType)
	{
		//pBuffer = malloc(*pbytesBufSize);
		pBuffer = new PVOID[blkNum];
		for(ULONG i = 0; i < blkNum; i++)
		{
			pBuffer[i] = IPC_virtAlloc(bBlkSize);
			//pBuffer[i] = VirtualAlloc(NULL, bBlkSize, MEM_COMMIT, PAGE_READWRITE);
			if(!pBuffer[i])
			{
				BRDC_printf(_BRDC("IPC_virtAlloc() by allocating buffer %d is error!!!\n"), i);
				return -1; // error
			}
		}
	}
	g_buf_dscr.dir = BRDstrm_DIR_IN;
	g_buf_dscr.isCont = memType;
	g_buf_dscr.blkNum = blkNum;
	g_buf_dscr.blkSize = bBlkSize;//*pbytesBufSize;
	g_buf_dscr.ppBlk = new PVOID[g_buf_dscr.blkNum];
	if(g_buf_dscr.isCont == 2)
	{
		for(ULONG i = 0; i < blkNum; i++)
			g_buf_dscr.ppBlk[i] = pBuffer[i];
		delete[] pBuffer;
	}
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_ALLOC, &g_buf_dscr);
	if(BRD_errcmp(status, BRDerr_PARAMETER_CHANGED))
	{ // может быть выделено меньшее количество памяти
		BRDC_printf(_BRDC("Warning!!! BRDctrl_STREAM_CBUF_ALLOC: BRDerr_PARAMETER_CHANGED\n"));
		status = BRDerr_OK;
	}
	else
	{
		if(BRD_errcmp(status, BRDerr_OK))
		{
			BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC SUCCESS: status = 0x%08X\n"), status);
		}
		else
		{
			if(BRD_errcmp(status, BRDerr_NOT_ENOUGH_MEMORY))
				BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_NOT_ENOUGH_MEMORY (0x%08X)\n"), status);
			else
				if(BRD_errcmp(status, BRDerr_BAD_PARAMETER))
					BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_BAD_PARAMETER (0x%08X)\n"), status);
				else
					if(BRD_errcmp(status, BRDerr_INSUFFICIENT_RESOURCES))
						BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_INSUFFICIENT_RESOURCES (0x%08X)\n"), status);
					else
						if(BRD_errcmp(status, BRDerr_CMD_UNSUPPORTED))
							BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_CMD_UNSUPPORTED (0x%08X)\n"), status);
						else
							BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: status = 0x%08X\n"), status);

			if(2 == memType)
			{
				for(ULONG i = 0; i < blkNum; i++)
				{
					IPC_virtFree(g_buf_dscr.ppBlk[i]);
					//VirtualFree(g_buf_dscr.ppBlk[i], 0, MEM_RELEASE);
				}
			}
			delete[] g_buf_dscr.ppBlk;
			return status;
		}
	}
	pSig = new PVOID[blkNum];
	for(ULONG i = 0; i < blkNum; i++)
	{
		pSig[i] = g_buf_dscr.ppBlk[i];
	}
	*pbytesBufSize = (unsigned long long)g_buf_dscr.blkSize * blkNum;
	*pBlkNum = blkNum;
	BRDC_printf(_BRDC("Allocated memory for Stream:: Number of blocks = %d, Block size = %d kBytes\n"), 
													blkNum, g_buf_dscr.blkSize/1024);
	return status;
}

// освобождение буфера стрима
S32 FreeDaqBuf(BRD_Handle hADC, ULONG blkNum)
{
	S32		status;
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_FREE, NULL);
	if(g_buf_dscr.isCont == 2)
	{
		for(ULONG i = 0; i < blkNum; i++)
		{
			IPC_virtFree(g_buf_dscr.ppBlk[i]);
			//VirtualFree(g_buf_dscr.ppBlk[i], 0, MEM_RELEASE);
		}
	}
	delete[] g_buf_dscr.ppBlk;
	return status;
}

ULONG evt_status = BRDerr_OK;

typedef struct _THREAD_PARAM {
	BRD_Handle hADC;
	BRD_Handle hMem;
} THREAD_PARAM, *PTHREAD_PARAM;

IPC_handle g_hThread;
//HANDLE g_hThread = NULL;
THREAD_PARAM thread_par;
HANDLE g_hUserEvent = NULL;

// выполнить сбор данных в SDRAM с ПДП-методом передачи в ПК
// с использованием прерывания по окончанию сбора
thread_value __IPC_API ThreadDaqIntoSdramDMA(void* pParams)
//unsigned __stdcall ThreadDaqIntoSdramDMA(void* pParams)
{
	S32		status;
	ULONG AdcStatus = 0;
	ULONG Status = 0;
	ULONG isAcqComplete = 0;
	evt_status = BRDerr_OK;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	PTHREAD_PARAM pThreadParam = (PTHREAD_PARAM)pParams;
	BRD_Handle hADC = pThreadParam->hADC;
	BRD_Handle hMem = pThreadParam->hMem;

	ULONG Enable = 1;

	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_IRQACQCOMPLETE, &Enable); // разрешение прерывания от флага завершения сбора в SDRAM
																	   //status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_FIFOSTATUS, &Status);

	// для определение скорости сбора данных
	IPC_TIMEVAL start;
	IPC_TIMEVAL stop;

	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFORESET, NULL); // сброс FIFO АЦП
	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_FIFORESET, NULL); // сброс FIFO SDRAM
	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_ENABLE, &Enable); // разрешение записи в SDRAM

	IPC_getTime(&start); // отметка начала сбора
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // разрешение работы АЦП

	BRDC_printf(_BRDC("ADC Start...     \r"));

	// дожидаемся окончания сбора
	BRD_WaitEvent waitEvt;
	waitEvt.timeout = 10000; // ждать окончания сбора данных в память до 10 сек.
	waitEvt.hAppEvent = g_hUserEvent;
	status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_WAITACQCOMPLETEEX, &waitEvt);
	IPC_getTime(&stop); // отметка окончания сбора
	if (BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
	{	// если вышли по тайм-ауту
		evt_status = status;
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFOSTATUS, &AdcStatus);
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_FIFOSTATUS, &Status);
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_ISACQCOMPLETE, &isAcqComplete);
		//BRDCHAR msg[255];
		BRDC_printf(_BRDC("BRDctrl_SDRAM_WAITACQCOMPLETE is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X SdramFifoStatus = %08X\n"),
			waitEvt.timeout / 1000, AdcStatus, Status);
		// получить реальное число собранных данных
		ULONG acq_size;
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_GETACQSIZE, &acq_size);
		unsigned __int64 bRealSize = (unsigned __int64)acq_size << 2; // запомнить в байтах
		BRDC_printf(_BRDC("    isAcqComplete=%d, DAQ real size = %d kByte\n"), isAcqComplete, (ULONG)(bRealSize / 1024));
	}
	evt_status = status;

	Enable = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // запрет работы АЦП
	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_ENABLE, &Enable); // запрет записи в SDRAM
	BRDC_printf(_BRDC("ADC Stop         \r"));

	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_IRQACQCOMPLETE, &Enable); // запрет прерывания от флага завершения сбора в SDRAM

	if (!BRD_errcmp(evt_status, BRDerr_OK))
	//if (BRD_errcmp(evt_status, BRDerr_WAIT_TIMEOUT))
		return evt_status; // выход по тайм-ауту

	// показываем скорость сбора данных
	double msTime = IPC_getDiffTime(&start, &stop);
	BRDC_printf(_BRDC("DAQ into board memory rate is %.2f Mbytes/sec\n"), ((double)g_bMemSize / msTime) / 1000.);

	// установить, что стрим работает с памятью
	ULONG tetrad;
	status = BRD_ctrl(hMem, 0, BRDctrl_SDRAM_GETSRCSTREAM, &tetrad);
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_SETSRC, &tetrad);

	// устанавливать флаг для формирования запроса ПДП надо после установки источника (тетрады) для работы стрима
	//	ULONG flag = BRDstrm_DRQ_ALMOST; // FIFO почти пустое
	//	ULONG flag = BRDstrm_DRQ_READY;
	ULONG flag = BRDstrm_DRQ_HALF; // рекомендуется флаг - FIFO наполовину заполнено
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_SETDRQ, &flag);

	BRD_ctrl(hADC, 0, BRDctrl_STREAM_RESETFIFO, NULL);

	evt_status = status;

	return status;
}

// создает событие и запускает тред
S32 StartDaqIntoSdramDMA(BRD_Handle hADC, BRD_Handle hMEM)
{
	S32		status = BRDerr_OK;
//	unsigned threadID;

	thread_par.hADC = hADC;
	thread_par.hMem = hMEM;
	g_hUserEvent = CreateEvent(
		NULL,   // default security attributes
		FALSE,  // auto-reset event object
		FALSE,  // initial state is nonsignaled
		NULL);  // unnamed object

	// Create thread
	g_hThread = IPC_createThread(_BRDC("DaqIntoSdram"), &ThreadDaqIntoSdramDMA, &thread_par);
	//g_hThread = (HANDLE)_beginthreadex(NULL, 0, &ThreadDaqIntoSdramDMA, &thread_par, 0, &threadID);
	return 1;
}

// проверяет завершение треда
S32 CheckDaqIntoSdramDMA()
{
	// check for terminate thread
	int ret = IPC_waitThread(g_hThread, 0);	// no wait
	if (ret == IPC_WAIT_TIMEOUT)
		return 0;
	//ULONG ret = WaitForSingleObject(g_hThread, 0);
	//if (ret == WAIT_TIMEOUT)
	//	return 0;
	return 1;
}

// прерывает исполнение треда, находящегося в ожидании завершения сбора данных в SDRAM
void BreakDaqIntoSdramDMA()
{
	SetEvent(g_hUserEvent); // установить в состояние Signaled
	IPC_waitThread(g_hThread, INFINITE);// Wait until threads terminates
	//WaitForSingleObject(g_hThread, INFINITE); // Wait until thread terminates
}

// Эта функция должна вызываться ТОЛЬКО когда тред уже закончил исполняться 
S32 EndDaqIntoSdramDMA()
{
	CloseHandle(g_hUserEvent);
	IPC_deleteThread(g_hThread);
	//CloseHandle(g_hThread);
	g_hUserEvent = NULL;
	g_hThread = NULL;
	return evt_status;
}

// получить данные из памяти
S32 DataFromMem(BRD_Handle hADC)
{
	S32		status;
	ULONG sdram_status = 0;
	BRDctrl_StreamCBufStart start_pars;
	start_pars.isCycle = 0; // без зацикливания 
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_START, &start_pars); // стартуем передачу данных из памяти устройства в ПК
	ULONG msTimeout = 5000; // ждать окончания передачи данных до 5 сек.
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_WAITBUF, &msTimeout);
	if (BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
	{	// если вышли по тайм-ауту, то остановимся
		status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STOP, NULL);

		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_FIFOSTATUS, &sdram_status);
		BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.):   SdramFifoStatus = %08X\n"),
			msTimeout / 1000, sdram_status);
	}
	return status;
}

//
// End of file
//
