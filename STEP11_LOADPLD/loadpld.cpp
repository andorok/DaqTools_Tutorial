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

//#include	"gipcy.h"

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

#pragma pack(pop)

// открыть устройство
BRD_Handle DEV_open(BRDCHAR * inifile, int idev, int* numdev);
// получить информацию об открытом устройстве
int DEV_info(BRD_Handle hDEV, int idev, DEV_INFO* pdevcfg);
// закрыть устройство
int DEV_close(BRD_Handle hDEV);

// отобразить информацию об устройстве
void DisplayDeviceInfo(PDEV_INFO pDevInfo)
{
	BRDC_printf(_BRDC("%s %x.%x (0x%x): Bus = %d, Dev = %d, Slot = %d, PID = %d\n"),
		pDevInfo->devName, pDevInfo->rev >> 4, pDevInfo->rev & 0xf, pDevInfo->devID, pDevInfo->bus, pDevInfo->dev, pDevInfo->slot, pDevInfo->pid);

	if (pDevInfo->pldVer != 0xFFFF)
	BRDC_printf(_BRDC("%s: Version %d.%d, Modification %d, Build 0x%X\n"),
		pDevInfo->pldName, pDevInfo->pldVer >> 8, pDevInfo->pldVer & 0xff, pDevInfo->pldMod, pDevInfo->pldBuild);

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

// вспомогательные функции для DEV_info
//=***************************************************************************************
void SubmodName(ULONG id, BRDCHAR * str)
{
	switch (id)
	{
	case 0x0250:    BRDC_strcpy(str, _BRDC("ADM212x200M")); break;
	case 0x0460:    BRDC_strcpy(str, _BRDC("ADM214x200M")); break;
	case 0x0450:    BRDC_strcpy(str, _BRDC("ADM414x65M")); break;
	case 0x0640:    BRDC_strcpy(str, _BRDC("ADM216x100M")); break;
	default: BRDC_strcpy(str, _BRDC("UNKNOW")); break;
	}
}

void PldFileName(ULONG devId, ULONG subId, BRDCHAR * str)
{
	BRDC_strcpy(str, _BRDC(""));
	if(devId == 0x4D58) // AMBPCX
	{
		switch (subId)
		{
		case 0x0250:    BRDC_strcpy(str, _BRDC("ambpcx_v10_adm212x200m.mcs")); break;
		case 0x0450:    BRDC_strcpy(str, _BRDC("ambpcx_v10_adm414x60m.mcs")); break;
		case 0x0640:    BRDC_strcpy(str, _BRDC("ambpcx_v10_adm216x100m.mcs")); break;
		}
	}
	if (devId == 0x4D44) // AMBPCD
	{
		switch (subId)
		{
		case 0x0250:    BRDC_strcpy(str, _BRDC("ambpcd_v10_adm212x200m.mcs")); break;
		case 0x0450:    BRDC_strcpy(str, _BRDC("ambpcd_v10_adm414x60m.mcs")); break;
		case 0x0460:    BRDC_strcpy(str, _BRDC("ambpcd_v10_adm214x200m.mcs")); break;
		case 0x0640:    BRDC_strcpy(str, _BRDC("ambpcd_v10_adm216x100m.mcs")); break;
		}
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
	U32	PldState = 1;
	status = BRD_puList(hDEV, PuList, MAX_PU, &ItemReal); // получить список программируемых устройств
	if (ItemReal <= MAX_PU)
	{
		for (U32 j = 0; j < ItemReal; j++)
		{
			status = BRD_puState(hDEV, PuList[j].puId, &PldState); // получить состояние ПЛИС
			if (PuList[j].puId == 0x100)
			{// если это ПЛИС ADM
				if (!PldState)
				{// если ПЛИС ADM НЕ загружена, то пытаемся загрузить
					BRDCHAR pldFileName[MAX_PATH];
					PldFileName(pdevcfg->devID, info.subunitType[0], pldFileName);
					if (BRDC_strlen(pldFileName))
					{
						BRDC_printf(_BRDC("BRD_puLoad: loading...\r"));
						status = BRD_puLoad(hDEV, PuList[j].puId, pldFileName, &PldState);
						if (BRD_errcmp(status, BRDerr_OK))
							BRDC_printf(_BRDC("BRD_puLoad: load is OK\n"));
						else
							if (BRD_errcmp(status, BRDerr_PLD_TEST_DATA_ERROR))
								BRDC_printf(_BRDC("BRD_puLoad: data error starting test\n"));
							else
								if (BRD_errcmp(status, BRDerr_PLD_TEST_ADDRESS_ERROR))
									BRDC_printf(_BRDC("BRD_puLoad: address error starting test\n"));
								else
									BRDC_printf(_BRDC("BRD_puLoad: error loading\n"));
					}
				}
				if (PldState)
				{// если ПЛИС ADM загружена
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
				else
				{
					// если это ПЛИС ADM и она НЕ загружена
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

	if(PldState)
	{ // если ПЛИС загружена (если НЕ загружена, то любое обращение к тетрадам приведет к зависанию компьютера)

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
	}
	else
		BRDC_strcpy(pdevcfg->srvName[0], _BRDC(""));

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

//
// End of file
//
