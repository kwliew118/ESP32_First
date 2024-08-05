/* Someone quoted to solve cmake.cmakepath on VS code is to
  "cmake.cmakePath": "/usr/local/bin/cmake" in project setting */
#include "nvs_flash.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_efuse.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/gpio.h"
//#include "driver/uart.h"
#include "freertos/queue.h"
//#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gptimer.h"
#include "esp_event.h"
#include "esp_system.h"
#include "driver/gpio.h"

/* ESP32-module      EVB Board   */
/* GPIO 22(Pin 36)    Pin 14      */
/* GPIO 23(Pin 37)    Pin 15      */
/* GPIO 15 (Pin 23)     Pin 3 */
#define GPIO_ClrD   19
#define GPIO_Sw1    22  
#define GPIO_Sw2    23  
#define GPIO_INPUT_PIN_SEL ((1ULL<<GPIO_Sw1)|(1ULL<<GPIO_Sw2)|(1ULL<<GPIO_ClrD))

#define LED_BLUE    2
#define LED_LIGHT   21
#define LED_Show    15
#define GPIO_OUTPUT_PIN_SEL ((1ULL<<LED_BLUE)|(1ULL<<LED_LIGHT)|(1ULL<<LED_Show))

int In1_LevH, In1_Pressed;
int In2_LevH, In2_Pressed;
int In3_LevH, In3_Pressed;
int cnts;
int tog;
int tog2;
int trigcnt1,trigcnt2;
int init_In1, init_In2;

int tognew;
/* Example of key-value pair onto separate namespace */

int8_t cnt1 = 0;
nvs_handle_t my_handle; /* Use as NVS handle */
/* Defining 5 array each with 40 char spaces */

char exbuffer[10][40];
char buffer[400] = {"\0"};
void get_sw1Pressed(void);
void get_sw2Pressed(void);
void get_sw3Pressed(void);
void set_nvs_data(void);

void static splitstring(char *buffer);
int32_t get_no_entries(void);
void read_keydemo(void);
void write_keydemo(void);
void read_strdemo(void);
void write_strdemo(void);
void write_nvs(char inbuf[10][40]);


void cvint2hex(uint8_t *macin);
static void gpio_task(void* arg);
void setup_gpio(void);

int32_t rd_nvs_i32(char *key);
void wr_nvs_i32(char *key, int32_t valx);
void set_keyval(uint8_t no, char pkey[15]);


char macstr[13];
static const char *TAG = "Nvs";
#define _ON    1
#define _OFF   0

uint8_t sw1_cnt = 0;
uint8_t sw2_cnt = 0;
uint8_t sw3_cnt = 0;


typedef struct {
    uint64_t event_count;
} example_queue_element_t;
example_queue_element_t ele;
QueueHandle_t queue;
gptimer_handle_t gptimer;

static bool IRAM_ATTR example_timer_on_alarm_cb_v2(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    // Retrieve count value and send to queue
    example_queue_element_t ele = {.event_count = edata->count_value };
    xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    return (high_task_awoken == pdTRUE);
}


void app_main(void)
{
    esp_err_t ret;
     setup_gpio();
    /* Initialize NVS - Non volatile storage */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        ESP_LOGI(TAG,"NVS error detected");
    }
   /* If error occurs, error message will be displayed & program aborted */
    ESP_ERROR_CHECK(ret);

    read_strdemo();
    read_keydemo();
   
    queue = xQueueCreate(10, sizeof(uint64_t));
    if (!queue) {
        ESP_LOGE(TAG, "Creating queue failed");
        return;
    }
    ESP_LOGI(TAG, "Create timer handle");
    gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  /* Tick time = 1/Freq = 1/1Mhz = 1us */
    };
    /* Create a timer returning the handle */
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    /* Set a callback function when alarm triggers */
    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v2,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, queue));
   
    ESP_LOGI(TAG, "Enable 1 timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Set start count = 0");
    ESP_ERROR_CHECK(gptimer_set_raw_count(gptimer, 0));
    ESP_LOGI(TAG, "Configure alarm at 50ms and restart from 0 count");
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0, /* 1,000,000 -1s 100000 - 0.1s */
        .alarm_count = 50000, // period = 50ms
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config2));
    
    ESP_LOGI(TAG, "Start timer");
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    gpio_set_level(LED_Show,1);
   xTaskCreate(gpio_task, "gpio_tmr0", 4096, NULL, 10, NULL);
}

