//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   находящего указанную службу АЦП,
//   программирующего его параметрами из указанного файла,
//   выполняющего сбор данных методом DMA (служба стрим)
//
// (C) InSys, 2007-2015
//
//********************************************************

#include	"brd.h"
#include	"extn.h"
#include	"ctrladc.h"
#include	"ctrlstrm.h"
#include	"gipcy.h"

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV
#define		MAX_CHAN	32		// считаем, что каналов может быть не больше MAX_CHAN


BRDCHAR g_AdcSrvName[64] = _BRDC("FM412x500M0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM212x1G0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM816x250M0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X400M0"); // с номером службы

S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx);
S32 AllocDaqBuf(BRD_Handle hADC, PVOID* &pSig, unsigned long long* pbytesBufSize, int memType, int* pBlkNum);
S32 FreeDaqBuf(BRD_Handle hADC, ULONG blkNum);
S32 DaqIntoFifoDMA(BRD_Handle hADC);

BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM412x500M.ini");
//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM212x1G.ini");
//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_FM816x250M.ini");
//BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//ADC_214x400M.ini");

BRDctrl_StreamCBufAlloc g_buf_dscr; // описание буфера стрима
unsigned long long g_bBufSize; // размер собираемых данных (в байтах)
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
						int blk_num = 1;
						PVOID* pSig = NULL; // указатель на массив указателей на блоки памяти с сигналом
						g_bBufSize = 256 * 1024 * 1024; // собирать будем 256 Мбайта
						AllocDaqBuf(hADC, pSig, &g_bBufSize, g_MemType, &blk_num);
						DaqIntoFifoDMA(hADC);

						IPC_handle hfile = 0;
						hfile = IPC_openFile(_BRDC("data.bin"), IPC_CREATE_FILE | IPC_FILE_WRONLY);
						for(int iBlk = 0; iBlk < blk_num; iBlk++)
							IPC_writeFile(hfile, pSig[iBlk], g_buf_dscr.blkSize);
						IPC_closeFile(hfile);

						FreeDaqBuf(hADC, blk_num);
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

// выполнить сбор данных в FIFO с ПДП-методом передачи в ПК
S32 DaqIntoFifoDMA(BRD_Handle hADC)
{
	S32	status = 0;
	ULONG adc_status = 0;
	ULONG sdram_status = 0;
	S32	wait_status = 0;
	ULONG Enable = 1;

	// установить источник для работы стрима
	ULONG tetrad;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETSRCSTREAM, &tetrad); // стрим будет работать с АЦП
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_SETSRC, &tetrad);

	// устанавливать флаг для формирования запроса ПДП надо после установки источника (тетрады) для работы стрима
//	ULONG flag = BRDstrm_DRQ_ALMOST; // FIFO почти пустое
//	ULONG flag = BRDstrm_DRQ_READY;
	ULONG flag = BRDstrm_DRQ_HALF; // рекомендуется флаг - FIFO наполовину заполнено
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_SETDRQ, &flag);

	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFORESET, NULL); // сброс FIFO АЦП
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_RESETFIFO, NULL);

	BRDctrl_StreamCBufStart start_pars;
	start_pars.isCycle = 0; // без зацикливания 
	//start_pars.isCycle = 1; // с зацикливанием
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_START, &start_pars); // старт ПДП

	BRDC_printf(_BRDC("ADC Start...     \r"));
	IPC_TIMEVAL start;
	IPC_TIMEVAL stop;
	IPC_getTime(&start);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // разрешение работы АЦП

	ULONG msTimeout = 1000; // ждать окончания передачи данных до 1 сек. (внутри BRDctrl_STREAM_CBUF_WAITBUF)
	int i = 0;	// организуем цикл, чтобы иметь возможность прерваться
	for(i = 0; i < 20; i++)
	{
		wait_status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_WAITBUF, &msTimeout);
		if(BRD_errcmp(wait_status, BRDerr_OK))
			break;	// дождались окончания передачи данных
		if(IPC_kbhit())
		{
			int ch = IPC_getch();
			if(0x1B == ch) 
				break;	// прерываем ожидание по Esc
		}
	}
	IPC_getTime(&stop);

	if(BRD_errcmp(wait_status, BRDerr_WAIT_TIMEOUT))
	{	// если вышли по тайм-ауту, то остановимся
		status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
		BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X"),
														msTimeout*(i+1)/1000, adc_status);
	}

	//while(1) // при старте с зацикливанием
	//{
	//	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_WAITBLOCK, &msTimeout);
	//	if(BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
	//	{	// если вышли по тайм-ауту, то остановимся
	//		status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
	//		DisplayError(status, __FUNCTION__, _BRDC("TIME-OUT"));
	//		break;
	//	}
	//	BRDctrl_StreamCBufState buf_state;
	//	buf_state.timeout = 0;
	//	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STATE, &buf_state);
	//	printf("State Total Counter = %d\r", buf_state.blkNumTotal);
	//	//printf("Total Counter = %d\r", g_buf_dscr.pStub->totalCounter);
	//	if(GetAsyncKeyState(VK_ESCAPE))
	//	{
	//		status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
	//		printf("\n\n", buf_state.blkNumTotal);
	//		_getch();
	//		break; 
	//	}
	//}

	Enable = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // запрет работы АЦП

	if(BRD_errcmp(wait_status, BRDerr_OK))
	{
		BRDC_printf(_BRDC("ADC Stop         \r"));
		double msTime = IPC_getDiffTime(&start, &stop);
		printf("DAQ & Transfer by bus rate is %.2f Mbytes/sec\n", ((double)g_bBufSize / msTime)/1000.);

		//status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
		//printf("ADC status = 0x%X ", adc_status);

		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ISBITSOVERFLOW, &adc_status);
		if(adc_status)
			printf("ADC Bits OVERFLOW %X  ", adc_status);

		BRDC_printf(_BRDC("DAQ by DMA from FIFO is complete!!!\n"));
	}
	return status;
}


//
// End of file
//
