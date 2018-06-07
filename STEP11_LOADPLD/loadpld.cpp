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

//#define		MAX_NAME	32		// считаем, что имя устройства/ПЛИС/службы может быть не более 32 символов 
//#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV

// открыть устройство
BRD_Handle DEV_open(BRDCHAR * inifile, int idev, int* numdev);
// загрузить файл в ПЛИС
int DEV_loadPLD(BRD_Handle hDEV, const BRDCHAR* pldFileName, BRDextn_PLDINFO* pld_info);
// закрыть устройство
int DEV_close(BRD_Handle hDEV);

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
	// загрузить файл в ПЛИС
	BRDextn_PLDINFO pld_info;
	BRDCHAR pldFileName[MAX_PATH] = _BRDC("ambpcx_v10_admtest2_s1500.mcs");
	DEV_loadPLD(hDev, pldFileName, &pld_info);

	// отобразить полученную информацию
	if (pld_info.version != 0xFFFF)
		BRDC_printf(_BRDC("ADM PLD: Version %d.%d, Modification %d, Build 0x%X\n"),
			pld_info.version >> 8, pld_info.version & 0xff, pld_info.modification, pld_info.build);

	//====================================================================================
	// закрыть устройство
	DEV_close(hDev);

	return 0;
}

//=************************* DEV_& ADC_ functions *************************

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
//#define		MAX_PU		8		// считаем, что PU-устройств (ПЛИС, ППЗУ) на одном модуле может быть не больше MAX_PU

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
	delete lidList.pLID;
	return handle;
}

// загрузить файл в ПЛИС
// hDEV - (IN) дескриптор открытого устройства
// pldFileName - (IN) имен файла для загрузки в ПЛИС
int DEV_loadPLD(BRD_Handle hDEV, const BRDCHAR* pldFileName, BRDextn_PLDINFO* pld_info)
{
	S32		status;
	U32	PldState = 1;

	BRDC_printf(_BRDC("BRD_puLoad: loading...\r"));
	status = BRD_puLoad(hDEV, 0x100, pldFileName, &PldState);
	if (BRD_errcmp(status, BRDerr_OK))
	{
		BRDC_printf(_BRDC("BRD_puLoad: load is OK\n"));
		pld_info->pldId = 0x100;
		status = BRD_extension(hDEV, 0, BRDextn_GET_PLDINFO, pld_info);
	}
	else
		if (BRD_errcmp(status, BRDerr_PLD_TEST_DATA_ERROR))
			BRDC_printf(_BRDC("BRD_puLoad: data error starting test\n"));
		else
			if (BRD_errcmp(status, BRDerr_PLD_TEST_ADDRESS_ERROR))
				BRDC_printf(_BRDC("BRD_puLoad: address error starting test\n"));
			else
				BRDC_printf(_BRDC("BRD_puLoad: error loading\n"));

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
