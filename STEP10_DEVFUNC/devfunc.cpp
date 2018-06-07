//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   программируемых устройствах на нем (ПЛИС, ППЗУ...)
//   и BARDY-службах (стримы, АЦП, ЦАП...)
//
// (C) InSys, 2007-2016
//
//********************************************************

#include	"brd.h"
#include	"extn.h"
#include	"ctrladc.h"
#include	"ctrlstrm.h"

#include	"gipcy.h"

#define		MAX_NAME	32		// считаем, что имя устройства/ПЛИС/службы может быть не более 32 символов 
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV

#pragma pack(push,1)

// информация об устройстве
typedef struct
{
	U32			size;			// sizeof(DEV_INFO)
	U16			devID;			// Device ID
	U16			rev;			// Revision
	BRDCHAR		devName[MAX_NAME];	// Device Name
	U32			pid;			// Board Phisical ID
	S32			bus;			// Bus Number
	S32			dev;			// Dev Number
	S32			slot;			// Slot Number
	U08			pwrOn;			// FMC power on/off
	U32			pwrValue;		// FMC power value
	U16			pldVer;			// ADM PLD version
	U16			pldMod;			// ADM PLD modification
	U16			pldBuild;		// ADM PLD build
	BRDCHAR		pldName[MAX_NAME];	// ADM PLD Name
	U16			subType;		// Subunit Type Code
	U16			subVer;			// Subunit version
	U32			subPID;			// Subunit serial number
	BRDCHAR		subName[MAX_NAME];	// Submodule Name
	BRDCHAR		srvName[MAX_NAME][MAX_SRV];	// massive of Service Names
} DEV_INFO, *PDEV_INFO;

// информация о рабочих параметрах АЦП
typedef struct
{
	U32		size;			// sizeof(ADC_PARAM)
	U32		chmask;			// mask of ON channels
	U32		clkSrc;			// clock source
	REAL64	clkValue;		// clock value
	REAL64	rate;			// sampling rate
} ADC_PARAM, *PADC_PARAM;

#pragma pack(pop)

BRDCHAR g_AdcSrvName[64] = _BRDC("ADC214X1GTRF0"); // с номером службы
//BRDCHAR g_AdcSrvName[MAX_NAME] = _BRDC("FM412x500M0"); // имя службы с номером 
//BRDCHAR g_AdcSrvName[MAX_NAME] = _BRDC("FM212x1G0"); // имя службы с номером 
//BRDCHAR g_AdcSrvName[MAX_NAME] = _BRDC("FM814x250M0"); // имя службы с номером 
//BRDCHAR g_AdcSrvName[MAX_NAME] = _BRDC("ADC214X400M0"); // имя службы с номером 
BRDctrl_StreamCBufAlloc g_buf_dscr; // описание буфера стрима

// открыть устройство
BRD_Handle DEV_open(BRDCHAR * inifile, int idev, int* numdev);
// получить информацию об открытом устройстве
int DEV_info(BRD_Handle hDEV, int idev, DEV_INFO* pdevcfg);
// закрыть устройство
int DEV_close(BRD_Handle hDEV);
// открыть АЦП и получить информацию о нем 
BRD_Handle ADC_open(BRD_Handle hDEV, BRDCHAR* adcsrv, BRD_AdcCfg* adcfg);
// установить рабочие параметры АЦП
int ADC_set(BRD_Handle hADC, int idev, BRDCHAR* adcsrv, BRDCHAR* inifile, ADC_PARAM* adcpar);
// размещение буфера для получения данных с АЦП через Стрим
S32 ADC_allocbuf(BRD_Handle hADC, U64* pbytesBufSize);
// выполнить сбор данных в FIFO с ПДП-методом передачи в ПК
int ADC_read(BRD_Handle hADC);
// освобождение буфера стрима
S32 ADC_freebuf(BRD_Handle hADC);
// закрыть АЦП
int ADC_close(BRD_Handle hADC);