/* char* strcpy(char* dest, const char* src);
   The strcpy() function copies the C-string pointed to by src to the memory location 
   pointed to by dest. The null terminating character '\0' is also copied.
   Notice that:
   src is of const char* type. The const keyword ensures that the C-string pointed to by src 
   cannot be modified by strcpy().
   dest is of char* type. The absence of const ensures that the C-string pointed to by dest
   can be modified by strcpy(). */

void clr_nvs_data(void){
  for (uint8_t i = 0; i <4; i++){
     strcpy(exbuffer[i],"\0");  /* Each array buffer has NULL terminated char */
  } 
}
/* Try setup 5 lines of strings data */
void setup_str_data(void){
   /* Each string is terminated by a NULL char */
   strcpy(exbuffer[0],"L1-Learn nvs(Non-Volatile-Storage)"); // printf("buf0 len %d",strlen(exbuffer[0]));
   strcpy(exbuffer[1] ,"L2-Save by line of string ");
   strcpy(exbuffer[2] ,"L3-Separate them by newline char 0x0A ");
   strcpy(exbuffer[3] ,"L4-Save into nvs now");
   strcpy(exbuffer[4] ,"L5-fantasy begins");
}

void read_strdemo(void){
   esp_err_t err;
   err = nvs_open("Str_space", NVS_READWRITE, &my_handle);
   if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
   } 
   else {
      printf("NVSStr_space opening okay\n");
      /* Test the data size before reading into buffer */
      size_t required_size;
      err = nvs_get_str(my_handle, "keyname", NULL, &required_size);
      ESP_LOGI(TAG,"nvs_get_str err code = [ %s ]",esp_err_to_name(err));
      switch (err) {
          case ESP_OK:
              printf("NVS reading succeeded\n");
              break;
          case ESP_ERR_NVS_NOT_FOUND:
              nvs_close(my_handle);
              printf("The nvs string data does not exist!\n");
              return;
              break;
          default :
              printf("Error (%s) reading str!\n", esp_err_to_name(err));
              nvs_close(my_handle);
          break; 
      }
      /* End of switch */

      printf("Nvs data size retrieved = [%d] \n",required_size);
      /* Allocate a memory space to store these data */
      char *dblock = malloc(required_size);
      err = nvs_get_str(my_handle, "keyname", dblock, &required_size);
      switch (err) {
          case ESP_OK:
              printf("nvs content are : \n");
              printf("%s",dblock);
              splitstring(dblock);
              break;
          case ESP_ERR_NVS_NOT_FOUND:
               printf("The string is not initialized yet!\n");
               break;
          default :
               printf("Error (%s) reading str!\n", esp_err_to_name(err));
        }
        nvs_close(my_handle);
    }
}
void write_strdemo(void){
    setup_str_data();
    write_nvs(exbuffer);
}
/* strcpy will copy from src to dest and add a NULL at the end*/
/* strcat wll concatenate the string with a newline char*/
void write_nvs(char inbuf[10][40]){
    esp_err_t err;
    char buffer[400] = {"\0"};
    /* INLINE is added to separate the string array */
    /* Adding array string into buffer pointer one by one */
    for (uint8_t c = 0; c <=4; c++) {
      strcat(buffer,inbuf[c]); /* The array must be NULL terminated*/
      strcat(buffer,"\n"); /* Add a newline separator */
    }
    err = nvs_open("Str_space", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } 
    else {
      printf("NVS str space opening OK\n");
      err = nvs_set_str(my_handle, "keyname", buffer);
      ESP_LOGI(TAG,"Writing (key + str) ");
      // Commit written value.
      // After setting any values, nvs_commit() must be called to ensure changes are written
      // to flash storage. Implementations may write to storage at other times,
      // but this is not guaranteed.
       printf("Committing updates in NVS ... ");
       err = nvs_commit(my_handle);
       printf((err != ESP_OK) ? "NVS writing failed!\n" : "Done\n");
       // Close
       nvs_close(my_handle);
     }
}
/* Array string are separated by NULL character */
void static splitstring(char *buffer){
   uint8_t c1;
   char *ptr = buffer; 
   c1 = 0;
   printf("Running split strings \n");
   while (1){
      /* Search a newline char and save its position in var pos */
      char *pos = strchr(ptr, 0x0A);  /* Value 0x0A is newline char*/
      /* NLINE char found */
      /* Eg: String  = {Abc  NULL NLINE}  10 11 12  13[NULL] 14[NLINE] --> 14-10  = 4 */  
      /* pos = 14 ptr = 10  pos-ptr = 4 (copy 4 bytes) */   
      if (pos != NULL ){  
        /* Eg        buf0 ,  10,  4 bytes    */
         strncpy(exbuffer[c1],ptr,(pos-ptr)); /* Copy without NLINE */
         //uint8_t sz = (uint8_t)(pos-ptr);
        // exbuffer[c1][sz]= '\0'; /* Add extra NULL */
         printf("Line %d - [%s]\n",c1+1, exbuffer[c1]);
         //strcpy(ptr,pos+1); /* advance ptr */
         ptr = pos+1;  /* Alternative method strcpy(ptr,pos+1) */
         c1++;
      }
      else{
        break;
      }
    }
    printf("No of data %d",c1);
}
/* Check no of entries within the save [namespace1]  */
int32_t get_no_entries(void){
  esp_err_t err;
  err = nvs_open("kyval_space", NVS_READWRITE, &my_handle);
  ESP_LOGI(TAG,"kyval_space opening status code = [ %s] ",esp_err_to_name(err));
  size_t used_entries;
  if  (nvs_get_used_entry_count(my_handle, &used_entries) == ESP_OK){
     printf("Total keyvalue pair = [%d] \n",used_entries);
     return(used_entries);
  }
  return(0);
}

