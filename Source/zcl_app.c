
#include "AF.h"
#include "OSAL.h"
#include "OSAL_Clock.h"
#include "OSAL_PwrMgr.h"
#include "ZComDef.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "math.h"

#include "nwk_util.h"
#include "zcl.h"
#include "zcl_app.h"
#include "zcl_diagnostic.h"
#include "zcl_general.h"
#include "zcl_ms.h"

#include "bdb.h"
#include "bdb_interface.h"
#include "bdb_touchlink.h"
#include "bdb_touchlink_target.h"

#include "gp_interface.h"

#include "Debug.h"

#include "OnBoard.h"

#include "commissioning.h"
#include "factory_reset.h"
/* HAL */
#include "bme280.h"
#include "ds18b20.h"
#include "hal_drivers.h"
#include "hal_i2c.h"
#include "hal_key.h"
#include "hal_led.h"
#include "mhz19.h"
#include "senseair.h"
#include "utils.h"
#include "version.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
byte zclApp_TaskID;

static uint8 temp_sensor_type = EBME280; //ENOTFOUND
static bool abc_state = true;
static bool sensor_init = false;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
void user_delay_ms(uint32_t period);
void user_delay_ms(uint32_t period) { MicroWait(period * 1000); }

/*********************************************************************
 * LOCAL VARIABLES
 */

struct bme280_dev bme_dev = {.dev_id = BME280_I2C_ADDR_PRIM, .intf = BME280_I2C_INTF, .read = I2C_ReadMultByte, .write = I2C_WriteMultByte, .delay_ms = user_delay_ms};

static zclAirSensor_t const *air_dev = &sense_air_dev;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void zclApp_Report(void);
static void zclApp_BasicResetCB(void);
static void zclApp_RestoreAttributesFromNV(void);
static void zclApp_SaveAttributesToNV(void);
static void zclApp_ReadSensors(void);
static void zclApp_ReadBMEDS18B20(void);
static void zclApp_PerformABC(void);
static void zclApp_HandleKeys(byte portAndAction, byte keyCode);
static uint8 zclApp_RequestBME280(struct bme280_dev *dev);
static uint8 zclApp_ReadBME280(struct bme280_dev *dev);
static void zclApp_InitCO2Uart(void);
static ZStatus_t zclApp_ReadWriteAuthCB(afAddrType_t *srcAddr, zclAttrRec_t *pAttr, uint8 oper);
static void UartProcessData (uint8 port, uint8 event);
static void ProcessUartResults(uint8 *, uint8 count);


/*********************************************************************
 * ZCL General Profile Callback table
 */
static zclGeneral_AppCallbacks_t zclApp_CmdCallbacks = {
    zclApp_BasicResetCB, // Basic Cluster Reset command
    NULL,                // Identify Trigger Effect command
    NULL,                // On/Off cluster commands
    NULL,                // On/Off cluster enhanced command Off with Effect
    NULL,                // On/Off cluster enhanced command On with Recall Global Scene
    NULL,                // On/Off cluster enhanced command On with Timed Off
    NULL,                // RSSI Location command
    NULL                 // RSSI Location Response command
};

void zclApp_Init(byte task_id) {
    HalLedSet(HAL_LED_ALL, HAL_LED_MODE_BLINK);

    zclApp_RestoreAttributesFromNV();
    zclApp_InitCO2Uart();
    zclApp_TaskID = task_id;

    bdb_RegisterSimpleDescriptor(&zclApp_FirstEP);

    zclGeneral_RegisterCmdCallbacks(zclApp_FirstEP.EndPoint, &zclApp_CmdCallbacks);

    zcl_registerAttrList(zclApp_FirstEP.EndPoint, zclApp_AttrsCount, zclApp_AttrsFirstEP);

    zcl_registerReadWriteCB(zclApp_FirstEP.EndPoint, NULL, zclApp_ReadWriteAuthCB);
    zcl_registerForMsg(zclApp_TaskID);
    RegisterForKeys(zclApp_TaskID);

    LREP("Build %s \r\n", zclApp_DateCodeNT);

    HalI2CInit();
    
    //osal_start_timerEx(zclApp_TaskID, APP_REPORT_EVT, APP_REPORT_DELAY);
    osal_start_reload_timer(zclApp_TaskID, APP_REPORT_EVT, APP_REPORT_DELAY);
}