// отобразить информацию об устройстве
void DisplayDeviceInfo(PDEV_INFO pDevInfo)
{
	if (pDevInfo->devID == 0x53B1 || pDevInfo->devID == 0x53B3) // FMC115cP or FMC117cP
	BRDC_printf(_BRDC("%s %x.%x (0x%x): Bus = %d, Dev = %d, G.adr = %d, Order = %d, PID = %d\n"),
		pDevInfo->devName, pDevInfo->rev >> 4, pDevInfo->rev & 0xf, pDevInfo->devID, pDevInfo->bus, pDevInfo->dev,
		pDevInfo->slot & 0xffff, pDevInfo->pid >> 28, pDevInfo->pid & 0xfffffff);
	else
	BRDC_printf(_BRDC("%s %x.%x (0x%x): Bus = %d, Dev = %d, Slot = %d, PID = %d\n"),
		pDevInfo->devName, pDevInfo->rev >> 4, pDevInfo->rev & 0xf, pDevInfo->devID, pDevInfo->bus, pDevInfo->dev, pDevInfo->slot, pDevInfo->pid);

	if (pDevInfo->pldVer != 0xFFFF)
	BRDC_printf(_BRDC("%s: Version %d.%d, Modification %d, Build 0x%X\n"),
		pDevInfo->pldName, pDevInfo->pldVer >> 8, pDevInfo->pldVer & 0xff, pDevInfo->pldMod, pDevInfo->pldBuild);

	if (pDevInfo->pwrOn && (pDevInfo->pwrOn != 0xFF))
	BRDC_printf(_BRDC("FMC Power: ON %.2f Volt\n"), pDevInfo->pwrValue / 100.);
	else
	BRDC_printf(_BRDC("FMC Power: OFF %.2f Volt\n"), pDevInfo->pwrValue / 100.);

	if (pDevInfo->subType != 0xFFFF)
	BRDC_printf(_BRDC("  Subm: %s %x.%x (%X), Serial Number = %d\n"),
		pDevInfo->subName, pDevInfo->subVer >> 4, pDevInfo->subVer & 0xf, pDevInfo->subType, pDevInfo->subPID);

	int j = 0;
	while (BRDC_strlen(pDevInfo->srvName[j]))
	{
		BRDC_printf(_BRDC("Service %d: %s\n"), j, pDevInfo->srvName[j]);
		j++;
	}
}

