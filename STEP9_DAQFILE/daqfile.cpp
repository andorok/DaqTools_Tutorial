//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   находящего указанную службу АЦП,
//   программирующего его параметрами из указанного файла,
//   выполняющего сбор данных непосредственно в файл на диске.
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

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV
#define		MAX_CHAN	32		// считаем, что каналов может быть не больше MAX_CHAN


BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X1GTRF0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM412x500M0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("FM212x1G0"); // с номером службы
//BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X400M0"); // с номером службы

S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx);
void DirectFile(BRD_Handle hADC);

BRDCHAR g_iniFileName[FILENAME_MAX] = _BRDC("//adc.ini");

 //=************************* main *************************
//=********************************************************
int BRDC_main(int argc, BRDCHAR *argv[])
{
	S32		status;

	// чтобы все печати сразу выводились на экран
	fflush(stdout);
	setbuf(stdout, NULL);

	BRD_displayMode(BRDdm_VISIBLE | BRDdm_CONSOLE); // режим вывода информационных сообщений : отображать все уровни на консоле

	S32	DevNum;
	//status = BRD_initEx(BRDinit_AUTOINIT, "brd.ini", NULL, &DevNum); // инициализировать библиотеку - автоинициализация
	status = BRD_init(_BRDC("brd.ini"), &DevNum); // инициализировать библиотеку
	if (!BRD_errcmp(status, BRDerr_OK))
	{
		BRDC_printf(_BRDC("ERROR: BARDY Initialization = 0x%X\n"), status);
		BRDC_printf(_BRDC("Press any key for leaving program...\n"));
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
	for (iDev = 0; iDev < lidList.itemReal; iDev++)
	{
		BRDC_printf(_BRDC("\n"));

		BRD_getInfo(lidList.pLID[iDev], &info); // получить информацию об устройстве
		if (info.busType == BRDbus_ETHERNET)
			BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, IP %u.%u.%u.%u, Port %u, PID = %d.\n"),
				info.name, info.boardType >> 16, info.boardType & 0xff,
				(UCHAR)info.bus, (UCHAR)(info.bus >> 8), (UCHAR)(info.bus >> 16), (UCHAR)(info.bus >> 24),
				info.dev, info.pid);
		else
		{
			ULONG dev_id = info.boardType >> 16;
			if (dev_id == 0x53B1 || dev_id == 0x53B3) // FMC115cP or FMC117cP
				BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, Bus = %d, Dev = %d, G.adr = %d, Order = %d, PID = %d.\n"),
					info.name, info.boardType >> 16, info.boardType & 0xff, info.bus, info.dev,
					info.slot & 0xffff, info.pid >> 28, info.pid & 0xfffffff);
			else
				BRDC_printf(_BRDC("%s: DevID = 0x%x, RevID = 0x%x, Bus = %d, Dev = %d, Slot = %d, PID = %d.\n"),
					info.name, info.boardType >> 16, info.boardType & 0xff, info.bus, info.dev, info.slot, info.pid);
		}

		//		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_EXCLUSIVE, NULL); // открыть устройство в монопольном режиме
		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_SHARED, NULL); // открыть устройство в разделяемом режиме
		if (handle[iDev] > 0)
		{
			U32 ItemReal;
			// получить список служб
			BRD_ServList srvList[MAX_SRV];
			status = BRD_serviceList(handle[iDev], 0, srvList, MAX_SRV, &ItemReal);
			if (ItemReal <= MAX_SRV)
			{
				U32 iSrv;
				BRD_Handle hADC;
				for (U32 j = 0; j < ItemReal; j++)
				{
					BRDC_printf(_BRDC("Service %d: %s\n"), j, srvList[j].name);
				}
				for (iSrv = 0; iSrv < ItemReal; iSrv++)
				{
					if (!BRDC_stricmp(srvList[iSrv].name, g_AdcSrvName))
					{
						hADC = SetParamSrv(handle[iDev], &srvList[iSrv], iDev);
						break;
					}
				}
				if (iSrv == ItemReal)
					BRDC_printf(_BRDC("NO Service %s\n"), g_AdcSrvName);
				else
					if (hADC > 0)
					{
						DirectFile(hADC);
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

	// задать параметры работы АЦП из секции ini-файла
	BRDCHAR iniFilePath[MAX_PATH];
	BRDCHAR iniSectionName[MAX_PATH];
	IPC_getCurrentDir(iniFilePath, sizeof(iniFilePath) / sizeof(BRDCHAR));
	BRDC_strcat(iniFilePath, g_iniFileName);
	BRDC_sprintf(iniSectionName, _BRDC("device%d_%s"), idx, srvName);

	BRD_IniFile ini_file;
	BRDC_strcpy(ini_file.fileName, iniFilePath);
	BRDC_strcpy(ini_file.sectionName, iniSectionName);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_READINIFILE, &ini_file);

	// получить маску включенных каналов
	ULONG chan_mask = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCHANMASK, &chan_mask);
	if (BRD_errcmp(status, BRDerr_OK))
		BRDC_printf(_BRDC("BRDctrl_ADC_GETCHANMASK: chan_mask = %0X\n"), chan_mask);
	else
		BRDC_printf(_BRDC("BRDctrl_ADC_GETCHANMASK: Error!!!\n"));

	// проверяем наличие динамической памяти
	BRD_SdramCfgEx SdramConfig;
	SdramConfig.Size = sizeof(BRD_SdramCfgEx);
	ULONG PhysMemSize;
	status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_GETCFGEX, &SdramConfig);
	if (status < 0)
	{
		BRDC_printf(_BRDC("Get SDRAM Config: Error!!!\n"));
		return BRDerr_INSUFFICIENT_RESOURCES;
	}
	else
	{
		if (SdramConfig.MemType == 11) //DDR3
			PhysMemSize = (ULONG)((
				(((__int64)SdramConfig.CapacityMbits * 1024 * 1024) >> 3) *
				(__int64)SdramConfig.PrimWidth / SdramConfig.ChipWidth * SdramConfig.ModuleBanks *
				SdramConfig.ModuleCnt) >> 2); // в 32-битных словах
		else
			PhysMemSize = (1 << SdramConfig.RowAddrBits) *
			(1 << SdramConfig.ColAddrBits) *
			SdramConfig.ModuleBanks *
			SdramConfig.ChipBanks *
			SdramConfig.ModuleCnt * 2; // в 32-битных словах
	}
	if (PhysMemSize)
	{ // динамическая память присутствует на модуле
		BRDC_printf(_BRDC("SDRAM Config: Memory size = %d MBytes\n"), (PhysMemSize / (1024 * 1024)) * 4);

		// установить параметры SDRAM
		ULONG target = 2; // будем осуществлять сбор данных в память
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_SETTARGET, &target);

		ULONG fifo_mode = 1; // память используется как FIFO
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_SETFIFOMODE, &fifo_mode);

		BRDC_printf(_BRDC("SDRAM as a FIFO mode!!!\n"));
	}
	else
	{
		// освободить службу SDRAM (она могла быть захвачена командой BRDctrl_SDRAM_GETCFG, если та отработала без ошибки)
		ULONG mem_size = 0;
		status = BRD_ctrl(hADC, 0, BRDctrl_SDRAM_SETMEMSIZE, &mem_size);
		BRDC_printf(_BRDC("No SDRAM on board!!!\n"));
		status = BRDerr_INSUFFICIENT_RESOURCES;
	}

	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_PREPARESTART, NULL);
	if (status < 0)
		if (!(BRD_errcmp(status, BRDerr_CMD_UNSUPPORTED)
			|| BRD_errcmp(status, BRDerr_INSUFFICIENT_SERVICES)))
		{
			return -1;
		}

	return status;
}