static void zclApp_HandleKeys(byte portAndAction, byte keyCode) {
    LREP("zclApp_HandleKeys portAndAction=0x%X keyCode=0x%X\r\n", portAndAction, keyCode);
    zclFactoryResetter_HandleKeys(portAndAction, keyCode);
    zclCommissioning_HandleKeys(portAndAction, keyCode);
    if (portAndAction & HAL_KEY_PRESS) {
        LREPMaster("Key press\r\n");
        zclApp_Report();
    }
}

static void zclApp_InitCO2Uart(void) {
    halUARTCfg_t halUARTConfig;
    halUARTConfig.configured = TRUE;
    halUARTConfig.baudRate = HAL_UART_BR_9600;
    halUARTConfig.flowControl = FALSE;
    halUARTConfig.flowControlThreshold = 48; // this parameter indicates number of bytes left before Rx Buffer
                                             // reaches maxRxBufSize
    halUARTConfig.idleTimeout = 10;          // this parameter indicates rx timeout period in millisecond
    halUARTConfig.rx.maxBufSize = 15;
    halUARTConfig.tx.maxBufSize = 15;
    halUARTConfig.intEnable = TRUE;
    halUARTConfig.callBackFunc = UartProcessData;
    HalUARTInit();
    if (HalUARTOpen(CO2_UART_PORT, &halUARTConfig) == HAL_UART_SUCCESS) {
        LREPMaster("Initialized CO2 UART \r\n");
    }
}

uint16 zclApp_event_loop(uint8 task_id, uint16 events) {
    LREP("events 0x%x \r\n", events);
    if (events & SYS_EVENT_MSG) {
        afIncomingMSGPacket_t *MSGpkt;
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(zclApp_TaskID))) {
            LREP("MSGpkt->hdr.event 0x%X clusterId=0x%X\r\n", MSGpkt->hdr.event, MSGpkt->clusterId);
            switch (MSGpkt->hdr.event) {
            case KEY_CHANGE:
                zclApp_HandleKeys(((keyChange_t *)MSGpkt)->state, ((keyChange_t *)MSGpkt)->keys);
                break;

            case ZCL_INCOMING_MSG:
                if (((zclIncomingMsg_t *)MSGpkt)->attrCmd) {
                    osal_mem_free(((zclIncomingMsg_t *)MSGpkt)->attrCmd);
                }
                break;

            default:
                break;
            }

            // Release the memory
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }
    if (events & APP_REPORT_EVT) {
        LREPMaster("APP_REPORT_EVT\r\n");
        zclApp_Report();
        return (events ^ APP_REPORT_EVT);
    }

    if (events & APP_SAVE_ATTRS_EVT) {
        LREPMaster("APP_SAVE_ATTRS_EVT\r\n");
        zclApp_SaveAttributesToNV();
        return (events ^ APP_SAVE_ATTRS_EVT);
    }
    if (events & APP_READ_SENSORS_EVT) {
        LREPMaster("APP_READ_SENSORS_EVT\r\n");
        zclApp_ReadSensors();
        return (events ^ APP_READ_SENSORS_EVT);
    }
        
    return 0;
}

static void zclApp_LedFeedback(void) {
    if (zclApp_Config.LedFeedback) {
        if (zclApp_Sensors.CO2_PPM > zclApp_Config.Threshold2_PPM) {
            HalLedSet(HAL_LED_3, HAL_LED_MODE_FLASH);
            HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
        } else if (zclApp_Sensors.CO2_PPM > zclApp_Config.Threshold1_PPM) {
            HalLedSet(HAL_LED_2, HAL_LED_MODE_FLASH);
            HalLedSet(HAL_LED_3, HAL_LED_MODE_OFF);
        }
    } else {
        HalLedSet(HAL_LED_2 | HAL_LED_3, HAL_LED_MODE_OFF);
    }
}