//=********************************* main ********************************************
//====================================================================================
int BRDC_main( int argc, BRDCHAR *argv[] )
{
	// чтобы все печати сразу выводились на экран
	fflush(stdout);
	setbuf(stdout, NULL);

	//====================================================================================
	// открыть устройство, описанное в указанном файле с указанным порядковым номером LID 
	// возвращает дескриптор устройства (также возвращает общее число устройств)
	int idev = 0;
	int num_dev = 0;
	BRD_Handle hDev = DEV_open(_BRDC("brd.ini"), idev, &num_dev);
	if(hDev == -1)
	{
		BRDC_printf(_BRDC("ERROR by BARDY Initialization\n"));
		BRDC_printf(_BRDC("Press any key for leaving program...\n"));
		return -1;
	}
	BRDC_printf(_BRDC("Number of devices = %d\n"), num_dev);

	//====================================================================================
	// получить информацию об открытом устройстве
	DEV_INFO dev_info;
	dev_info.size = sizeof(DEV_INFO);
	DEV_info(hDev, idev, &dev_info);

	// отобразить полученную информацию
	DisplayDeviceInfo(&dev_info);

	//====================================================================================
	// открыть АЦП и получить информацию о нем 
	BRD_AdcCfg adc_cfg;
	BRD_Handle hADC = ADC_open(hDev, g_AdcSrvName, &adc_cfg);
	if (hADC <= 0)
	{
		BRDC_printf(_BRDC("ERROR by ADC service (%s) capture\n"), g_AdcSrvName);
		DEV_close(hDev); // закрыть устройство
		return -1;
	}

	//====================================================================================
	// установить параметры работы АЦП
	ADC_PARAM adc_param;
	dev_info.size = sizeof(ADC_PARAM);
	ADC_set(hADC, idev, g_AdcSrvName, _BRDC("adc.ini"), &adc_param);

	BRDC_printf(_BRDC("\nADC Config: %d Bits, FIFO is %d kBytes, %d channels\n"), adc_cfg.Bits, adc_cfg.FifoSize / 1024, adc_cfg.NumChans);
	BRDC_printf(_BRDC("            Min rate = %d kHz, Max rate = %d MHz\n"), adc_cfg.MinRate / 1000, adc_cfg.MaxRate / 1000000);
	BRDC_printf(_BRDC("            Input range = %.3f V\n"), adc_cfg.InpRange / 1000.);

	BRDC_printf(_BRDC("\nADC channel mask = %0X\n"), adc_param.chmask);
	BRDC_printf(_BRDC("ADC clocking: source = %d, value = %.2f MHz, rate = %.3f kHz\n\n"),
		adc_param.clkSrc, adc_param.clkValue / 1000000, adc_param.rate / 1000);

	//====================================================================================
	U64 bBufSize = 256 * 1024 * 1024; // собирать будем 256 Мбайта
	g_buf_dscr.blkNum = 1;
	g_buf_dscr.isCont = 0;
	g_buf_dscr.ppBlk = NULL; // указатель на массив указателей на блоки памяти с сигналом
	// выделить буфер для сбора данных с АЦП
	S32 ret = ADC_allocbuf(hADC, &bBufSize);
	if(ret == -1)
		BRDC_printf(_BRDC("IPC_virtAlloc() by allocating of buffer is error!!!\n"));
	else
	{
		if (BRD_errcmp(ret, BRDerr_OK))
		{
			BRDC_printf(_BRDC("Allocated memory for Stream: Number of blocks = %d, Block size = %d kBytes\n"),
				g_buf_dscr.blkNum, g_buf_dscr.blkSize / 1024);

			//====================================================================================
			// выполнить сбор данных с АЦП
			BRDC_printf(_BRDC("ADC is starting...     \r"));
			ret = ADC_read(hADC);

			if (BRD_errcmp(ret, BRDerr_OK))
			{
				BRDC_printf(_BRDC("DAQ by DMA from FIFO is complete!!!\n"));
				// записать собранные данные в файл
				IPC_handle hfile = 0;
				hfile = IPC_openFile(_BRDC("data.bin"), IPC_CREATE_FILE | IPC_FILE_WRONLY);
				for (U32 iBlk = 0; iBlk < g_buf_dscr.blkNum; iBlk++)
					//IPC_writeFile(hfile, pSig[iBlk], g_buf_dscr.blkSize);
					IPC_writeFile(hfile, g_buf_dscr.ppBlk[iBlk], g_buf_dscr.blkSize);
				IPC_closeFile(hfile);
			}

			//====================================================================================
			// освободить буфер
			ADC_freebuf(hADC);
		}
		else
		{
			if (BRD_errcmp(ret, BRDerr_NOT_ENOUGH_MEMORY))
				BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_NOT_ENOUGH_MEMORY (0x%08X)\n"), ret);
			else
				if (BRD_errcmp(ret, BRDerr_BAD_PARAMETER))
					BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_BAD_PARAMETER (0x%08X)\n"), ret);
				else
					if (BRD_errcmp(ret, BRDerr_INSUFFICIENT_RESOURCES))
						BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_INSUFFICIENT_RESOURCES (0x%08X)\n"), ret);
					else
						if (BRD_errcmp(ret, BRDerr_CMD_UNSUPPORTED))
							BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: BRDerr_CMD_UNSUPPORTED (0x%08X)\n"), ret);
						else
							BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC ERROR: status = 0x%08X\n"), ret);
		}
	}

	//====================================================================================
	// закрыть АЦП
	ADC_close(hADC);
	// закрыть устройство
	DEV_close(hDev);

	return 0;
}

//=************************* DEV_& ADC_ functions *************************

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_PU		8		// считаем, что PU-устройств (ПЛИС, ППЗУ) на одном модуле может быть не больше MAX_PU

