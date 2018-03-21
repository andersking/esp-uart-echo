#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "user_interface.h"
#include "espconn.h"


// prototypes
void dnsconnect_cb (void *arg);
void connect_cb (void *arg);
void recon_cb (void *arg,sint8 err);
void disconnect_cb (void *arg);
void sent_cb(void *arg);
void recv_cb(void *arg,char *pdata,unsigned short len);
void dns_cb(const char *name, ip_addr_t *ipaddr, void *arg);
void wakeup_cb(void);
void loop(void *arg);

// wifi AP info 
struct station_config stationConf = { .ssid = SSID, .password = SSID_PASSWORD , .bssid_set = 0 };

// tcp connection info
struct espconn con;
struct _esp_tcp tcp;

// server to notify 
ip_addr_t server_ip = { .addr = 0};

os_timer_t timer;

// string to hold response to server
char response[50];

// current state
typedef enum {
                RESET,
                WAKE_UP,
                AP_WAIT,
                AP_UP,
                DNS_UP,
                CONNECT,
                CONN_WAIT,
                CONN_UP,
                DISCONNECT,
                DATA_SENDING,
                DATA_SENT,
                WAIT,
                SLEEP
             } state_t;
state_t state = RESET;

//Init function 

void ICACHE_FLASH_ATTR
user_init()
{

// init gpio 
    gpio_init();
//    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
//    GPIO_DIS_OUTPUT(GPIO_ID_PIN(0));
//    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1);
    GPIO_DIS_OUTPUT(GPIO_ID_PIN(1));
//    ETS_GPIO_INTR_DISABLE();
    PIN_PULLUP_EN(PERIPHS_IO_MUX_U0TXD_U);

    //Set station mode

    state = RESET;
    os_timer_setfn( &timer, loop, 0 );
    os_timer_arm( &timer, 0, 0 );

    espconn_init();
}

// main loop
void loop(void *arg)
{
    struct ip_info ipconfig;

    os_timer_disarm( &timer );
    int timer_wait = 100;
    volatile int status = 0;
    
    switch(state)
    {
        
        case WAKE_UP: 

            wifi_fpm_close();
            os_timer_setfn( &timer, loop, 0 );
            
        case RESET:

            wifi_set_opmode( STATION_MODE );
            wifi_station_set_config(&stationConf);
            wifi_station_connect();

            state = AP_WAIT;
            break;

        case AP_WAIT:
            wifi_get_ip_info(STATION_IF, &ipconfig);
            
            if ( wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0 ) 
            {
                state = AP_UP;
            } else {
                timer_wait = 1000;
            }
            break;
        case AP_UP:
            espconn_gethostbyname(&con, SERVER_NAME, &server_ip, dns_cb);
            timer_wait = 1000;
            break;
        case DNS_UP:
            con.type = ESPCONN_TCP;
            con.state = ESPCONN_NONE;
            con.proto.tcp = &tcp;
            con.proto.tcp->remote_port = 2000;

            os_memcpy(con.proto.tcp->local_ip, &ipconfig.ip, 4);
            os_memcpy(con.proto.tcp->remote_ip, &server_ip, 4);

            espconn_regist_connectcb(&con,connect_cb);
            espconn_regist_reconcb(&con,recon_cb);
            espconn_regist_disconcb(&con,disconnect_cb);
            espconn_regist_sentcb(&con,sent_cb);
            espconn_regist_recvcb(&con,recv_cb);
            con.proto.tcp->local_port = espconn_port();
        case CONNECT:
            state = CONN_WAIT;
            espconn_connect(&con);
            break;
        case CONN_WAIT:
            break;
        case CONN_UP:
            status = GPIO_INPUT_GET(1);
//            os_sprintf(response,"%d.%d.%d.%d - %s",
//                *((uint8 *)&server_ip.addr),
//                *((uint8 *)&server_ip.addr + 1),
//                *((uint8 *)&server_ip.addr + 2),
//                *((uint8 *)&server_ip.addr + 3),
//                (status?"Open\n":"Closed\n"));
            os_sprintf(response,"%s\n",(status?"Closed\n":"Open\n"));
            espconn_send(&con,response,os_strlen(response));    
            timer_wait = 100;
            state = DATA_SENDING;
            break;
        case DATA_SENDING:
            break;
        case DATA_SENT:
            espconn_disconnect(&con);
            state = WAIT;
        case DISCONNECT:
            // break down connection?
            if (   con.state == ESPCONN_NONE 
                || con.state == ESPCONN_WAIT 
                || con.state == ESPCONN_NONE)
            {
                state = WAIT;
            }
            break;
        case WAIT:
            // we will "light sleep" to wait
            
            timer_wait = SLEEP_TIME*100;
//            wifi_fpm_auto_sleep_set_in_null_mode(0);
//            wifi_set_opmode( NULL_MODE );
//            wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
//            wifi_fpm_open();
//            wifi_fpm_set_wakeup_cb(wakeup_cb); 
            state = SLEEP;
//            wifi_fpm_do_sleep(SLEEP_TIME * 1000 ); 
            break;
        case SLEEP:
            state=CONN_UP;
            ;
        default:
            ;
    }     
//    if (state != SLEEP)
//    {
        os_timer_arm( &timer, timer_wait, 0 );
//    } else {
//        loop(0);
//    }
}

void 
dns_cb(const char *name, ip_addr_t *ipaddr, void *arg)
{
    if (ipaddr == NULL) 
    {
        return;
    }
    if (server_ip.addr == 0 && ipaddr->addr !=0)
    {
        os_timer_disarm(&timer);
        state = DNS_UP;
        server_ip.addr = ipaddr->addr;
        os_timer_arm( &timer, 100, 0 );
    }
}
/*

TCP Handlers

- Initialize espconn parameters according to protocols.
- Register connect callback and reconnect callback function.
    - (Call espconn_regist_connectcb and espconn_regist_reconcb )
- Call espconn_accept function to listen to the connection with host.
- Registered connect function will be called after successful connection, which will register corresponding callback function.
    - (Call espconn_regist_recvcb , espconn_regist_sentcb and espconn_regist_disconcb in connected callback)
*/

//void espconn_connect_callback (void *arg)
void connect_cb (void *arg)
{
    os_timer_disarm(&timer);
    espconn_set_opt(&con,ESPCONN_REUSEADDR);
    state = CONN_UP;
    os_timer_arm(&timer,100,0);
}

void recon_cb (void *arg, sint8 err)
{
    os_timer_disarm(&timer);
//    state = WAIT;
    state = CONNECT;
    os_timer_arm(&timer,100,0);
}

void disconnect_cb (void *arg)
{
    os_timer_disarm(&timer);
//    state = WAIT;
    state = CONNECT;
    os_timer_arm(&timer,100,0);
}

//void espconn_sent_callback (void *arg)
void sent_cb(void *arg)
{
    // data sent
    os_timer_disarm(&timer);
//    state = DATA_SENT;
    state = WAIT;
    os_timer_arm(&timer,10,0);
    
}

//void espconn_recv_callback (void *arg,char *pdata,unsigned short len)
void recv_cb(void *arg,char *pdata,unsigned short len)
{
    
}

// wakeup from sleep 
void wakeup_cb(void)
{
    state = WAKE_UP; 
    loop(0); // can't use timer when sleep enabled
}