static void zclApp_PerformABC(void) {
    LREPMaster("PerformABC \r\n");
    if(abc_state != zclApp_Config.EnableABC){
        LREPMaster("Setup ABC \r\n");
        (*air_dev->SetABC)(zclApp_Config.EnableABC);
        abc_state = zclApp_Config.EnableABC;    
    }
}

static void zclApp_ReadBMEDS18B20(void) {

    int16 temp;

    if (temp_sensor_type == EBME280) {
        temp_sensor_type = (zclApp_RequestBME280(&bme_dev) == 0) ? EBME280 : EDS18B20;
        }
    if (temp_sensor_type == EBME280) {
        zclApp_ReadBME280(&bme_dev);
        }
    if (temp_sensor_type == EDS18B20) {
        temp = readTemperature();
        if (temp == 1) {
            temp_sensor_type = ENOTFOUND;
            LREPMaster("ReadDS18B20 error\r\n");
          } else {
                zclApp_Sensors.Temperature = temp + zclApp_Config.TemperatureOffset;
                LREP("ReadDS18B20 t=%d offset=\r\n", zclApp_Sensors.Temperature, zclApp_Config.TemperatureOffset);
            }
    
        }
    if (temp_sensor_type == EBME280) {
            bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
            bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, PRESSURE, ATTRID_MS_PRESSURE_MEASUREMENT_MEASURED_VALUE);
            bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, HUMIDITY, ATTRID_MS_RELATIVE_HUMIDITY_MEASURED_VALUE);
        }
    if (temp_sensor_type == EDS18B20) {
            bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
        }
    if(sensor_init)
      osal_pwrmgr_task_state(zclApp_TaskID, PWRMGR_CONSERVE);
}

void UartProcessData (uint8 port, uint8 event)
{
  uint8 ch,count=0;
  uint8 buf[15];
  LREPMaster("UART Started \r\n"); 
  if (event &(HAL_UART_RX_FULL|HAL_UART_RX_ABOUT_FULL|HAL_UART_RX_TIMEOUT))
    {
      LREPMaster("UART Recieved \r\n");
      while (Hal_UART_RxBufLen(port))
        {
          HalUARTRead (port, &ch, 1);
          buf[count++]=ch; //copy the received byte
          //printf("%x ", ch);
        }
      //for(int i=0;i<count;i++){
        //LREP(" %x ", buf[i] & 0xff);
       // printf("%x \r\n", buf[i]);
      //}
      //printf("count=%d \r\n", count);
      ProcessUartResults(buf, count);
      
     }
}

static void ProcessUartResults(uint8* data, uint8 count)
{
  
  uint16 ppm = 0;
  
  LREPMaster("Process Uart Results \r\n");
  ppm = (*air_dev->Read)(data);
  
  if (ppm == AIR_QUALITY_INVALID_RESPONSE) {
      LREPMaster("Sensor type UNKNOWN continue detect\r\n");
      }
  else if (ppm == AIR_QUALITY_ABC_RESPONSE){
      LREPMaster("ABC Response\r\n");
      }
   else {
      sensor_init = true; 
      //LREP("PPM Received CO2=%d ppm\r\n", ppm);
      zclApp_Sensors.CO2_PPM = ppm;
      zclApp_Sensors.CO2 = (double)ppm / 1000000.0;
      //printf ("ppm: %d \r\n", zclApp_Sensors.CO2_PPM);
      //printf ("ppm_f: %4.10f \r\n", zclApp_Sensors.CO2);
      //LREPMaster("Report PPM\r\n");
      bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, ZCL_CO2, ATTRID_CO2_MEASURED_VALUE);  
      
      zclApp_LedFeedback();
      zclApp_PerformABC();
      }
    
   zclApp_ReadBMEDS18B20();
   
}


static void zclApp_ReadSensors(void) {
    
   if (zclApp_Config.LedFeedback) {
        HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);
    }
    if(!sensor_init)
      air_dev = (air_dev == &sense_air_dev) ? &MHZ19_dev : &sense_air_dev;
    
    (*air_dev->RequestMeasure)();

}