// открыть устройство
// inifile - (IN) файл инициализации c описанием нужного устройства
// iDev - (IN) порядковый номер LID в указанном файле c описанием нужного устройства
// pnumdev - (OUT) возвращает общее число устройств
// возвращает дескриптор устройства
BRD_Handle DEV_open(BRDCHAR* inifile, int iDev, S32* pnumdev)
{
	S32		status;

	BRD_displayMode(BRDdm_VISIBLE | BRDdm_CONSOLE); // режим вывода информационных сообщений : отображать все уровни на консоле

	//status = BRD_initEx(BRDinit_AUTOINIT, inifile, NULL, &DevNum); // инициализировать библиотеку - автоинициализация
	status = BRD_init(inifile, pnumdev); // инициализировать библиотеку
	if (!BRD_errcmp(status, BRDerr_OK))
		return -1;
	if(iDev >= *pnumdev)
		return -1;

	// получить список LID (каждая запись соответствует устройству)
	BRD_LidList lidList;
	lidList.item = MAX_DEV;
	lidList.pLID = new U32[MAX_DEV];
	status = BRD_lidList(lidList.pLID, lidList.item, &lidList.itemReal);

	BRD_Handle handle = BRD_open(lidList.pLID[iDev], BRDopen_SHARED, NULL); // открыть устройство в разделяемом режиме
//	if(handle > 0)
//	{
		// получить список служб
		//U32 ItemReal;
		//BRD_ServList srvList[MAX_SRV];
		//status = BRD_serviceList(handle, 0, srvList, MAX_SRV, &ItemReal);
		//if (ItemReal <= MAX_SRV)
		//{
		//	for (U32 j = 0; j < ItemReal; j++)
		//		BRDC_strcpy(g_srvName[j], srvList[j].name);
		//}
		//else
		//	BRDC_printf(_BRDC("BRD_serviceList: Real Items = %d (> 16 - ERROR!!!)\n"), ItemReal);
//	}
	delete lidList.pLID;
	return handle;
}

// вспомогательная функция для DEV_info
//=***************************************************************************************
void SubmodName(ULONG id, BRDCHAR * str)
{
	switch (id)
	{
	case 0x1010:    BRDC_strcpy(str, _BRDC("FM814x125M")); break;
	case 0x1012:    BRDC_strcpy(str, _BRDC("FM814x250M")); break;
	case 0x1020:    BRDC_strcpy(str, _BRDC("FM214x250M")); break;
	case 0x1030:    BRDC_strcpy(str, _BRDC("FM412x500M")); break;
	case 0x1040:    BRDC_strcpy(str, _BRDC("FM212x1G")); break;
	case 0x1050:    BRDC_strcpy(str, _BRDC("FM816x250M")); break;
	case 0x1051:    BRDC_strcpy(str, _BRDC("FM416x250M")); break;
	case 0x1052:    BRDC_strcpy(str, _BRDC("FM216x250MDA")); break;
	case 0x1053:    BRDC_strcpy(str, _BRDC("FM816x250M1")); break;
	case 0x10C0:    BRDC_strcpy(str, _BRDC("FM212x4GDA")); break;
	case 0x10C8:    BRDC_strcpy(str, _BRDC("FM214x1GTRF")); break;
	default: BRDC_strcpy(str, _BRDC("UNKNOW")); break;
	}
}