// захватываем службу АЦП устанавливаем параметры АЦП
S32 SetParamSrv(BRD_Handle handle, BRD_ServList* srv, int idx)
{
	S32		status;
	U32 mode = BRDcapt_EXCLUSIVE;
	BRD_Handle hADC = BRD_capture(handle, 0, &mode, srv->name, 10000);
	if (mode == BRDcapt_EXCLUSIVE) BRDC_printf(_BRDC("%s is captured in EXCLUSIVE mode!\n"), srv->name);
	if (mode == BRDcapt_SPY)	BRDC_printf(_BRDC("%s is captured in SPY mode!\n"), srv->name);
	if (hADC > 0)
	{
		status = AdcSettings(hADC, idx, srv->name); // установить параметры АЦП
		if (status < 0)
			hADC = -1;
	}
	return hADC;
}

#define BLOCK_SIZE 4194304			// размер блока = 4 Mбайтa 
#define BLOCK_NUM 8					// число блоков = 8 

typedef struct _THREAD_PARAM {
	BRD_Handle handle;
	int idx;
} THREAD_PARAM, *PTHREAD_PARAM;

thread_value __IPC_API DirWriteIntoFile(void* pParams);
int SimpleProcWrDir(BRD_Handle hSrv, IPC_handle hfile, int idx, BRDctrl_StreamCBufAlloc*  buf_dscr);
int MultiBlkProcWrDir(BRD_Handle hSrv, IPC_handle hfile, int idx, BRDctrl_StreamCBufAlloc*  buf_dscr);