/* Read keyvalue from nvs - return a 32 bit numbers */
int32_t rd_nvs_i32(char *key){
  esp_err_t err;
  int32_t valr = 0;
  err = nvs_open("kyval_space", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
      // value will default to 0, if not set yet in NVS
     err = nvs_get_i32(my_handle, key, &valr);
     switch (err) {
         case ESP_OK:
            //printf("Read i32 okay\n");
         break;
         case ESP_ERR_NVS_NOT_FOUND:
            printf("Key not exist!\n");
         break;
         default :
            printf("Error (%s) reading keyvalue!\n", esp_err_to_name(err));
      }
    }
    nvs_close(my_handle);
    return(valr);
}
/* Nvs can organise key value pair in separate namespace - nvs_open("namespace", ) 
   Different namespace can hole same keyname but it is stored separately */
void wr_nvs_i32(char *key, int32_t valx){
  esp_err_t err;
  err = nvs_open("kyval_space", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
      err = nvs_set_i32(my_handle, key, valx);
      printf ((err != ESP_OK) ? "Write key failure\n" : "Write key+int32 OK\n");
  }
  nvs_close(my_handle);
}

/* Return a key name of key X wrt input no */
void set_keyval(uint8_t no, char pkey[15]){
  char p1str[15] = "key";  
  char p2str[5] ;
  itoa(no,p2str,10);  /* save converted int into p2str - base 10 */
  strcat(p1str,p2str);
  strcpy(pkey,p1str);
}
/* Demo on writing key values pair */
void write_keydemo(void){
  uint32_t valst = 300;
  char retkey[15];
  for (uint8_t i = 1; i <=5; i++){
     set_keyval(i, retkey);
     ESP_LOGI(TAG,"Write (key + int) key=[%s] val=[%ld]",retkey,valst);
     wr_nvs_i32(retkey,valst);
     valst += 5;
  }
}
/* Reading keys from space 1*/
void read_keydemo(void){
  char retkey[15];
  size_t entries;
  /* Check no of entries */
  entries =  get_no_entries();
  if (entries >0 ){
    /* Read 10 key values written */              
    for (uint8_t i = 1; i <=5; i++){
       set_keyval(i, retkey);
       printf("Reading key [%s] ",retkey);
       int32_t val = rd_nvs_i32(retkey);
       printf("Data = [ %ld]\n",val);
    }
  }
}