// получить информацию об открытом устройстве
// hDEV - (IN) дескриптор открытого устройства
// iDev - (IN) порядковый номер LID (в массиве лидов) c описанием нужного устройства
// pdevcfg - (OUT) заполняемая информацией об устройстве структура
// srv_name - (OUT) массив имен служб
//int DEV_info(BRD_Handle hDEV, int iDev, DEV_INFO* pdevcfg, BRDCHAR srv_name[][MAX_SRV])
int DEV_info(BRD_Handle hDEV, int iDev, DEV_INFO* pdevcfg)
{
	S32		status;

	BRD_Info	info;
	info.size = sizeof(info);

	// получить список LID (каждая запись соответствует устройству)
	BRD_LidList lidList;
	lidList.item = MAX_DEV;
	lidList.pLID = new U32[MAX_DEV];
	status = BRD_lidList(lidList.pLID, lidList.item, &lidList.itemReal);

	BRD_getInfo(lidList.pLID[iDev], &info); // получить информацию об устройстве
	pdevcfg->devID = info.boardType >> 16;
	pdevcfg->rev = info.boardType & 0xff;
	BRDC_strcpy(pdevcfg->devName, info.name);
	pdevcfg->pid = info.pid;
	pdevcfg->bus = info.bus;
	pdevcfg->dev = info.dev;
	pdevcfg->slot = info.slot;
	pdevcfg->subType = info.subunitType[0];

	BRDCHAR subName[MAX_NAME];
	BRDC_strcpy(pdevcfg->subName, _BRDC("NONE"));
	if(info.subunitType[0] != 0xffff)
	{
		SubmodName(info.subunitType[0], subName);
		BRDC_strcpy(pdevcfg->subName, subName);
	}

	BRDC_strcpy(pdevcfg->pldName, _BRDC("ADM PLD"));
	U32 ItemReal;
	BRD_PuList PuList[MAX_PU];
	status = BRD_puList(hDEV, PuList, MAX_PU, &ItemReal); // получить список программируемых устройств
	if (ItemReal <= MAX_PU)
	{
		for (U32 j = 0; j < ItemReal; j++)
		{
			U32	PldState;
			status = BRD_puState(hDEV, PuList[j].puId, &PldState); // получить состояние ПЛИС
			if (PuList[j].puId == 0x100 && PldState)
			{// если это ПЛИС ADM и она загружена
				BRDC_strcpy(pdevcfg->pldName, PuList[j].puDescription);
				pdevcfg->pldVer = 0xFFFF;
				BRDextn_PLDINFO pld_info;
				pld_info.pldId = 0x100;
				status = BRD_extension(hDEV, 0, BRDextn_GET_PLDINFO, &pld_info);
				if (BRD_errcmp(status, BRDerr_OK))
				{
					pdevcfg->pldVer = pld_info.version;
					pdevcfg->pldMod = pld_info.modification;
					pdevcfg->pldBuild = pld_info.build;
				}
			}
			if (PuList[j].puId == 0x03)
			{// если это ICR субмодуля
				if (PldState)
				{ // и оно прописано данными
					char subICR[14];
					status = BRD_puRead(hDEV, PuList[j].puId, 0, subICR, 14);
					U16 tagICR = *(U16*)(subICR);
					pdevcfg->subPID = *(U32*)(subICR + 7); // серийный номер субмодуля
					pdevcfg->subType = *(U16*)(subICR + 11);  // тип субмодуля
					pdevcfg->subVer = *(U08*)(subICR + 13);   // версия субмодуля
					SubmodName(pdevcfg->subType, subName);
					BRDC_strcpy(pdevcfg->subName, subName);
				}
			}
		}
	}
	delete lidList.pLID;

	// получаем состояние FMC-питания (если не FMC-модуль, то ошибка)
	pdevcfg->pwrOn = 0xFF;
	BRDextn_FMCPOWER power;
	power.slot = 0;
	status = BRD_extension(hDEV, 0, BRDextn_GET_FMCPOWER, &power);
	if (BRD_errcmp(status, BRDerr_OK))
	{
		pdevcfg->pwrOn = power.onOff;
		pdevcfg->pwrValue = power.value;
	}

	// получить список служб
	BRD_ServList srvList[MAX_SRV];
	status = BRD_serviceList(hDEV, 0, srvList, MAX_SRV, &ItemReal);
	if (ItemReal <= MAX_SRV)
	{
		U32 j = 0;
		for (j = 0; j < ItemReal; j++)
			BRDC_strcpy(pdevcfg->srvName[j], srvList[j].name);
		BRDC_strcpy(pdevcfg->srvName[j], _BRDC(""));
	}
	else
		BRDC_printf(_BRDC("BRD_serviceList: Real Items = %d (> 16 - ERROR!!!)\n"), ItemReal);

	return 0;
}

// закрыть устройство
int DEV_close(BRD_Handle hDEV)
{
	S32		status;
	status = BRD_close(hDEV); // закрыть устройство 
	status = BRD_cleanup();
	return 0;
}

// открыть АЦП и получить информацию о нем 
// hDEV - (IN) дескриптор открытого устройства
// adcsrv - (IN) имя службы АЦП
// adcfg - (OUT) заполняемая информацией об АЦП структура
// возвращает дескриптор службы АЦП
BRD_Handle ADC_open(BRD_Handle hDEV, BRDCHAR* adcsrv, BRD_AdcCfg* adcfg)
{
	S32		status;
	BRD_Handle hADC = -1;
	U32 mode = BRDcapt_EXCLUSIVE;
	hADC = BRD_capture(hDEV, 0, &mode, adcsrv, 10000);
	if (hADC > 0)
	{
		if (mode != BRDcapt_EXCLUSIVE)
			return -1;
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCFG, adcfg);
	}
	return hADC;
}
	