static void zclApp_Report(void) {
     
     osal_pwrmgr_task_state(zclApp_TaskID, PWRMGR_HOLD);  
     
     osal_start_timerEx(zclApp_TaskID, APP_READ_SENSORS_EVT, APP_EVT_DELAY);
     }

static void zclApp_BasicResetCB(void) {
    LREPMaster("BasicResetCB\r\n");
    zclApp_ResetAttributesToDefaultValues();
    zclApp_SaveAttributesToNV();
}

static ZStatus_t zclApp_ReadWriteAuthCB(afAddrType_t *srcAddr, zclAttrRec_t *pAttr, uint8 oper) {
    LREPMaster("AUTH CB called\r\n");
    osal_start_timerEx(zclApp_TaskID, APP_SAVE_ATTRS_EVT, 2000);
    return ZSuccess;
}

static void zclApp_SaveAttributesToNV(void) {
    uint8 writeStatus = osal_nv_write(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
    LREP("Saving attributes to NV write=%d\r\n", writeStatus);
}

static void zclApp_RestoreAttributesFromNV(void) {
    uint8 status = osal_nv_item_init(NW_APP_CONFIG, sizeof(application_config_t), NULL);
    LREP("Restoring attributes from NV  status=%d \r\n", status);
    if (status == NV_ITEM_UNINIT) {
        uint8 writeStatus = osal_nv_write(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
        LREP("NV was empty, writing %d\r\n", writeStatus);
    }
    if (status == ZSUCCESS) {
        LREPMaster("Reading from NV\r\n");
        osal_nv_read(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
    }
}

static uint8 zclApp_RequestBME280(struct bme280_dev *dev) {
    int8_t rslt = bme280_init(dev);
    if (rslt == BME280_OK) {
        uint8_t settings_sel;
        dev->settings.osr_h = BME280_OVERSAMPLING_16X;
        dev->settings.osr_p = BME280_OVERSAMPLING_16X;
        dev->settings.osr_t = BME280_OVERSAMPLING_16X;
        dev->settings.filter = BME280_FILTER_COEFF_16;
        dev->settings.standby_time = BME280_STANDBY_TIME_62_5_MS;

        settings_sel = BME280_OSR_PRESS_SEL;
        settings_sel |= BME280_OSR_TEMP_SEL;
        settings_sel |= BME280_OSR_HUM_SEL;
        settings_sel |= BME280_STANDBY_SEL;
        settings_sel |= BME280_FILTER_SEL;
        rslt = bme280_set_sensor_settings(settings_sel, dev);
        rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, dev);
    } else {
        LREP("ReadBME280 init error %d\r\n", rslt);
        return 1;
    }
    return 0;
}
static uint8 zclApp_ReadBME280(struct bme280_dev *dev) {
    struct bme280_data bme_results;
    int8_t rslt = bme280_get_sensor_data(BME280_ALL, &bme_results, dev);
    if (rslt == BME280_OK) {
        LREP("ReadBME280 t=%ld, p=%ld, h=%ld\r\n", bme_results.temperature, bme_results.pressure, bme_results.humidity);
        zclApp_Sensors.BME280_HumiditySensor_MeasuredValue = (uint16)(bme_results.humidity * 100 / 1024.0) + zclApp_Config.HumidityOffset;
        zclApp_Sensors.BME280_PressureSensor_ScaledValue = (int16) (pow(10.0, (double) zclApp_Sensors.BME280_PressureSensor_Scale) * (double) bme_results.pressure);
        zclApp_Sensors.BME280_PressureSensor_MeasuredValue = (int16) (zclApp_Sensors.BME280_PressureSensor_ScaledValue / 10.0);
        zclApp_Sensors.Temperature = (int16)bme_results.temperature + zclApp_Config.TemperatureOffset;
    } else {
        LREP("ReadBME280 bme280_get_sensor_data error %d\r\n", rslt);
        return 1;
    }
    return 0;
}




/****************************************************************************
****************************************************************************/