BRDCHAR g_dirFileName[MAX_PATH] = _BRDC("dirdata");

ULONG g_fileBlkNum = 64; // число блоков, записываемых в файл (если его умножить на BLOCK_SIZE, то будет размер файла)

static int g_flbreak = 0;

void DirectFile(BRD_Handle hADC)
{
	THREAD_PARAM thread_par;

	//SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	//DWORD prior_class = GetPriorityClass(GetCurrentProcess());
	//BRDC_printf(_BRDC("Process Priority = %d\n"), prior_class);

	g_flbreak = 0;
	thread_par.handle = hADC;
	thread_par.idx = 0;
	IPC_handle hThread = IPC_createThread(_BRDC("DirFile"), &DirWriteIntoFile, &thread_par);
	IPC_waitThread(hThread, INFINITE);// Wait until threads terminates
	IPC_deleteThread(hThread);
}

thread_value __IPC_API DirWriteIntoFile(void* pParams)
{
	S32		status = BRDerr_OK;
	ULONG adc_status = 0;
	ULONG sdram_status = 0;
	//SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	//int prior = GetThreadPriority(GetCurrentThread());
	//BRDC_printf(_BRDC("Thread Priority = %d\n"), prior);
	PTHREAD_PARAM pThreadParam = (PTHREAD_PARAM)pParams;
	BRD_Handle hSrv = pThreadParam->handle;
	int idx = pThreadParam->idx;

	BRDCHAR fileName[MAX_PATH];
	BRDC_sprintf(fileName, _BRDC("%s_%d.bin"), g_dirFileName, idx);

	IPC_handle hfile = IPC_openFileEx(fileName, IPC_CREATE_FILE | IPC_FILE_WRONLY, IPC_FILE_NOBUFFER);
	if (!hfile)
	{
		BRDC_printf(_BRDC("Create file error\n"));
		return (thread_value)status;
	}

	BRDctrl_StreamCBufAlloc buf_dscr;
	buf_dscr.dir = BRDstrm_DIR_IN;
	buf_dscr.isCont = 1; // 1 - буфер размещается в системной памяти ПК
	buf_dscr.blkNum = BLOCK_NUM;
	buf_dscr.blkSize = BLOCK_SIZE;
	buf_dscr.ppBlk = new PVOID[buf_dscr.blkNum];
	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_ALLOC, &buf_dscr);
	if (!BRD_errcmp(status, BRDerr_OK) && !BRD_errcmp(status, BRDerr_PARAMETER_CHANGED))
	{
		BRDC_printf(_BRDC("ERROR!!! BRDctrl_STREAM_CBUF_ALLOC\n"));
		return (thread_value)status;
	}
	if (BRD_errcmp(status, BRDerr_PARAMETER_CHANGED))
	{
		BRDC_printf(_BRDC("Warning!!! BRDctrl_STREAM_CBUF_ALLOC: BRDerr_PARAMETER_CHANGED\n"));
		status = BRDerr_OK;
	}
	BRDC_printf(_BRDC("Block size = %d Mbytes, Block num = %d\n"), buf_dscr.blkSize / 1024 / 1024, buf_dscr.blkNum);

	// установить источник для работы стрима
	ULONG tetrad;
	status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_GETSRCSTREAM, &tetrad); // стрим будет работать с SDRAM
	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_SETSRC, &tetrad);

	// устанавливать флаг для формирования запроса ПДП надо после установки источника (тетрады) для работы стрима
	//	ULONG flag = BRDstrm_DRQ_ALMOST; // FIFO почти пустое
	//	ULONG flag = BRDstrm_DRQ_READY;
	ULONG flag = BRDstrm_DRQ_HALF; // рекомендуется флаг - FIFO наполовину заполнено
	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_SETDRQ, &flag);

	ULONG Enable = 1;
	status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFORESET, NULL); // сброс FIFO АЦП
	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_RESETFIFO, NULL);
	status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_FIFORESET, NULL); // сборс FIFO SDRAM
	status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_ENABLE, &Enable); // разрешение записи в SDRAM

	BRDctrl_StreamCBufStart start_pars;
	start_pars.isCycle = 1; // с зацикливанием
	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_START, &start_pars); // старт ПДП

	status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_ENABLE, &Enable); // разрешение работы АЦП

	int errCnt = 0;
	BRDC_printf(_BRDC("Data writing into file %s...\n"), fileName);
	if (BLOCK_NUM == 2)
		errCnt = SimpleProcWrDir(hSrv, hfile, idx, &buf_dscr);
	else
		errCnt = MultiBlkProcWrDir(hSrv, hfile, idx, &buf_dscr);

	Enable = 0;
	status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_ENABLE, &Enable); // запрет работы АЦП
	status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_ENABLE, &Enable); // запрет записи в SDRAM

	printf("                                                                       \r");
	if(errCnt)
		BRDC_printf(_BRDC("ERROR (%s): buffers skiped %d\n"), fileName, errCnt);

	BRDC_printf(_BRDC("Total Buffer Counter (%s) = %d\n"), fileName, buf_dscr.pStub->totalCounter);

	status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
	if (adc_status & 0x80)
		BRDC_printf(_BRDC("\nERROR: ADC FIFO is overflow (ADC FIFO Status = 0x%04X)\n"), adc_status);
	status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_FIFOSTATUS, &sdram_status);
	if (sdram_status & 0x80)
		BRDC_printf(_BRDC("\nERROR: SDRAM FIFO is overflow (SDRAM FIFO Status = 0x%04X)\n"), sdram_status);

	status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_FREE, &buf_dscr);
	delete[] buf_dscr.ppBlk;

	return (thread_value)status;
}