// установить рабочие параметры АЦП
// hADC - (IN) дескриптор службы АЦП
// iDev - (IN) порядковый номер устройства 
// adcsrv - (IN) имя службы АЦП
// inifile - (IN) файл инициализации c параметрами работы АЦП
// adcpar - (OUT) заполняемая рабочими параметрами АЦП структура
int ADC_set(BRD_Handle hADC, int iDev, BRDCHAR* adcsrv, BRDCHAR* inifile, ADC_PARAM* adcpar)
{
	S32		status;

	// задать параметры работы АЦП из секции ini-файла
	BRDCHAR iniFilePath[MAX_PATH];
	BRDCHAR iniSectionName[MAX_PATH];
	IPC_getCurrentDir(iniFilePath, sizeof(iniFilePath) / sizeof(BRDCHAR));
	BRDC_strcat(iniFilePath, _BRDC("//"));
	BRDC_strcat(iniFilePath, inifile);
	BRDC_sprintf(iniSectionName, _BRDC("device%d_%s"), iDev, adcsrv);

	BRD_IniFile ini_file;
	BRDC_strcpy(ini_file.fileName, iniFilePath);
	BRDC_strcpy(ini_file.sectionName, iniSectionName);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_READINIFILE, &ini_file);

	// получить источник и значение тактовой частоты, а также частоту дискретизации можно отдельной функцией
	BRD_SyncMode sync_mode;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETSYNCMODE, &sync_mode);
	if (!BRD_errcmp(status, BRDerr_OK))
		return -1;
	// получить маску включенных каналов
	ULONG chan_mask = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_GETCHANMASK, &chan_mask);
	if (!BRD_errcmp(status, BRDerr_OK))
		return -1;

	adcpar->chmask = chan_mask;
	adcpar->clkSrc = sync_mode.clkSrc;
	adcpar->clkValue = sync_mode.clkValue;
	adcpar->rate = sync_mode.rate;

	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_PREPARESTART, NULL);
	if (status < 0)
		if (!(BRD_errcmp(status, BRDerr_CMD_UNSUPPORTED)
			|| BRD_errcmp(status, BRDerr_INSUFFICIENT_SERVICES)))
		{
			return -1;
		}

	return 0;
}

// закрыть АЦП
int ADC_close(BRD_Handle hADC)
{
	S32		status;
	status = BRD_release(hADC, 0);
	return status;
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
//	pBlkNum - число блоков составного буфера (IN/OUT)
//	memType - тип памяти для данных (IN): 
//		0 - пользовательская память выделяется в драйвере (точнее, в DLL базового модуля)
//		1 - системная память выделяется драйвере 0-го кольца
//		2 - пользовательская память выделяется в приложении
//S32 ADC_allocbuf(BRD_Handle hADC, PVOID* &pSig, unsigned long long* pbytesBufSize, int* pBlkNum, int memType)
//S32 ADC_allocbuf(BRD_Handle hADC, PVOID* &pSig, U64* pbytesBufSize)
S32 ADC_allocbuf(BRD_Handle hADC, U64* pbytesBufSize)
{
	S32		status;

	unsigned long long bBufSize = *pbytesBufSize;
	ULONG bBlkSize;
	ULONG blkNum = 1;
	// определяем число блоков составного буфера
	if (bBufSize > MAX_BLOCK_SIZE)
	{
		do {
			blkNum <<= 1;
			bBufSize >>= 1;
		} while (bBufSize > MAX_BLOCK_SIZE);
	}
	bBlkSize = (ULONG)bBufSize;

	void** pBuffer = NULL;
	if (2 == g_buf_dscr.isCont)
	{
		pBuffer = new PVOID[blkNum];
		for (ULONG i = 0; i < blkNum; i++)
		{
			pBuffer[i] = IPC_virtAlloc(bBlkSize);
			if (!pBuffer[i])
				return -1; // error
		}
	}
	g_buf_dscr.dir = BRDstrm_DIR_IN;
	//g_buf_dscr.isCont = memType;
	g_buf_dscr.blkNum = blkNum;
	g_buf_dscr.blkSize = bBlkSize;//*pbytesBufSize;
	g_buf_dscr.ppBlk = new PVOID[g_buf_dscr.blkNum];
	if (g_buf_dscr.isCont == 2)
	{
		for (ULONG i = 0; i < blkNum; i++)
			g_buf_dscr.ppBlk[i] = pBuffer[i];
		delete[] pBuffer;
	}
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_ALLOC, &g_buf_dscr);
	if (BRD_errcmp(status, BRDerr_PARAMETER_CHANGED))
	{ // может быть выделено меньшее количество памяти
		BRDC_printf(_BRDC("Warning!!! BRDctrl_STREAM_CBUF_ALLOC: BRDerr_PARAMETER_CHANGED\n"));
		status = BRDerr_OK;
	}
	else
	{
		if (BRD_errcmp(status, BRDerr_OK))
		{
			//BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_ALLOC SUCCESS: status = 0x%08X\n"), status);
		}
		else
		{ // при выделении памяти произошла ошибка
			if (2 == g_buf_dscr.isCont)
			{
				for (ULONG i = 0; i < blkNum; i++)
					IPC_virtFree(g_buf_dscr.ppBlk[i]);
			}
			delete[] g_buf_dscr.ppBlk;
			return status;
		}
	}
	//pSig = new PVOID[blkNum];
	//for (ULONG i = 0; i < blkNum; i++)
	//{
	//	pSig[i] = g_buf_dscr.ppBlk[i];
	//}
	*pbytesBufSize = (unsigned long long)g_buf_dscr.blkSize * blkNum;
	//*pBlkNum = blkNum;
	return status;
}

