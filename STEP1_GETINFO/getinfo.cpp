//********************************************************
//
// Пример приложения, 
//   получающего информацию о модуле, 
//   программируемых устройствах на нем (ПЛИС, ППЗУ...)
//   и BARDY-службах (стримы, АЦП, ЦАП...)
//
// (C) InSys, 2007-2015
//
//********************************************************

#include	"brd.h"
#include	"extn.h"

#define		MAX_DEV		12		// считаем, что модулей может быть не больше MAX_DEV
#define		MAX_PU		8		// считаем, что PU-устройств (ПЛИС, ППЗУ) на одном модуле может быть не больше MAX_PU
#define		MAX_SRV		16		// считаем, что служб на одном модуле может быть не больше MAX_SRV

//***************************************************************************************
void SubmodName(ULONG id, BRDCHAR * str)
{
	switch(id)
	{
		case 0x1010:    BRDC_strcpy(str, _BRDC("FM814x125M")); break;
		case 0x1012:    BRDC_strcpy(str, _BRDC("FM814x250M")); break;
		case 0x1020:    BRDC_strcpy(str, _BRDC("FM214x250M")); break;
		case 0x1030:    BRDC_strcpy(str, _BRDC("FM412x500M")); break;
		case 0x1040:    BRDC_strcpy(str, _BRDC("FM212x1G")); break;
		default: BRDC_strcpy(str, _BRDC("UNKNOW")); break;
	}
}

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
		return -1;
	}
	BRDC_printf(_BRDC("BRD_init: OK. Number of devices = %d\n"), DevNum);

	// получить список LID (каждая запись соответствует устройству)
	BRD_LidList lidList;
	lidList.item = MAX_DEV;
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
		BRDCHAR subName[32];
		if(info.subunitType[0] != 0xffff)
		{
			SubmodName(info.subunitType[0], subName);
			BRDC_printf(_BRDC("     Subm: %s (%X).\n"), subName, info.subunitType[0]);
		}

//		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_EXCLUSIVE, NULL); // открыть устройство в монопольном режиме
		handle[iDev] = BRD_open(lidList.pLID[iDev], BRDopen_SHARED, NULL); // открыть устройство в разделяемом режиме
		if(handle[iDev] > 0)
		{
			U32 ItemReal;
			BRD_PuList PuList[MAX_PU];
			status = BRD_puList(handle[iDev], PuList, MAX_PU, &ItemReal); // получить список программируемых устройств
			if(ItemReal <= MAX_PU)
			{
				for(U32 j = 0; j < ItemReal; j++)
				{
					U32	PldState;
					status = BRD_puState(handle[iDev], PuList[j].puId, &PldState); // получить состояние ПЛИС
					BRDC_printf(_BRDC("PU%d: %s, Id=%d, Code=%X, Attr=%X, PldState=%d\n"),
									j, PuList[j].puDescription, PuList[j].puId, PuList[j].puCode, PuList[j].puAttr, PldState);
					if(PuList[j].puId == 0x100 && PldState)
					{// если это ПЛИС ADM и она загружена
						BRDextn_PLDINFO pld_info;
						pld_info.pldId = 0x100;
						status = BRD_extension(handle[iDev], 0, BRDextn_GET_PLDINFO, &pld_info);
						if(BRD_errcmp(status, BRDerr_OK))
							BRDC_printf(_BRDC("     ADM PLD: Version %d.%d, Modification %d, Build 0x%X\n"),
									pld_info.version >> 8, pld_info.version & 0xff, pld_info.modification, pld_info.build);
					}
					if(PuList[j].puId == 0x03)
					{// если это ICR субмодуля
						if(PldState)
						{ // и оно прописано данными
							char subICR[14];
							status = BRD_puRead(handle[iDev], PuList[j].puId, 0, subICR, 14);
							U16 tagICR = *(U16*)(subICR);
							U32 serialNum = *(U32*)(subICR+7); // серийный номер субмодуля
							U16 subType = *(U16*)(subICR+11);  // тип субмодуля
							U08 subVer = *(U08*)(subICR+13);   // версия субмодуля
							BRDCHAR subName[32];
							SubmodName(subType, subName);
							BRDC_printf(_BRDC("     SubmICR: Serial Number = %d, Type = %s(%x), Version = %x.%x.\n"), 
									serialNum, subName, subType, subVer >> 4, subVer & 0xf);
						}
						//else
						//{
						//	BRDC_printf(_BRDC("Submodule is absent or ICR of submodule is empty.\n"));
						//}
					}
				}
			}

			// получаем состояние FMC-питания (если не FMC-модуль, то ошибка)
			BRDextn_FMCPOWER power;
			power.slot = 0;
			status = BRD_extension(handle[iDev], 0, BRDextn_GET_FMCPOWER, &power);
			if(BRD_errcmp(status, BRDerr_OK))
			{
				if(power.onOff)
					BRDC_printf(_BRDC("FMC Power: ON %.2f Volt\n"), power.value/100.);
				else
					BRDC_printf(_BRDC("FMC Power: OFF %.2f Volt\n"), power.value/100.);
/*
				// отключаем, а потом включаем, FMC-питание
				power.onOff = 0;
				status = BRD_extension(handle[iDev], 0, BRDextn_SET_FMCPOWER, &power);
				status = BRD_extension(handle[iDev], 0, BRDextn_GET_FMCPOWER, &power);
				if(power.onOff)
					BRDC_printf(_BRDC("FMC Power: ON %.2f Volt\n"), power.value/100.);
				else
					BRDC_printf(_BRDC("FMC Power: OFF %.2f Volt\n"), power.value/100.);

				power.onOff = 1;
				status = BRD_extension(handle[iDev], 0, BRDextn_SET_FMCPOWER, &power);
				status = BRD_extension(handle[iDev], 0, BRDextn_GET_FMCPOWER, &power);
				if(power.onOff)
					BRDC_printf(_BRDC("FMC Power: ON %.2f Volt\n"), power.value/100.);
				else
					BRDC_printf(_BRDC("FMC Power: OFF %.2f Volt\n"), power.value/100.);
*/
			}

			// получить список служб
			BRD_ServList srvList[MAX_SRV];
			status = BRD_serviceList(handle[iDev], 0, srvList, MAX_SRV, &ItemReal);
			if(ItemReal <= MAX_SRV)
			{
				for(U32 j = 0; j < ItemReal; j++)
				{
					BRDC_printf(_BRDC("Service %d: %s\n"), j, srvList[j].name);
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

//
// End of file
//