int SimpleProcWrDir(BRD_Handle hSrv, IPC_handle hfile, int idx, BRDctrl_StreamCBufAlloc*  buf_dscr)
{
	S32		status = BRDerr_OK;
	ULONG Status = 0;
	ULONG curbuf = 0;
	int cnt = 0;
	int errcnt = 0;
	ULONG msTimeout = 20000; // ждать окончания передачи данных до 20 сек.
	//ULONG nNumberOfWriteBytes;
	for (ULONG i = 0; i < g_fileBlkNum; i++)
	{
		status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_WAITBLOCK, &msTimeout);
		if (BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
		{	// если вышли по тайм-ауту, то остановимся
			status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
			ULONG adc_status = 0, sdram_status = 0;
			status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
			status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_FIFOSTATUS, &sdram_status);
			BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X SdramFifoStatus = %08X"),
				msTimeout / 1000, adc_status, sdram_status);
			break;
		}
		if (IPC_kbhit())
		{
			int ch = IPC_getch(); // получает клавишу
			if (0x1B == ch) // если Esc
			{
				g_flbreak = 1;
				break;
			}
		}
		IPC_writeFile(hfile, buf_dscr->ppBlk[curbuf], buf_dscr->blkSize);
		curbuf ^= 1;
		if (cnt + 1 != (int)buf_dscr->pStub->totalCounter)
			errcnt++;
		//			printf("ERROR: buffer %d skip\n", cnt);
		cnt = buf_dscr->pStub->totalCounter;
		if (i % 4 == 0)
			BRDC_printf(_BRDC("Current buffer = %d\r"), cnt);
		//			printf("Current buffer = %d, MO = %d\r", cnt, mo);
	}
	return errcnt;
}