// освобождение буфера стрима
S32 ADC_freebuf(BRD_Handle hADC)
{
	S32		status;
	status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_FREE, NULL);
	if (g_buf_dscr.isCont == 2)
	{
		for (ULONG i = 0; i < g_buf_dscr.blkNum; i++)
			IPC_virtFree(g_buf_dscr.ppBlk[i]);
	}
	delete[] g_buf_dscr.ppBlk;
	return status;
}

// выполнить сбор данных в FIFO с ПДП-методом передачи в ПК
S32 ADC_read(BRD_Handle hADC)
{
	S32	status = 0;
	ULONG adc_status = 0;
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

	IPC_TIMEVAL start;
	IPC_TIMEVAL stop;
	IPC_getTime(&start);
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // разрешение работы АЦП

	ULONG msTimeout = 1000; // ждать окончания передачи данных до 1 сек. (внутри BRDctrl_STREAM_CBUF_WAITBUF)
	int i = 0;	// организуем цикл, чтобы иметь возможность прерваться
	for (i = 0; i < 20; i++)
	{
		wait_status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_WAITBUF, &msTimeout);
		if (BRD_errcmp(wait_status, BRDerr_OK))
			break;	// дождались окончания передачи данных
		if (IPC_kbhit())
		{
			int ch = IPC_getch();
			if (0x1B == ch)
				break;	// прерываем ожидание по Esc
		}
	}
	IPC_getTime(&stop);

	if (BRD_errcmp(wait_status, BRDerr_WAIT_TIMEOUT))
	{	// если вышли по тайм-ауту, то остановимся
		status = BRD_ctrl(hADC, 0, BRDctrl_STREAM_CBUF_STOP, NULL);
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_FIFOSTATUS, &adc_status);
		BRDC_printf(_BRDC("BRDctrl_STREAM_CBUF_WAITBUF is TIME-OUT(%d sec.)\n    AdcFifoStatus = %08X"),
								msTimeout*(i + 1) / 1000, adc_status);
	}

	Enable = 0;
	status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ENABLE, &Enable); // запрет работы АЦП

	if (BRD_errcmp(wait_status, BRDerr_OK))
	{
		double msTime = IPC_getDiffTime(&start, &stop);
		U64 bBufSize = g_buf_dscr.blkSize * g_buf_dscr.blkNum;
		printf("DAQ & Transfer by bus rate is %.2f Mbytes/sec\n", ((double)bBufSize / msTime) / 1000.);

		// проверить переполнение разрядной сетки АЦП
		status = BRD_ctrl(hADC, 0, BRDctrl_ADC_ISBITSOVERFLOW, &adc_status);
		if (adc_status)
			printf("ADC Bits OVERFLOW %X  ", adc_status);

	}
	return wait_status;
}

//
// End of file
//