/* Convert 6 byte MAC (integer) into HEX format */
void cvint2hex(uint8_t *macin){
  uint8_t cnt ,hnib,lnib;
  /* uint8 value range 0..255 */
  macstr[12] = '\0';
  for (cnt = 0; cnt <=5; cnt++){
     hnib = macin[cnt] & 0xF0;
    hnib = hnib >>4;
     /* 0,2,4,6,8,10 */
     if (hnib <=9){
       macstr[cnt*2] = (int)('0'+hnib);
     }
     else if ((hnib >=10) && (hnib <=15)) {
       macstr[cnt*2] = (int)('A'+(hnib-10));
     }
     /* 1,3,5,7,9,11 */
     lnib = macin[cnt] & 0x0F;
     if (lnib <=9) {
       macstr[(cnt*2)+1] = (int)('0'+lnib);
     }
     else if ((lnib >=10) && (lnib <=15)) {
        macstr[(cnt*2)+1] = (int)('A'+(lnib-10));
     }
  }
}

void setup_gpio(void){
    gpio_config_t io_conf;
    /* OUTPUT setup */
    io_conf.mode = GPIO_MODE_OUTPUT;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO output with given settings
    gpio_config(&io_conf);

    /* INPUT setup */
    io_conf.mode = GPIO_MODE_INPUT;  /* set as input mode */
    io_conf.intr_type = GPIO_INTR_DISABLE; /* Trailing edge */
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);

    gpio_set_level(LED_BLUE,_OFF);
}

/* 5ms INT is generated by setting an alarm counter NNN that matches
   the counter value.  An alarm is generated & ISR call will send
   and event data */

/* Input is read wrt ISR Hardware timer callback - 5ms rate */
static void gpio_task(void *arg)
{
  esp_err_t ret;
  while (1) {
      /* The 3rd para is just the blocking time */
      if (xQueueReceive(queue, &ele, pdMS_TO_TICKS(1000))) {
      //    ESP_LOGI(TAG, "Timer reloaded, count=%llu", ele.event_count);
          tog ^= 1;
          if (tog == 0) { 
             // gpio_set_level(LED_BLUE,0);
              gpio_set_level(LED_LIGHT,0);
          }
          else {
             // gpio_set_level(LED_BLUE,1);
              gpio_set_level(LED_LIGHT,1);
          }
          /* Event runs at 50ms */
          /* Check Sw1 press - H to L trail */
          get_sw1Pressed();
          if (In1_Pressed){
             ESP_LOGW(TAG,"Sw1 pressed %d times [Read]",sw1_cnt);
             In1_Pressed = 0; sw1_cnt++;
              read_keydemo();
              read_strdemo();
           }
           /* End of sw1 */
           
           /* Check Sw2 press */           
           get_sw2Pressed();
           if (In2_Pressed){
              ESP_LOGW(TAG,"Sw2 pressed %d times [Write]",sw2_cnt);
               In2_Pressed = 0; sw2_cnt++;
               
              /* Write 10 values to Storage 1 */
               write_keydemo();
               write_strdemo();
           }
           /* End of Sw 2 */

           /* Check Sw3 press */           
           get_sw3Pressed();
           if (In3_Pressed){
               In3_Pressed = 0;
               ESP_LOGI(TAG,"Sw3 pressed %d times [Clear]",sw3_cnt);
               ESP_ERROR_CHECK(nvs_flash_erase());
               ret = nvs_flash_init();
               if (ret == ESP_OK){
                ESP_LOGI(TAG,"Nvs re-initialised ok");
               }
               else{
                ESP_LOGW(TAG,"Init error %d ",ret);
               }

           }
        }
      /* End of XQueue */
      else {
            ESP_LOGW(TAG, "Missed one count event");
      }
   }
}

void get_sw1Pressed(void){
  if (In1_LevH == 0) {
    if (gpio_get_level(GPIO_Sw1)) In1_LevH  = 1;
  }
  else {
     if (!(gpio_get_level(GPIO_Sw1))) {
        In1_LevH = 0; In1_Pressed = 1;
       
     }
  }
}
void get_sw2Pressed(void){
  if (In2_LevH == 0) {
     if (gpio_get_level(GPIO_Sw2)) In2_LevH  = 1;
  }
  else if (gpio_get_level(GPIO_Sw2) == 0) {
     In2_LevH = 0; In2_Pressed = 1;
  }
}
void get_sw3Pressed(void){
  if (In3_LevH == 0) {
     if (gpio_get_level(GPIO_ClrD)) In3_LevH  = 1;
  }
  else if (gpio_get_level(GPIO_ClrD) == 0) {
     In3_LevH = 0; In3_Pressed = 1;
  }
}