int MultiBlkProcWrDir(BRD_Handle hSrv, IPC_handle hfile, int idx, BRDctrl_StreamCBufAlloc*  buf_dscr)
{
	S32		status = BRDerr_OK;
	ULONG Status = 0;
	ULONG total_cnt = 0;
	LONG delta_cnt = 0;
	LONG twice = 1;
	LONG cur_buf = 0;
	ULONG write_cnt = 0;
	ULONG first_err = 0;
	int err_cnt = 0;
	int waitblk_cnt = 0;
	ULONG msTimeout = 40000; // ждать окончания передачи данных до 20 сек.
	//ULONG nNumberOfWriteBytes;
	do
	{
		total_cnt = buf_dscr->pStub->totalCounter;
		delta_cnt = total_cnt - write_cnt;
		//printf("Delta = %d, total %d\n", delta_cnt, total_cnt);
		if (delta_cnt > (S32)buf_dscr->blkNum)
		{
			if (!err_cnt)
				first_err = write_cnt;
			err_cnt++;
			delta_cnt = 0;
		}
		if (delta_cnt <= 0 || !(delta_cnt % buf_dscr->blkNum))
		{
			//printf("Waiting...\n");
			waitblk_cnt++;
			status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_WAITBLOCK, &msTimeout);
			if (BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
			{	// если вышли по тайм-ауту, то остановимся
				status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
				ULONG adc_status = 0, sdram_status = 0;
				status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
				status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_FIFOSTATUS, &sdram_status);
				BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X SdramFifoStatus = %08X"),
					msTimeout / 1000, adc_status, sdram_status);
				break;
			}
			if (twice)
			{
				status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_WAITBLOCK, &msTimeout);
				if (BRD_errcmp(status, BRDerr_WAIT_TIMEOUT))
				{	// если вышли по тайм-ауту, то остановимся
					status = BRD_ctrl(hSrv, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
					ULONG adc_status = 0, sdram_status = 0;
					status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
					status = BRD_ctrl(hSrv, 0, BRDctrl_SDRAM_FIFOSTATUS, &sdram_status);
					BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X SdramFifoStatus = %08X"),
						msTimeout / 1000, adc_status, sdram_status);
					break;
				}
			}
			delta_cnt = 1;
			twice = 0;
			//total_cnt++;
		}
		else
			twice = 1;
		if (IPC_kbhit())
		{
			int ch = IPC_getch(); // получает клавишу
			if (0x1B == ch) // если Esc
			{
				g_flbreak = 1;
				break;
			}
		}
		cur_buf = write_cnt % buf_dscr->blkNum;
		for (int j = 0; j < delta_cnt; j++)
		{
			status = BRD_ctrl(hSrv, 0, BRDctrl_ADC_FIFOSTATUS, &Status);
			if (Status & 0x80)
				BRDC_printf(_BRDC("ERROR: ADC FIFO is overflow (ADC FIFO Status = 0x%04X)\n"), Status);
			IPC_writeFile(hfile, buf_dscr->ppBlk[cur_buf], buf_dscr->blkSize);
			cur_buf++;
			if (cur_buf == buf_dscr->blkNum)
				cur_buf = 0;
			write_cnt++;
			if (write_cnt == g_fileBlkNum)
				break;
		}
		BRDC_printf(_BRDC("Delta = %ld, written buffers = %d, errors = %d\r"), delta_cnt, write_cnt, err_cnt);
	} while (write_cnt < g_fileBlkNum);
	return err_cnt;
}

//
// End of file
//
